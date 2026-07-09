/*
 * dlfcn.h shim for Nintendo 3DS (OpenMW port)
 *
 * devkitARM newlib has no dynamic loader. OSG (GLExtensions, DynamicLibrary)
 * includes <dlfcn.h> and calls dlopen/dlsym as a *fallback* — the primary GL
 * proc lookup is routed through novaglGetProcAddress by the ctr-compat patch.
 * These inline no-ops make the fallback compile and always fail cleanly.
 */
#ifndef _OPENMW_CTR_DLFCN_H_
#define _OPENMW_CTR_DLFCN_H_

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 4
#define RTLD_LOCAL 8
#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT ((void*)-1)

#ifdef __cplusplus
extern "C" {
#endif

static inline void* dlopen(const char* file, int mode)
{
    (void)file;
    (void)mode;
    return 0;
}

static inline void* dlsym(void* handle, const char* name)
{
    (void)handle;
    (void)name;
    return 0;
}

static inline int dlclose(void* handle)
{
    (void)handle;
    return 0;
}

static inline const char* dlerror(void)
{
    return "dynamic loading is not supported on 3DS";
}

#ifdef __cplusplus
}
#endif

#endif /* _OPENMW_CTR_DLFCN_H_ */
