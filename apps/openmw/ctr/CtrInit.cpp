#ifdef __3DS__

// 3DS (CTR) implementation of the platform layer declared in vita/VitaInit.h.
// The namespace keeps the Vita name so the ~100 existing call sites compile
// unchanged on both handhelds.

#include "../vita/VitaInit.h"

#include <3ds.h>
#include <thread>

#include <components/ctr/gpu/ctrgpu.hpp>

#include <LinearMath/btAlignedAllocator.h>

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <dirent.h>
#include <exception>
#include <malloc.h>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <typeinfo>
#include <unistd.h>
#include <vector>

extern "C" {

// Main thread stack (read by the 3dsx crt0; for CIA the exheader StackSize
// in the RSF must match). OpenMW parses deep NIF/ESM recursion — 2MB.
u32 __stacksize__ = 2 * 1024 * 1024;

u32 __ctru_linear_heap_size = 16 * 1024 * 1024;

// Our replacement for libctru's weak __system_allocateHeaps. The stock
// auto-size grabs ALL remaining APPLICATION memory for the heap; in the
// 178MB dev mode with a 14MB linear heap that is ~133MB — which overflows
// the process heap VA window (a single MEMOP_ALLOC block; observed as a
// pre-main PC=0 crash from svcBreak in crt0). Clamp the heap to 120MB:
// the 178MB dev config ran fine at 123MB, and the retail 124MB mode only
// yields ~79MB so the clamp never triggers there.
extern "C" {
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
extern u32 __ctru_heap_size;

void __system_allocateHeaps(void)
{
    Result rc;

    Handle reslimit = 0;
    rc = svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE);
    if (R_FAILED(rc))
        svcBreak(USERBREAK_PANIC);

    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType reslimitType = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, reslimit, &reslimitType, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &reslimitType, 1);
    svcCloseHandle(reslimit);

    u32 remaining = (u32)(maxCommit - currentCommit) & ~0xFFF;

    if (remaining >= 80u * 1024u * 1024u)
        __ctru_linear_heap_size = 25 * 1024 * 1024;

    if (__ctru_linear_heap_size > remaining)
        svcBreak(USERBREAK_PANIC);

    if (__ctru_heap_size == 0)
        __ctru_heap_size = remaining - __ctru_linear_heap_size;

    // 126MB: the heap VA window is [0x08000000, 0x10000000) = 128MB and the
    // linear heap lives in a separate region, so only the window bounds the
    // cap. 120MB starved the world's first frames (v54 ran at ~123MB).
    const u32 kHeapCap = 126u * 1024u * 1024u;
    if (__ctru_heap_size > kHeapCap)
        __ctru_heap_size = kHeapCap;

    rc = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC,
        (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
    if (R_FAILED(rc))
        svcBreak(USERBREAK_PANIC);

    rc = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR,
        (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
    if (R_FAILED(rc))
        svcBreak(USERBREAK_PANIC);

    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
}

static FILE* s_debugLogFile = nullptr;

extern "C" {
extern volatile int g_ctrFrameNo;
}

void vitaBreadcrumb(const char* msg)
{
    // Mirror boot-stage crumbs to the bottom-screen console until the main
    // loop starts (cheap gate: the frame counter only moves in-game).
    if (g_ctrFrameNo == 0)
        printf("%s\n", msg);
    FILE* f = fopen("sdmc:/3ds/openmw/boot.log", "ab");
    if (f)
    {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

void vitaTimedBreadcrumb(const char* msg)
{
    FILE* f = fopen("sdmc:/3ds/openmw/boot.log", "ab");
    if (f)
    {
        u64 ms = osGetTime();
        fprintf(f, "[%llu] %s\n", (unsigned long long)ms, msg);
        fclose(f);
    }
}

void vitaMemBreadcrumb(const char* msg)
{
    FILE* f = fopen("sdmc:/3ds/openmw/boot.log", "ab");
    if (f)
    {
        struct mallinfo mi = mallinfo();
        fprintf(f, "[CtrMem] %s | used: %uKB | free: %uKB | linear: %uKB\n", msg,
            (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
            (unsigned)(linearSpaceFree() / 1024));
        fclose(f);
    }
}

// Hang-diagnosis beacon: the engine main loop bumps g_ctrFrameNo once per
// frame and hot code paths stamp g_ctrBeacon with a static string naming the
// phase they just entered. The watchdog thread below prints both when the
// counter stops moving, so a wedged main thread still leaves a trail.
extern "C" {
volatile int g_ctrFrameNo = 0;
const char* volatile g_ctrBeacon = "boot";
}

// newlib stubs for POSIX functions referenced at link time
int fchown(int /*fd*/, uid_t /*owner*/, gid_t /*group*/)
{
    errno = ENOSYS;
    return -1;
}

int lchown(const char* /*path*/, uid_t /*owner*/, gid_t /*group*/)
{
    errno = ENOSYS;
    return -1;
}

// SQLite VFS stubs — built with SQLITE_OS_OTHER=1 since 3DS lacks
// flock/fcntl/mmap needed by the Unix VFS.
int sqlite3_os_init(void)
{
    return 0; /* SQLITE_OK */
}
int sqlite3_os_end(void)
{
    return 0;
}

} // extern "C"

#include <sys/iosupport.h>

namespace
{
    ssize_t discardWrite(struct _reent*, void*, const char*, size_t len)
    {
        return static_cast<ssize_t>(len);
    }

    const devoptab_t sDiscardOutput = { .name = "discard", .structSize = 0, .write_r = &discardWrite };
}

extern "C" void ctrDetachConsole(void)
{
    fflush(stdout);
    fflush(stderr);
    devoptab_list[STD_OUT] = &sDiscardOutput;
    devoptab_list[STD_ERR] = &sDiscardOutput;
}

#ifndef OPENMW_CTR_BUILD_STAMP
#define OPENMW_CTR_BUILD_STAMP __DATE__ " " __TIME__
#endif

extern "C" void ctr_checked_free(void* ptr, void* caller);
extern "C" void* ctr_malloc_from(std::size_t size, void* caller);
extern "C" void ctr_diag_open(void);
extern "C" void ctr_diag_write(const char* text);
extern "C" void* volatile g_ctrMallocCaller;
extern "C" void* volatile g_ctrFreeCaller;

// Sized global operator new: newlib's sbrk allocator has no mmap path, so a
// single large request needs that much CONTIGUOUS arena. When boot OOMs with
// megabytes "free", the culprit is one big allocation — log its size so the
// offending container can be found instead of guessed at.
void* operator new(std::size_t size)
{
    // Ledger of big allocations: caller PC (LR) is addr2line-able against the
    // matching openmw.elf. Level 0 only — -fomit-frame-pointer breaks deeper.
    if (size >= 4 * 1024 * 1024)
    {
        FILE* f = fopen("sdmc:/3ds/openmw/bigalloc.log", "ab");
        if (f)
        {
            struct mallinfo mi = mallinfo();
            fprintf(f, "[BIG] size=%uKB used=%uKB free=%uKB ra=%p\n", (unsigned)(size / 1024),
                (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024), __builtin_return_address(0));
            fclose(f);
        }
    }
    void* p = ctr_malloc_from(size, __builtin_return_address(0));
    if (!p)
    {
        FILE* f = fopen("sdmc:/3ds/openmw/boot.log", "ab");
        if (f)
        {
            struct mallinfo mi = mallinfo();
            fprintf(f, "[NEW_FAIL] size=%u KB | used: %u KB | free: %u KB | ra=%p | beacon=%s\n",
                (unsigned)(size / 1024), (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
                __builtin_return_address(0), (const char*)g_ctrBeacon);
            fclose(f);
        }
        // Give the new-handler protocol a chance (emergency reserve release)
        std::new_handler h = std::get_new_handler();
        while (!p && h)
        {
            h();
            p = malloc(size);
            h = std::get_new_handler();
            if (p)
                break;
            // handler either fixed something or exited the process
            static int retries = 0;
            if (++retries > 4)
                break;
        }
        if (!p)
            throw std::bad_alloc();
    }
    return p;
}

void* operator new[](std::size_t size)
{
    return operator new(size);
}

void operator delete(void* p) noexcept
{
    ctr_checked_free(p, __builtin_return_address(0));
}

void operator delete[](void* p) noexcept
{
    ctr_checked_free(p, __builtin_return_address(0));
}

void operator delete(void* p, std::size_t) noexcept
{
    ctr_checked_free(p, __builtin_return_address(0));
}

void operator delete[](void* p, std::size_t) noexcept
{
    ctr_checked_free(p, __builtin_return_address(0));
}

namespace Vita
{
    static size_t s_minHeapFree = SIZE_MAX;

    // Emergency reserve: freed on first OOM to give breathing room for
    // the failing allocation. Smaller than Vita's 6MB — memory is scarcer.
    static constexpr size_t EMERGENCY_RESERVE_SIZE = 2 * 1024 * 1024;
    static void* s_emergencyReserve = nullptr;

    void breadcrumb(const char* msg)
    {
        vitaBreadcrumb(msg);
    }

    size_t getFreeUserMemory()
    {
        struct mallinfo mi = mallinfo();
        return static_cast<size_t>(mi.fordblks);
    }

    void logMemoryStatus(const char* label)
    {
        struct mallinfo mi = mallinfo();
        size_t heapFree = static_cast<size_t>(mi.fordblks);
        if (heapFree < s_minHeapFree)
            s_minHeapFree = heapFree;

        char buf[192];
        snprintf(buf, sizeof(buf), "[CtrMem] %s | used: %uKB | free: %uKB | min: %uKB | linear: %uKB", label,
            static_cast<unsigned>(mi.uordblks / 1024), static_cast<unsigned>(heapFree / 1024),
            static_cast<unsigned>(s_minHeapFree / 1024), static_cast<unsigned>(linearSpaceFree() / 1024));
        breadcrumb(buf);
        Log(Debug::Info) << buf;
    }

    static void releaseEmergencyReserve()
    {
        void* reserve = __atomic_exchange_n(&s_emergencyReserve, nullptr, __ATOMIC_ACQ_REL);
        if (reserve)
            free(reserve);
    }

    static void logOomAndExit(const char* prefix, size_t size)
    {
        struct mallinfo mi = mallinfo();
        char buf[192];
        if (size > 0)
            snprintf(buf, sizeof(buf), "[%s] malloc(%u) failed | used: %uKB | free: %uKB", prefix,
                static_cast<unsigned>(size), static_cast<unsigned>(mi.uordblks / 1024),
                static_cast<unsigned>(mi.fordblks / 1024));
        else
            snprintf(buf, sizeof(buf), "[%s] Heap: %uKB used / %uKB free", prefix,
                static_cast<unsigned>(mi.uordblks / 1024), static_cast<unsigned>(mi.fordblks / 1024));

        breadcrumb(buf);

        FILE* f = fopen("sdmc:/3ds/openmw/critical_oom.log", "ab");
        if (f)
        {
            fputs(buf, f);
            fputc('\n', f);
            fclose(f);
        }
        exit(1);
    }

    static void* ctrBtAlloc(size_t size)
    {
        void* ptr = malloc(size);
        if (!ptr)
        {
            releaseEmergencyReserve();
            ptr = malloc(size);
            if (!ptr)
                logOomAndExit("BT_OOM", size);
        }
        return ptr;
    }

    static void ctrBtFree(void* ptr)
    {
        free(ptr);
    }

    static void ctrNewHandler()
    {
        if (s_emergencyReserve)
        {
            releaseEmergencyReserve();
            breadcrumb("[OOM_RECOVERY] Released emergency reserve");
            return; // operator new retries
        }
        logOomAndExit("OOM", 0);
    }

    void debugLog(const char* msg)
    {
        if (!s_debugLogFile)
            s_debugLogFile = fopen("sdmc:/3ds/openmw/debug.log", "ab");
        if (s_debugLogFile)
        {
            fputs(msg, s_debugLogFile);
            fputc('\n', s_debugLogFile);
            fflush(s_debugLogFile);
        }
    }

    int getHeapUsedMB()
    {
        // Cache: mallinfo() walks the allocator free lists — expensive.
        static int s_cachedMB = 0;
        static u64 s_lastTime = 0;
        u64 now = osGetTime(); // milliseconds
        if (s_lastTime == 0 || (now - s_lastTime) > 1000)
        {
            struct mallinfo mi = mallinfo();
            s_cachedMB = mi.uordblks / (1024 * 1024);
            s_lastTime = now;
        }
        return s_cachedMB;
    }

    bool isMemoryPressure(int thresholdMB)
    {
        return getHeapUsedMB() > thresholdMB;
    }

    void replenishEmergencyReserve()
    {
        if (!s_emergencyReserve)
        {
            // Throttle: with the heap pinned at its ceiling this malloc can
            // never succeed, and every failed attempt walks the whole
            // fragmented arena looking for a 6MB block — measured at ~20 ms
            // per call when Scene::update retried it every frame.
            static u64 s_lastTryMs = 0;
            const u64 nowMs = osGetTime();
            if (s_lastTryMs != 0 && nowMs - s_lastTryMs < 2000)
                return;
            s_lastTryMs = nowMs;
            s_emergencyReserve = malloc(EMERGENCY_RESERVE_SIZE);
            if (s_emergencyReserve)
                breadcrumb("[MemWatchdog] Emergency reserve replenished");
        }
    }

    // ------------------------------------------------------------------
    // SELECT-held detection (raw HID, latched across boot)
    // ------------------------------------------------------------------
    static bool s_selectHeldLatched = false;

    void pollSelectHeld()
    {
        hidScanInput();
        u32 held = hidKeysHeld() | hidKeysDown();
        if (held & KEY_SELECT)
        {
            if (!s_selectHeldLatched)
                breadcrumb("SELECT held — forcing mod rescan");
            s_selectHeldLatched = true;
        }
    }

    bool isSelectHeld()
    {
        pollSelectHeld();
        return s_selectHeldLatched;
    }

    // ------------------------------------------------------------------
    // Filesystem helpers (POSIX — devkitARM devoptab handles sdmc:/)
    // ------------------------------------------------------------------
    static bool hasExtension(const char* name, const char* ext)
    {
        size_t nameLen = strlen(name);
        size_t extLen = strlen(ext);
        if (nameLen < extLen)
            return false;
        return strcasecmp(name + nameLen - extLen, ext) == 0;
    }

    static int readFileToBuffer(const char* path, char** outBuf)
    {
        *outBuf = nullptr;
        FILE* f = fopen(path, "rb");
        if (!f)
            return -1;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0 || size > 64 * 1024)
        {
            fclose(f);
            return -1;
        }
        *outBuf = static_cast<char*>(malloc(static_cast<size_t>(size) + 1));
        if (!*outBuf)
        {
            fclose(f);
            return -1;
        }
        fread(*outBuf, 1, static_cast<size_t>(size), f);
        (*outBuf)[size] = '\0';
        fclose(f);
        return static_cast<int>(size);
    }

    static bool cfgContainsEntry(const char* cfgBuf, const char* key, const char* value)
    {
        if (!cfgBuf)
            return false;
        size_t keyLen = strlen(key);
        size_t valLen = strlen(value);

        const char* p = cfgBuf;
        while (*p)
        {
            if (strncmp(p, key, keyLen) == 0 && p[keyLen] == '=')
            {
                const char* v = p + keyLen + 1;
                if (*v == '"')
                    ++v;
                if (strncmp(v, value, valLen) == 0
                    && (v[valLen] == '\n' || v[valLen] == '\r' || v[valLen] == '\0' || v[valLen] == '"'))
                    return true;
            }
            while (*p && *p != '\n')
                ++p;
            if (*p == '\n')
                ++p;
        }
        return false;
    }

    static const char* kUserCfgPath = "sdmc:/3ds/openmw/config/openmw.cfg";
    static const char* kDataFilesDir = "sdmc:/3ds/openmw/Data Files";

    static void pruneAutoDetectedSections()
    {
        char* buf = nullptr;
        int size = readFileToBuffer(kUserCfgPath, &buf);
        if (!buf || size <= 0)
        {
            free(buf);
            return;
        }

        static const char marker[] = "# Auto-detected";
        const int markerLen = sizeof(marker) - 1;
        int cutAt = -1;
        for (int i = 0; i + markerLen <= size; ++i)
        {
            if ((i == 0 || buf[i - 1] == '\n') && memcmp(buf + i, marker, markerLen) == 0)
            {
                cutAt = i;
                break;
            }
        }

        if (cutAt < 0)
        {
            free(buf);
            return;
        }

        while (cutAt > 0 && (buf[cutAt - 1] == '\n' || buf[cutAt - 1] == '\r'))
            --cutAt;

        FILE* f = fopen(kUserCfgPath, "wb");
        if (f)
        {
            if (cutAt > 0)
                fwrite(buf, 1, cutAt, f);
            fclose(f);
            breadcrumb("Pruned stale auto-detected entries from user cfg");
        }
        free(buf);
    }

    // Scan Data Files for content the base cfg doesn't already reference and
    // append it under an "# Auto-detected" section. Compact port of the Vita
    // autoDetectContent/autoDetectModDataDirs pair (no baked mods on 3DS yet).
    static void autoDetectContent()
    {
        char* cfgBuf = nullptr;
        readFileToBuffer(kUserCfgPath, &cfgBuf);

        std::vector<std::string> esmFiles, espFiles, bsaFiles;

        DIR* d = opendir(kDataFilesDir);
        if (!d)
        {
            free(cfgBuf);
            breadcrumb("Data Files dir not found — skipping content auto-detect");
            return;
        }
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr)
        {
            const char* name = ent->d_name;
            if (name[0] == '.')
                continue;
            if (hasExtension(name, ".esm") || hasExtension(name, ".omwgame"))
            {
                if (!cfgContainsEntry(cfgBuf, "content", name))
                    esmFiles.push_back(name);
            }
            else if (hasExtension(name, ".esp") || hasExtension(name, ".omwaddon")
                || hasExtension(name, ".omwscripts"))
            {
                if (!cfgContainsEntry(cfgBuf, "content", name))
                    espFiles.push_back(name);
            }
            else if (hasExtension(name, ".bsa"))
            {
                if (!cfgContainsEntry(cfgBuf, "fallback-archive", name))
                    bsaFiles.push_back(name);
            }
        }
        closedir(d);
        free(cfgBuf);

        if (esmFiles.empty() && espFiles.empty() && bsaFiles.empty())
            return;

        std::sort(esmFiles.begin(), esmFiles.end());
        std::sort(espFiles.begin(), espFiles.end());
        std::sort(bsaFiles.begin(), bsaFiles.end());

        FILE* f = fopen(kUserCfgPath, "ab");
        if (!f)
            return;
        fputs("\n# Auto-detected (do not edit below this line)\n", f);
        for (const auto& name : bsaFiles)
            fprintf(f, "fallback-archive=%s\n", name.c_str());
        for (const auto& name : esmFiles)
            fprintf(f, "content=%s\n", name.c_str());
        for (const auto& name : espFiles)
            fprintf(f, "content=%s\n", name.c_str());
        fclose(f);

        char buf[96];
        snprintf(buf, sizeof(buf), "Auto-detected %u esm, %u esp, %u bsa",
            (unsigned)esmFiles.size(), (unsigned)espFiles.size(), (unsigned)bsaFiles.size());
        breadcrumb(buf);
    }

    static void ensureDataDirectories()
    {
        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/openmw", 0777);
        mkdir("sdmc:/3ds/openmw/config", 0777);
        mkdir("sdmc:/3ds/openmw/data", 0777);
        mkdir("sdmc:/3ds/openmw/saves", 0777);
        mkdir("sdmc:/3ds/openmw/cache", 0777);
        mkdir("sdmc:/3ds/openmw/prebuilt", 0777); // textures.ctp, sounds.ctp from apps/ctrprep
        mkdir("sdmc:/3ds/openmw/screenshots", 0777);
        mkdir("sdmc:/3ds/openmw/mods", 0777);
        mkdir("sdmc:/3ds/openmw/Data Files", 0777);

        // Create default user openmw.cfg if it doesn't exist. The bundled
        // romfs:/openmw.cfg has paths but no content= lines.
        FILE* f = fopen(kUserCfgPath, "rb");
        if (f)
        {
            fclose(f);
        }
        else
        {
            f = fopen(kUserCfgPath, "wb");
            if (f)
            {
                static const char defaultCfg[] = "# Auto-generated OpenMW 3DS user config\n"
                                                 "# Place Morrowind data files in sdmc:/3ds/openmw/Data Files/\n"
                                                 "\n";
                fwrite(defaultCfg, 1, sizeof(defaultCfg) - 1, f);
                fclose(f);
                breadcrumb("Created default user openmw.cfg");
            }
        }

        // No scan cookie yet on 3DS — a single directory scan per boot is
        // acceptable; revisit if boot profiling shows otherwise.
        pruneAutoDetectedSections();
        autoDetectContent();
    }

    void initialize()
    {
        // Filesystem first — breadcrumbs need sdmc:/
        ensureDataDirectories();

        // Rotate logs
        remove("sdmc:/3ds/openmw/boot.log.prev");
        rename("sdmc:/3ds/openmw/boot.log", "sdmc:/3ds/openmw/boot.log.prev");
        remove("sdmc:/3ds/openmw/debug.log.prev");
        rename("sdmc:/3ds/openmw/debug.log", "sdmc:/3ds/openmw/debug.log.prev");

        breadcrumb("BOOT: Ctr initialize() start");
        breadcrumb("BOOT: build " OPENMW_CTR_BUILD_STAMP);
        ctr_diag_open();

        setenv("TMPDIR", "sdmc:/3ds/openmw/cache", 1);
        setenv("HOME", "sdmc:/3ds/openmw", 1);
        setenv("OSG_NOTIFY_LEVEL", "ALWAYS", 0);

        // New 3DS: 804MHz + L2 cache
        osSetSpeedupEnable(true);

        // Reserve a syscore (core 1) share so ONE app thread may live there —
        // the OpenAL mixer (see ctrSetThreadSpawnHint in engine.cpp). Without
        // this, threadCreate(core=1) is denied and the wrapper falls back to
        // core 0. 30% is the customary retail value.
        {
            Result cpuRc = APT_SetAppCpuTimeLimit(30);
            if (R_FAILED(cpuRc))
                breadcrumb("BOOT: WARNING - APT_SetAppCpuTimeLimit(30) failed");
        }

        // romfs carries the engine resources (shaders, mygui layouts, vfs)
        Result rc = romfsInit();
        if (R_FAILED(rc))
            breadcrumb("BOOT: WARNING - romfsInit failed");

        // HID for SELECT latch
        hidInit();
        pollSelectHeld();

        std::set_terminate([]() {
            // Re-entrancy guard: if the handler itself faults (heap gone,
            // broken unwind state), the second entry must not touch the
            // exception machinery again — that's how the log ends up with
            // hundreds of "[TERMINATE]" lines and nothing useful.
            static volatile int s_inTerminate = 0;
            if (s_inTerminate)
            {
                svcBreak(USERBREAK_PANIC);
                for (;;)
                    svcSleepThread(1000000000ULL);
            }
            s_inTerminate = 1;

            breadcrumb("[TERMINATE] std::terminate called");

            // Log the exception TYPE without rethrowing — this works even
            // when the unwinder is unable to actually deliver the exception.
            if (const std::type_info* t = abi::__cxa_current_exception_type())
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "[TERMINATE] exception type: %s", t->name());
                breadcrumb(buf);
            }

            std::exception_ptr ep = std::current_exception();
            if (ep)
            {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (const std::exception& e)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "[TERMINATE] exception: %s", e.what());
                    breadcrumb(buf);
                }
                catch (...)
                {
                    breadcrumb("[TERMINATE] non-std exception");
                }
            }
            else
                breadcrumb("[TERMINATE] no active exception (noexcept violation?)");

            breadcrumb("[TERMINATE] halting");
            svcBreak(USERBREAK_PANIC);
            for (;;)
                svcSleepThread(1000000000ULL);
        });

        // C++ EH self-test: if the unwinder is broken in this executable
        // (bad exidx after packaging, etc.), EVERY throw becomes terminate —
        // catch that here, loudly, instead of dying somewhere in boost.
        try
        {
            throw std::runtime_error("eh-selftest");
        }
        catch (const std::exception& e)
        {
            breadcrumb("BOOT: C++ EH self-test OK");
        }
        catch (...)
        {
            breadcrumb("BOOT: C++ EH self-test caught UNKNOWN type (suspicious)");
        }

        std::set_new_handler(ctrNewHandler);
        btAlignedAllocSetCustom(ctrBtAlloc, ctrBtFree);

        s_emergencyReserve = malloc(EMERGENCY_RESERVE_SIZE);
        breadcrumb(s_emergencyReserve ? "BOOT: emergency reserve allocated"
                                      : "BOOT: WARNING - emergency reserve allocation failed");

        gfxInit(GSP_BGR8_OES, GSP_RGB565_OES, false);
        gfxSet3D(false);
        consoleInit(GFX_BOTTOM, nullptr);
        printf("OpenMW 3DS boot\n");
        if (!CtrGpu::init())
            breadcrumb("BOOT: ERROR - citro3d initialisation failed");
        else
            breadcrumb("BOOT: citro3d initialized");

        char initBuf[128];
        snprintf(initBuf, sizeof(initBuf), "CTR Platform Initialized: linear=%uMB free, heap=%uKB free",
            (unsigned)(linearSpaceFree() / (1024 * 1024)), (unsigned)(mallinfo().fordblks / 1024));
        debugLog(initBuf);
        breadcrumb(initBuf);
        // Hang watchdog: once the main loop is running (g_ctrFrameNo > 0),
        // a frame counter frozen across a 5 s window means the main thread
        // is wedged — log the last phase beacon for the post-mortem.
        // threadCreate at prio 0x18 (not std::thread): the main thread runs
        // at 0x30 on the same core, and Horizon scheduling is strictly
        // priority-preemptive — an equal-priority watchdog would never get
        // CPU while the main loop spins in a wedged malloc walk.
        threadCreate(
            [](void*)
            {
                int lastSeen = -1;
                int stuckTicks = 0;
                while (true)
                {
                    svcSleepThread(5000000000LL);
                    const int cur = g_ctrFrameNo;
                    if (cur > 0 && cur == lastSeen)
                    {
                        ++stuckTicks;
                        char hb[200];
                        snprintf(hb, sizeof(hb), "[HANG] frame=%d stuck=%ds beacon=%s in_malloc=%p in_free=%p\n",
                            cur, stuckTicks * 5, (const char*)g_ctrBeacon, g_ctrMallocCaller, g_ctrFreeCaller);
                        ctr_diag_write(hb);
                    }
                    else
                        stuckTicks = 0;
                    lastSeen = cur;
                }
            },
            nullptr, 16 * 1024, 0x18, -2, true);

        breadcrumb("BOOT: Ctr initialize() done");
    }

    void applySettingsOverrides()
    {
        debugLog("Applying 3DS platform defaults...");

        // --- Video: top screen is 400x240; the bundled settings.cfg carries
        // the resolution. Antialiasing and framerate cap stay forced.
        Settings::video().mAntialiasing.set(0);
        Settings::video().mFramerateLimit.set(30.0f);

        // --- Shaders: FFP only (NovaGL is a fixed-function GLES1.1 layer) ---
        Settings::shaders().mForcePerPixelLighting.set(false);
        Settings::shaders().mAutoUseObjectNormalMaps.set(false);
        Settings::shaders().mAutoUseObjectSpecularMaps.set(false);
        Settings::shaders().mAutoUseTerrainNormalMaps.set(false);
        Settings::shaders().mAutoUseTerrainSpecularMaps.set(false);
        Settings::shaders().mSoftParticles.set(false);
        Settings::shaders().mWeatherParticleOcclusion.set(false);
        Settings::shaders().mAntialiasAlphaTest.set(false);
        Settings::shaders().mAdjustCoverageForAlphaTest.set(false);

        // --- Shadows / post-processing: off ---
        Settings::shadows().mEnableShadows.set(false);
        Settings::postProcessing().mEnabled.set(false);

        // --- Camera ---
        Settings::camera().mReverseZ.set(false);

        // --- Water: minimal ---
        Settings::water().mShader.set(false);
        Settings::water().mRefraction.set(false);
        Settings::water().mReflectionDetail.set(0);
        Settings::water().mRainRippleDetail.set(0);

        // --- Fog: simple ---
        Settings::fog().mUseDistantFog.set(false);
        Settings::fog().mRadialFog.set(false);
        Settings::fog().mExponentialFog.set(false);
        Settings::fog().mSkyBlending.set(false);

        Settings::physics().mAsyncNumThreads.set(0);

        // --- Cells ---
        // Preloader off: its per-frame door/position scans cost measurable
        // main-thread time on the ARM11 and interiors gain nothing from it.
        // Revisit for exteriors (preload exterior grid) once 30 FPS holds.
        Settings::cells().mPreloadEnabled.set(false);
        Settings::cells().mPreloadNumThreads.set(1);
        Settings::cells().mPreloadExteriorGrid.set(true);
        Settings::cells().mPreloadFastTravel.set(false);
        Settings::cells().mPreloadDoors.set(false);
        Settings::cells().mPreloadInstances.set(false);

        // --- Sound: cap the decoded-PCM buffer pool. Default is 15 MB and
        // the pool only evicts once the cap is crossed — with the heap
        // ceiling at ~122 MB and the world resident at ~115 MB, letting
        // sounds accumulate OOM'd the chargen ship ~145 s in (a ~1.8 MB
        // voice/SFX decode was the straw; heap had crept 115→124 MB).
        Settings::sound().mBufferCacheMin.set(2);
        Settings::sound().mBufferCacheMax.set(3);

        // --- Navigator: off ---
        Settings::navigator().mEnable.set(false);

        // --- Lua: main thread, tight memory ---
        // 12MB cap (was 16): measured Lua use at LuaInit is ~9MB, so 12 keeps
        // headroom while returning 4MB to the arena for the retail 124MB fit.
        Settings::lua().mLuaNumThreads.set(0);
        Settings::lua().mMemoryLimit.set(static_cast<std::uint64_t>(12 * 1024 * 1024));
        Settings::lua().mLuaProfiler.set(false);

        // --- GUI: 400x240 needs a small logical scale ---
        Settings::gui().mScalingFactor.set(0.5f);
        Settings::gui().mControllerMenus.set(true);
        // Subtitles ON: the chargen intro is driven by voiced Say lines and
        // real hardware needs a dumped dspfirm for audible voice — without
        // subtitles the intro looks like "nothing happens".
        Settings::gui().mSubtitles.set(true);

        // --- Groundcover / stereo: off ---
        Settings::groundcover().mEnabled.set(false);
        Settings::stereo().mStereoEnabled.set(false);

        {
            auto& w = Settings::windows();
            w.mMapPin.set(true);
            w.mMapHidden.set(false);
            w.mStatsX.set(0.1f);
            w.mStatsY.set(0.f);
            w.mStatsW.set(0.8f);
            w.mStatsH.set(1.f);
            w.mStatsMaximizedX.set(0.1f);
            w.mStatsMaximizedY.set(0.f);
            w.mStatsMaximizedW.set(0.8f);
            w.mStatsMaximizedH.set(1.f);
            w.mSpellsX.set(0.1f);
            w.mSpellsY.set(0.f);
            w.mSpellsW.set(0.8f);
            w.mSpellsH.set(1.f);
            w.mSpellsMaximizedX.set(0.1f);
            w.mSpellsMaximizedY.set(0.f);
            w.mSpellsMaximizedW.set(0.8f);
            w.mSpellsMaximizedH.set(1.f);
            w.mMapX.set(0.1f);
            w.mMapY.set(0.f);
            w.mMapW.set(0.8f);
            w.mMapH.set(1.f);
            w.mMapMaximizedX.set(0.1f);
            w.mMapMaximizedY.set(0.f);
            w.mMapMaximizedW.set(0.8f);
            w.mMapMaximizedH.set(1.f);
            w.mInventoryX.set(0.1f);
            w.mInventoryY.set(0.f);
            w.mInventoryW.set(0.8f);
            w.mInventoryH.set(1.f);
            w.mInventoryMaximizedX.set(0.1f);
            w.mInventoryMaximizedY.set(0.f);
            w.mInventoryMaximizedW.set(0.8f);
            w.mInventoryMaximizedH.set(1.f);
            w.mInventoryContainerX.set(0.1f);
            w.mInventoryContainerY.set(0.5f);
            w.mInventoryContainerW.set(0.8f);
            w.mInventoryContainerH.set(0.5f);
            w.mInventoryContainerMaximizedX.set(0.1f);
            w.mInventoryContainerMaximizedY.set(0.f);
            w.mInventoryContainerMaximizedW.set(0.8f);
            w.mInventoryContainerMaximizedH.set(1.f);
            w.mInventoryBarterX.set(0.1f);
            w.mInventoryBarterY.set(0.5f);
            w.mInventoryBarterW.set(0.8f);
            w.mInventoryBarterH.set(0.5f);
            w.mInventoryBarterMaximizedX.set(0.1f);
            w.mInventoryBarterMaximizedY.set(0.f);
            w.mInventoryBarterMaximizedW.set(0.8f);
            w.mInventoryBarterMaximizedH.set(1.f);
            w.mInventoryCompanionX.set(0.1f);
            w.mInventoryCompanionY.set(0.5f);
            w.mInventoryCompanionW.set(0.8f);
            w.mInventoryCompanionH.set(0.5f);
            w.mInventoryCompanionMaximizedX.set(0.1f);
            w.mInventoryCompanionMaximizedY.set(0.f);
            w.mInventoryCompanionMaximizedW.set(0.8f);
            w.mInventoryCompanionMaximizedH.set(1.f);
            w.mDialogueX.set(0.1f);
            w.mDialogueY.set(0.f);
            w.mDialogueW.set(0.8f);
            w.mDialogueH.set(1.f);
            w.mDialogueMaximizedX.set(0.1f);
            w.mDialogueMaximizedY.set(0.f);
            w.mDialogueMaximizedW.set(0.8f);
            w.mDialogueMaximizedH.set(1.f);
            w.mAlchemyX.set(0.1f);
            w.mAlchemyY.set(0.f);
            w.mAlchemyW.set(0.8f);
            w.mAlchemyH.set(1.f);
            w.mAlchemyMaximizedX.set(0.1f);
            w.mAlchemyMaximizedY.set(0.f);
            w.mAlchemyMaximizedW.set(0.8f);
            w.mAlchemyMaximizedH.set(1.f);
            w.mConsoleX.set(0.1f);
            w.mConsoleY.set(0.f);
            w.mConsoleW.set(0.8f);
            w.mConsoleH.set(1.f);
            w.mConsoleMaximizedX.set(0.1f);
            w.mConsoleMaximizedY.set(0.f);
            w.mConsoleMaximizedW.set(0.8f);
            w.mConsoleMaximizedH.set(1.f);
            w.mContainerX.set(0.1f);
            w.mContainerY.set(0.f);
            w.mContainerW.set(0.8f);
            w.mContainerH.set(0.5f);
            w.mContainerMaximizedX.set(0.1f);
            w.mContainerMaximizedY.set(0.f);
            w.mContainerMaximizedW.set(0.8f);
            w.mContainerMaximizedH.set(1.f);
            w.mBarterX.set(0.1f);
            w.mBarterY.set(0.f);
            w.mBarterW.set(0.8f);
            w.mBarterH.set(0.5f);
            w.mBarterMaximizedX.set(0.1f);
            w.mBarterMaximizedY.set(0.f);
            w.mBarterMaximizedW.set(0.8f);
            w.mBarterMaximizedH.set(1.f);
            w.mCompanionX.set(0.1f);
            w.mCompanionY.set(0.f);
            w.mCompanionW.set(0.8f);
            w.mCompanionH.set(0.5f);
            w.mCompanionMaximizedX.set(0.1f);
            w.mCompanionMaximizedY.set(0.f);
            w.mCompanionMaximizedW.set(0.8f);
            w.mCompanionMaximizedH.set(1.f);
            w.mSettingsX.set(0.1f);
            w.mSettingsY.set(0.f);
            w.mSettingsW.set(0.8f);
            w.mSettingsH.set(1.f);
            w.mSettingsMaximizedX.set(0.1f);
            w.mSettingsMaximizedY.set(0.f);
            w.mSettingsMaximizedW.set(0.8f);
            w.mSettingsMaximizedH.set(1.f);
            w.mPostprocessorX.set(0.1f);
            w.mPostprocessorY.set(0.f);
            w.mPostprocessorW.set(0.8f);
            w.mPostprocessorH.set(1.f);
            w.mPostprocessorMaximizedX.set(0.1f);
            w.mPostprocessorMaximizedY.set(0.f);
            w.mPostprocessorMaximizedW.set(0.8f);
            w.mPostprocessorMaximizedH.set(1.f);
            w.mDebugX.set(0.1f);
            w.mDebugY.set(0.f);
            w.mDebugW.set(0.8f);
            w.mDebugH.set(1.f);
            w.mDebugMaximizedX.set(0.1f);
            w.mDebugMaximizedY.set(0.f);
            w.mDebugMaximizedW.set(0.8f);
            w.mDebugMaximizedH.set(1.f);
        }

        debugLog("3DS platform defaults applied.");
        logMemoryStatus("Post-defaults");
    }

} // namespace Vita

#endif // __3DS__
