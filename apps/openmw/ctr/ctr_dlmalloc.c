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
/* newlib provides these headers fine; dlmalloc autodetects the rest */

#include "../../../extern/dlmalloc/dlmalloc.c"

/* ---- newlib API overrides ---- */

void* malloc(size_t size)
{
    return dlmalloc(size);
}

void free(void* ptr)
{
    dlfree(ptr);
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
    return dlmalloc(size);
}

void _free_r(struct _reent* r, void* ptr)
{
    (void)r;
    dlfree(ptr);
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
