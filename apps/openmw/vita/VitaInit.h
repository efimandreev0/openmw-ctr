#ifndef OPENMW_VITA_INIT_H
#define OPENMW_VITA_INIT_H

#if defined(__vita__) || defined(__3DS__)

#include <cstddef>

// C-linkage breadcrumbs (also callable from C++ via Vita::breadcrumb wrapper)
extern "C" {
void vitaBreadcrumb(const char* msg);
void vitaTimedBreadcrumb(const char* msg);
void vitaMemBreadcrumb(const char* msg);
}

namespace Vita
{
    // Must be called before anything else. Sets up clocks, OOM handler, logging.
    void initialize();

    // Apply Vita-specific settings overrides (video, shaders, memory, etc.)
    void applySettingsOverrides();

    // Write a breadcrumb message to ux0:data/openmw/boot.log (crash-safe via sceIo)
    void breadcrumb(const char* msg);

    // First-frames stage tracer gate: true for the first ~32 calls total.
    // Uses its own counter — the osgViewer frame number is useless for
    // "first gameplay frames" because boot loading screens pump it.
    inline bool firstFramesTrace()
    {
        static int s_calls = 0;
        return s_calls++ < 32;
    }

    // Write to ux0:data/openmw/debug.log (persistent fd, fast)
    void debugLog(const char* msg);

    // Log current memory status to boot.log and engine log
    void logMemoryStatus(const char* label);

    // Get free heap bytes
    size_t getFreeUserMemory();

    // Get heap used in MB (fast — just mallinfo)
    int getHeapUsedMB();

    // Returns true if heap usage exceeds the given MB threshold
    bool isMemoryPressure(int thresholdMB);

    // Replenish emergency reserve after OOM recovery.
    void replenishEmergencyReserve();

    // Check if SELECT is held right now (raw SCE ctrl, no SDL needed).
    bool isSelectHeld();
}

// Convenience macro that compiles to nothing on non-Vita
#define VITA_BOOT_LOG(msg) vitaBreadcrumb(msg)

#else

#define VITA_BOOT_LOG(msg) ((void)0)

#endif // __vita__

#endif // OPENMW_VITA_INIT_H
