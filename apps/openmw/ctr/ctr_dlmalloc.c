/*
 * 3DS: replace newlib's ancient malloc (dlmalloc 2.6-era mallocr.c) with
 * dlmalloc 2.8.6 over the same sbrk region.
 *
 * Why: OpenMW's ESM load churns the heap with millions of mixed-size
 * allocations. newlib's allocator shreds the ~125MB arena into confetti —
 * observed: a 2KB allocation failing with 7.4MB "free". dlmalloc 2.8.6's
 * tree bins + designated victim keep large contiguous runs usable.
 *
 * Every newlib entry point (including the reentrant _*_r family used
 * internally by libc/libstdc++) must be overridden — mixing two heaps over
 * one sbrk would corrupt both.
 */
#ifdef __3DS__

#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <unistd.h>

/* ---- dlmalloc configuration ---- */
#define USE_DL_PREFIX 1
#define USE_LOCKS 1
/* CRITICAL: no spin locks. dlmalloc's default custom spinlock livelocks on
 * the 3DS scheduler — the main thread spins at priority while the lock
 * holder (a worker inside dlmallinfo) never gets a timeslice. Observed as
 * non-deterministic boot hangs with main in spin_acquire_lock. pthread
 * mutexes block properly via kernel arbitration. */
#define USE_SPIN_LOCKS 0
#define HAVE_MMAP 0
#define LACKS_SYS_MMAN_H 1
#define HAVE_MORECORE 1
#define MORECORE sbrk
#define MORECORE_CONTIGUOUS 1
#define NO_MALLOC_STATS 1
#define DEFAULT_GRANULARITY (128U * 1024U)
/* Each in-use chunk ends with a check word (stored where the next chunk's
 * prev_foot is otherwise unused, so it costs no memory). Writing past the end
 * of a block clobbers it, and freeing that block reports the overrun. */
#define FOOTERS 1
static void ctr_heap_error(const char* what, void* chunk, int fatal);
#define USAGE_ERROR_ACTION(m, p) ctr_heap_error("bad free or overrun", (void*)(p), 0)
#define CORRUPTION_ERROR_ACTION(m) ctr_heap_error("corrupted free lists", NULL, 1)
/* newlib provides these headers fine; dlmalloc autodetects the rest */

#include "../../../extern/dlmalloc/dlmalloc.c"

/* ---- diagnostics that work while the heap is wedged ----
 * The log descriptor is opened at boot: opening a file allocates, and a
 * report from inside malloc (which holds the heap lock) must not. The caller
 * markers say which call is inside malloc/free, for the hang watchdog. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

extern const char* volatile g_ctrBeacon;

static int s_diagFd = -1;
void* volatile g_ctrMallocCaller = NULL;
void* volatile g_ctrFreeCaller = NULL;

void ctr_diag_open(void)
{
    if (s_diagFd < 0)
        /* Its own file: this descriptor's view of the size goes stale while boot.log grows through others. */
        s_diagFd = open("sdmc:/3ds/openmw/diag.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

void ctr_diag_write(const char* text)
{
    if (s_diagFd < 0)
        return;
    size_t size = 0;
    while (text[size] != '\0')
        ++size;
    write(s_diagFd, text, size);
}

static void ctr_heap_error(const char* what, void* chunk, int fatal)
{
    static int reports = 0;
    if (reports < 32)
    {
        ++reports;
        char line[200];
        snprintf(line, sizeof(line), "[HEAP] %s chunk=%p free_ra=%p malloc_ra=%p beacon=%s\n", what, chunk,
            g_ctrFreeCaller, g_ctrMallocCaller, (const char*)g_ctrBeacon);
        ctr_diag_write(line);
    }
    if (fatal)
        abort();
}

/* ---- bad free guard ----
 * A pointer outside the malloc heap or off the 8-byte chunk alignment can't
 * have come from malloc: freeing it data-aborts on hardware (dlfree reads the
 * chunk header in front of it). Log the caller (addr2line-able against the
 * matching openmw.elf) and leak instead. */

#include <stdint.h>
#include <stdio.h>

extern unsigned int __ctru_heap;
extern unsigned int __ctru_heap_size;
extern const char* volatile g_ctrBeacon;

void ctr_checked_free(void* ptr, void* caller)
{
    const uintptr_t address = (uintptr_t)ptr;
    if (ptr == NULL)
        return;
    if ((address & 7) == 0 && address >= __ctru_heap + 8 && address < __ctru_heap + __ctru_heap_size)
    {
        g_ctrFreeCaller = caller;
        dlfree(ptr);
        g_ctrFreeCaller = NULL;
        return;
    }
    static int reports = 0;
    if (reports < 32)
    {
        ++reports;
        FILE* f = fopen("sdmc:/3ds/openmw/boot.log", "ab");
        if (f)
        {
            fprintf(f, "[BAD_FREE] ptr=%p ra=%p beacon=%s\n", ptr, caller, (const char*)g_ctrBeacon);
            fclose(f);
        }
    }
}

/* ---- newlib API overrides ---- */

void* ctr_malloc_from(size_t size, void* caller)
{
    g_ctrMallocCaller = caller;
    void* ptr = dlmalloc(size);
    g_ctrMallocCaller = NULL;
    return ptr;
}

void* malloc(size_t size)
{
    return ctr_malloc_from(size, __builtin_return_address(0));
}

void free(void* ptr)
{
    ctr_checked_free(ptr, __builtin_return_address(0));
}

void* calloc(size_t nmemb, size_t size)
{
    return dlcalloc(nmemb, size);
}

void* realloc(void* ptr, size_t size)
{
    return dlrealloc(ptr, size);
}

void* memalign(size_t alignment, size_t size)
{
    return dlmemalign(alignment, size);
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    return dlposix_memalign(memptr, alignment, size);
}

void* valloc(size_t size)
{
    return dlvalloc(size);
}

void* pvalloc(size_t size)
{
    return dlpvalloc(size);
}

size_t malloc_usable_size(void* ptr)
{
    return dlmalloc_usable_size(ptr);
}

int malloc_trim(size_t pad)
{
    return dlmalloc_trim(pad);
}

/* struct mallinfo layouts match (SVID, size_t fields) — forward directly.
 * dlmalloc.c already defined `struct mallinfo` for us (no <malloc.h> here!). */
struct mallinfo mallinfo(void)
{
    return dlmallinfo();
}

int mallopt(int param, int value)
{
    return dlmallopt(param, value);
}

/* ---- reentrant family (newlib internals call these directly) ---- */

void* _malloc_r(struct _reent* r, size_t size)
{
    (void)r;
    return ctr_malloc_from(size, __builtin_return_address(0));
}

void _free_r(struct _reent* r, void* ptr)
{
    (void)r;
    ctr_checked_free(ptr, __builtin_return_address(0));
}

void* _calloc_r(struct _reent* r, size_t nmemb, size_t size)
{
    (void)r;
    return dlcalloc(nmemb, size);
}

void* _realloc_r(struct _reent* r, void* ptr, size_t size)
{
    (void)r;
    return dlrealloc(ptr, size);
}

void* _memalign_r(struct _reent* r, size_t alignment, size_t size)
{
    (void)r;
    return dlmemalign(alignment, size);
}

struct mallinfo _mallinfo_r(struct _reent* r)
{
    (void)r;
    return dlmallinfo();
}

size_t _malloc_usable_size_r(struct _reent* r, void* ptr)
{
    (void)r;
    return dlmalloc_usable_size(ptr);
}

int _malloc_trim_r(struct _reent* r, size_t pad)
{
    (void)r;
    return dlmalloc_trim(pad);
}

#endif /* __3DS__ */
