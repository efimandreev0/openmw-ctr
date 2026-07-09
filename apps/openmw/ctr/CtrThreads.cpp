// Thread placement for the 3DS port.
//
// devkitARM routes every std::thread / pthread_create through libctru's
// __syscall_thread_create, which hardcodes threadCreate(..., prio=0x3F,
// core=0, detached=false) — so out of the box the physics worker, the cell
// preloader and the OpenAL mixer all fight the main thread for core 0 at the
// LOWEST userland priority. The New3DS has two more usable cores sitting
// idle: #1 (syscore — one app thread once APT_SetAppCpuTimeLimit reserves a
// share) and #2 (app-exclusive with CanAccessCore2 in the RSF, which we set).
//
// openmw is linked with -Wl,--wrap=__syscall_thread_create (see
// apps/openmw/CMakeLists.txt), so the wrapper below replaces the glue. The
// default stays byte-compatible with libctru (core 0, prio 0x3F, 32KB
// stack); call ctrSetThreadSpawnHint() before creating a thread to place it
// deliberately, and ctrClearThreadSpawnHint() after. Hints are sticky so one
// hint can cover a burst of workers (e.g. a WorkQueue's whole pool).
//
// If the hinted core is denied (Old3DS, missing APT share, exhausted syscore
// slot) the wrapper retries on core 0 with the same priority/stack — thread
// creation itself must never start failing just because of a placement wish.

#ifdef __3DS__

#include <3ds.h>

#include <cstddef>
#include <cstdio>

namespace
{
    // One-shot-ish (sticky until cleared) spawn hint. All engine threads are
    // created from the main thread, so plain globals are race-free here.
    int s_hintCore = -100; // -100 = no hint -> core 0 (stock glue behaviour)
    int s_hintPrio = -1; // -1 = no hint -> 0x3F (stock glue behaviour)
    size_t s_hintMinStack = 0; // raise-only floor for the stack size
}

extern "C" void ctrSetThreadSpawnHint(int core, int prio, size_t minStack)
{
    s_hintCore = core;
    s_hintPrio = prio;
    s_hintMinStack = minStack;
}

extern "C" void ctrClearThreadSpawnHint(void)
{
    s_hintCore = -100;
    s_hintPrio = -1;
    s_hintMinStack = 0;
}

extern "C" int __wrap___syscall_thread_create(
    Thread* thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size)
{
    if (stack_addr != nullptr)
        return 22; // EINVAL — matches the libctru glue: no user-supplied stacks

    if (stack_size == 0)
        stack_size = 32 * 1024; // stock glue default
    if (stack_size < s_hintMinStack)
        stack_size = s_hintMinStack;

    int core = (s_hintCore != -100) ? s_hintCore : 0;
    int prio = (s_hintPrio > 0) ? s_hintPrio : 0x3F;

    Thread t = threadCreate((ThreadFunc) (void*) func, arg, stack_size, prio, core, false);
    if (t == nullptr && core != 0)
    {
        printf("[CtrThreads] core %d denied for prio=0x%02X thread, falling back to core 0\n", core, prio);
        t = threadCreate((ThreadFunc) (void*) func, arg, stack_size, prio, 0, false);
    }
    if (t == nullptr)
        return 12; // ENOMEM

    *thread = t;
    return 0;
}

#endif // __3DS__
