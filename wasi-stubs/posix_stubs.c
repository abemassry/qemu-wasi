/*
 * POSIX function stubs for QEMU wasm32-wasi build.
 *
 * These provide stub implementations for POSIX functions that
 * WASI does not support but QEMU references.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * sigsetjmp / siglongjmp: Now handled as macros in osdep.h (same as
 * Windows in os-win32.h). This is required for wasm sjlj correctness —
 * function wrappers break the try_table/throw ID matching mechanism.
 * See osdep.h for details.
 */

/* getpid: WASI has no process identity */
pid_t getpid(void)
{
    return 1;
}

/* shm_open / shm_unlink: WASI has no shared memory objects */
int shm_open(const char *name, int oflag, int mode)
{
    errno = ENOSYS;
    return -1;
}

int shm_unlink(const char *name)
{
    errno = ENOSYS;
    return -1;
}

/* msync: no-op (WASI mmap is emulated) */
int msync(void *addr, size_t length, int flags)
{
    return 0;
}

/* pthread_atfork: WASI is single-process, no fork */
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void))
{
    return 0;
}
