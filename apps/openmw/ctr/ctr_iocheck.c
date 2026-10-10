/*
 * 3DS: reads through a corrupted file handle.
 *
 * newlib looks a descriptor up in its handle table and calls the read
 * function of the handle's device. A handle whose device index is not a
 * registered device dereferences a null devoptab entry: a data abort on
 * hardware (Citra logs "unmapped Read32 @ 0x14" in _read_r). openmw is
 * linked with -Wl,--wrap=_read_r (cmake/CtrBuild.cmake), so such reads fail
 * with EBADF instead and the caller is logged to boot.log.
 */
#ifdef __3DS__

#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <stdio.h>
#include <sys/iosupport.h>
#include <unistd.h>

extern const char* volatile g_ctrBeacon;

_ssize_t __real__read_r(struct _reent* r, int fd, void* ptr, size_t len);

static void ctr_log_bad_fd(const char* what, int fd, void* caller)
{
    static int reports = 0;
    if (reports >= 32)
        return;
    ++reports;
    char line[160];
    const int size = snprintf(line, sizeof(line), "[BAD_FD] %s fd=%d ra=%p beacon=%s\n", what, fd, caller,
        (const char*)g_ctrBeacon);
    /* Plain descriptors: no stdio locks are taken while a FILE may be mid-read. */
    const int log = open("sdmc:/3ds/openmw/boot.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log < 0)
        return;
    if (size > 0)
        write(log, line, (size_t)size < sizeof(line) ? (size_t)size : sizeof(line) - 1);
    close(log);
}

/* Whether a read through the descriptor reaches a registered device. Descriptors without a handle are left to
 * newlib, which fails them with EBADF. */
int ctr_check_fd(int fd, const char* what, void* caller)
{
    if (fd < 0)
        return 1;
    const __handle* handle = __get_handle(fd);
    if (handle == NULL || (handle->device < STD_MAX && devoptab_list[handle->device] != NULL))
        return 1;
    ctr_log_bad_fd(what, fd, caller);
    return 0;
}

_ssize_t __wrap__read_r(struct _reent* r, int fd, void* ptr, size_t len)
{
    if (!ctr_check_fd(fd, "read", __builtin_return_address(0)))
    {
        r->_errno = EBADF;
        return -1;
    }
    return __real__read_r(r, fd, ptr, len);
}

#endif /* __3DS__ */
