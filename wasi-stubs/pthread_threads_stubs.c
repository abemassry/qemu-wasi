/*
 * The few pthread functions QEMU uses that wasi-libc's threads build
 * (wasm32-wasip1-threads) lacks.  Everything else is real; the
 * single-threaded build uses pthread_stubs.c instead.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <pthread.h>
#include <stddef.h>

/* There are no signals: every mask is the empty one. */
int pthread_sigmask(int how, const void *restrict set, void *restrict oldset)
{
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

/* Thread names are only a debugging aid. */
int pthread_setname_np(pthread_t thread, const char *name)
{
    (void)thread;
    (void)name;
    return 0;
}

/* The host schedules the threads. */
int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize,
                           const void *cpuset)
{
    (void)thread;
    (void)cpusetsize;
    (void)cpuset;
    return ENOSYS;
}

int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize, void *cpuset)
{
    (void)thread;
    (void)cpusetsize;
    (void)cpuset;
    return ENOSYS;
}
