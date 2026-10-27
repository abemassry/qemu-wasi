/*
 * pthread stubs for QEMU wasm32-wasi single-threaded build.
 *
 * WASI has no real threading support. The WASI SDK's libc.a includes
 * weak pthread stubs that return EAGAIN for pthread_create (causing
 * QEMU to abort in qemu_thread_create -> error_exit).
 *
 * These stubs override the weak libc definitions and return success (0)
 * so QEMU can initialize. "Created" threads simply never execute — their
 * start routines are not called. This is acceptable for a single-threaded
 * WASI environment because:
 *
 *   - RCU thread: the main loop runs the deferred frees (QEMU patch 0004)
 *   - Worker threads: work items run inline when waited on
 *   - The main execution (CPU loop) is single-threaded anyway
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <pthread.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Thread creation / lifecycle                                        */
/* ------------------------------------------------------------------ */

static unsigned long next_thread_id = 2; /* 1 = main thread */

int pthread_create(pthread_t *restrict thread,
                   const pthread_attr_t *restrict attr,
                   void *(*start_routine)(void *),
                   void *restrict arg)
{
    (void)attr;
    (void)start_routine;
    (void)arg;
    /* Assign a fake thread id so callers that store & compare thread ids
     * don't get confused by all threads being id 0. */
    if (thread) {
        *thread = (pthread_t)next_thread_id++;
    }
    /* Return success — the thread "exists" but never actually runs. */
    return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
    (void)thread;
    if (retval) *retval = NULL;
    /* In single-threaded mode, the "thread" already "finished". */
    return 0;
}

int pthread_detach(pthread_t thread)
{
    (void)thread;
    return 0;
}

void pthread_exit(void *retval)
{
    (void)retval;
    /* In the main thread, this is effectively exit(). In a "fake" thread
     * context we should never get here. Just abort to be safe. */
    abort();
}

pthread_t pthread_self(void)
{
    return (pthread_t)1; /* main thread */
}

/* pthread_equal is a macro in WASI's pthread.h: #define pthread_equal(x,y) ((x)==(y))
 * so we do NOT provide a function definition. */

/* ------------------------------------------------------------------ */
/*  Mutexes — all no-ops in single-threaded mode                       */
/* ------------------------------------------------------------------ */

int pthread_mutex_init(pthread_mutex_t *restrict mutex,
                       const pthread_mutexattr_t *restrict attr)
{
    (void)attr;
    if (mutex) memset(mutex, 0, sizeof(*mutex));
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Mutex attributes                                                    */
/* ------------------------------------------------------------------ */

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (attr) memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    (void)attr;
    (void)type;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Condition variables — single-threaded, waits return immediately     */
/* ------------------------------------------------------------------ */

int pthread_cond_init(pthread_cond_t *restrict cond,
                      const pthread_condattr_t *restrict attr)
{
    (void)attr;
    if (cond) memset(cond, 0, sizeof(*cond));
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *restrict cond,
                      pthread_mutex_t *restrict mutex)
{
    (void)cond;
    (void)mutex;
    /* In a real threaded program, this would block. In single-threaded
     * mode, we return immediately. This may cause spin-waits in code
     * that loops on cond_wait, but that's the best we can do. */
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *restrict cond,
                           pthread_mutex_t *restrict mutex,
                           const struct timespec *restrict abstime)
{
    (void)cond;
    (void)mutex;
    (void)abstime;
    return ETIMEDOUT; /* Pretend the timeout expired */
}

/* ------------------------------------------------------------------ */
/*  Condition variable attributes                                       */
/* ------------------------------------------------------------------ */

int pthread_condattr_init(pthread_condattr_t *attr)
{
    if (attr) memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id)
{
    (void)attr;
    (void)clock_id;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Thread attributes                                                   */
/* ------------------------------------------------------------------ */

int pthread_attr_init(pthread_attr_t *attr)
{
    if (attr) memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    (void)attr;
    (void)detachstate;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    (void)attr;
    (void)stacksize;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Signals (no-op on WASI)                                             */
/* ------------------------------------------------------------------ */

int pthread_sigmask(int how, const void *restrict set,
                    void *restrict oset)
{
    (void)how;
    (void)set;
    (void)oset;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Thread-local storage (TLS) — simple single-threaded implementation  */
/* ------------------------------------------------------------------ */

#define MAX_KEYS 128

static void *tls_values[MAX_KEYS];
static void (*tls_destructors[MAX_KEYS])(void *);
static int tls_used[MAX_KEYS];
static unsigned int next_key = 0;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    if (next_key >= MAX_KEYS) {
        return EAGAIN;
    }
    unsigned int k = next_key++;
    tls_used[k] = 1;
    tls_values[k] = NULL;
    tls_destructors[k] = destructor;
    if (key) *key = (pthread_key_t)k;
    return 0;
}

int pthread_key_delete(pthread_key_t key)
{
    unsigned int k = (unsigned int)key;
    if (k < MAX_KEYS && tls_used[k]) {
        tls_used[k] = 0;
        tls_values[k] = NULL;
        tls_destructors[k] = NULL;
    }
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    unsigned int k = (unsigned int)key;
    if (k < MAX_KEYS) return tls_values[k];
    return NULL;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    unsigned int k = (unsigned int)key;
    if (k < MAX_KEYS) {
        tls_values[k] = (void *)value;
        return 0;
    }
    return EINVAL;
}

/* ------------------------------------------------------------------ */
/*  Thread naming (no-op)                                               */
/* ------------------------------------------------------------------ */

int pthread_setname_np(pthread_t thread, const char *name)
{
    (void)thread;
    (void)name;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Thread affinity (no-op, return ENOSYS)                              */
/* ------------------------------------------------------------------ */

int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize,
                           const struct cpu_set_t *cpuset)
{
    (void)thread;
    (void)cpusetsize;
    (void)cpuset;
    return ENOSYS;
}

int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize,
                           struct cpu_set_t *cpuset)
{
    (void)thread;
    (void)cpusetsize;
    (void)cpuset;
    return ENOSYS;
}

/* ------------------------------------------------------------------ */
/*  Once — single-threaded, just call the init_routine                  */
/* ------------------------------------------------------------------ */

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (once_control && !*once_control) {
        *once_control = 1;
        if (init_routine) init_routine();
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Read-write locks — no-ops in single-threaded mode                   */
/* ------------------------------------------------------------------ */

int pthread_rwlock_init(pthread_rwlock_t *restrict rwlock,
                        const pthread_rwlockattr_t *restrict attr)
{
    (void)attr;
    if (rwlock) memset(rwlock, 0, sizeof(*rwlock));
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    (void)rwlock;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    (void)rwlock;
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    (void)rwlock;
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    (void)rwlock;
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    (void)rwlock;
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    (void)rwlock;
    return 0;
}
