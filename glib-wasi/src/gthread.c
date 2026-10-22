/*
 * GLib threading primitives shim – single-threaded wasm32-wasi stubs.
 *
 * Every synchronisation primitive is a no-op; "thread creation" calls
 * the function directly in the current (only) thread.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>

#include "../glib/gthread.h"

#ifdef WASI_THREADS
/*
 * wasm32-wasip1-threads: the primitives are wasi-libc's pthreads (see
 * gthread.h).  GLib's own semantics are kept where QEMU relies on them:
 * g_cond_wait_until() takes a monotonic deadline in microseconds, and
 * g_once_init_enter() makes concurrent callers wait for the first one.
 */
#include <errno.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>

gint64 g_get_monotonic_time(void);

/* Mutex */
void g_mutex_init(GMutex *mutex)
{
    pthread_mutex_init(&mutex->m, NULL);
}

void g_mutex_clear(GMutex *mutex)
{
    pthread_mutex_destroy(&mutex->m);
}

void g_mutex_lock(GMutex *mutex)
{
    pthread_mutex_lock(&mutex->m);
}

gboolean g_mutex_trylock(GMutex *mutex)
{
    return pthread_mutex_trylock(&mutex->m) == 0;
}

void g_mutex_unlock(GMutex *mutex)
{
    pthread_mutex_unlock(&mutex->m);
}

/* Recursive mutex: a zero-filled one becomes recursive on first use. */
static pthread_mutex_t rec_init_lock = PTHREAD_MUTEX_INITIALIZER;

static void rec_mutex_ready(GRecMutex *rec_mutex)
{
    if (__atomic_load_n(&rec_mutex->ready, __ATOMIC_ACQUIRE)) {
        return;
    }
    pthread_mutex_lock(&rec_init_lock);
    if (!rec_mutex->ready) {
        pthread_mutexattr_t attr;

        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&rec_mutex->m, &attr);
        pthread_mutexattr_destroy(&attr);
        __atomic_store_n(&rec_mutex->ready, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&rec_init_lock);
}

void g_rec_mutex_init(GRecMutex *rec_mutex)
{
    rec_mutex->ready = 0;
    rec_mutex_ready(rec_mutex);
}

void g_rec_mutex_clear(GRecMutex *rec_mutex)
{
    if (rec_mutex->ready) {
        pthread_mutex_destroy(&rec_mutex->m);
        rec_mutex->ready = 0;
    }
}

void g_rec_mutex_lock(GRecMutex *rec_mutex)
{
    rec_mutex_ready(rec_mutex);
    pthread_mutex_lock(&rec_mutex->m);
}

gboolean g_rec_mutex_trylock(GRecMutex *rec_mutex)
{
    rec_mutex_ready(rec_mutex);
    return pthread_mutex_trylock(&rec_mutex->m) == 0;
}

void g_rec_mutex_unlock(GRecMutex *rec_mutex)
{
    pthread_mutex_unlock(&rec_mutex->m);
}

/* Read-write lock */
void g_rw_lock_init(GRWLock *rw_lock)
{
    pthread_rwlock_init(&rw_lock->l, NULL);
}

void g_rw_lock_clear(GRWLock *rw_lock)
{
    pthread_rwlock_destroy(&rw_lock->l);
}

void g_rw_lock_reader_lock(GRWLock *rw_lock)
{
    pthread_rwlock_rdlock(&rw_lock->l);
}

gboolean g_rw_lock_reader_trylock(GRWLock *rw_lock)
{
    return pthread_rwlock_tryrdlock(&rw_lock->l) == 0;
}

void g_rw_lock_reader_unlock(GRWLock *rw_lock)
{
    pthread_rwlock_unlock(&rw_lock->l);
}

void g_rw_lock_writer_lock(GRWLock *rw_lock)
{
    pthread_rwlock_wrlock(&rw_lock->l);
}

gboolean g_rw_lock_writer_trylock(GRWLock *rw_lock)
{
    return pthread_rwlock_trywrlock(&rw_lock->l) == 0;
}

void g_rw_lock_writer_unlock(GRWLock *rw_lock)
{
    pthread_rwlock_unlock(&rw_lock->l);
}

/* Condition variable */
void g_cond_init(GCond *cond)
{
    pthread_cond_init(&cond->c, NULL);
}

void g_cond_clear(GCond *cond)
{
    pthread_cond_destroy(&cond->c);
}

void g_cond_wait(GCond *cond, GMutex *mutex)
{
    pthread_cond_wait(&cond->c, &mutex->m);
}

/* @end_time is on the monotonic clock, in microseconds. */
gboolean g_cond_wait_until(GCond *cond, GMutex *mutex, gint64 end_time)
{
    gint64 left = end_time - g_get_monotonic_time();
    struct timespec ts;

    if (left <= 0) {
        return FALSE;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += left / 1000000;
    ts.tv_nsec += (left % 1000000) * 1000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(&cond->c, &mutex->m, &ts) != ETIMEDOUT;
}

void g_cond_signal(GCond *cond)
{
    pthread_cond_signal(&cond->c);
}

void g_cond_broadcast(GCond *cond)
{
    pthread_cond_broadcast(&cond->c);
}

/* GOnce, and the g_once_init_enter()/leave() of gutils.c */
static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t once_cond = PTHREAD_COND_INITIALIZER;

gpointer g_once_impl(GOnce *once, GThreadFunc func, gpointer arg)
{
    pthread_mutex_lock(&once_lock);
    while (once->status == G_ONCE_STATUS_PROGRESS) {
        pthread_cond_wait(&once_cond, &once_lock);
    }
    if (once->status != G_ONCE_STATUS_READY) {
        gpointer retval;

        once->status = G_ONCE_STATUS_PROGRESS;
        pthread_mutex_unlock(&once_lock);
        retval = func(arg);
        pthread_mutex_lock(&once_lock);
        once->retval = retval;
        __atomic_store_n(&once->status, G_ONCE_STATUS_READY, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&once_cond);
    }
    pthread_mutex_unlock(&once_lock);
    return once->retval;
}

/* Locations being initialised by a g_once_init_enter() caller. */
static volatile gsize **once_busy;
static size_t once_busy_n, once_busy_cap;

static int once_busy_find(volatile gsize *location)
{
    for (size_t i = 0; i < once_busy_n; i++) {
        if (once_busy[i] == location) {
            return (int)i;
        }
    }
    return -1;
}

gboolean g_once_init_enter(volatile gsize *location)
{
    gboolean mine = FALSE;

    if (__atomic_load_n(location, __ATOMIC_ACQUIRE) != 0) {
        return FALSE;
    }
    pthread_mutex_lock(&once_lock);
    if (*location == 0) {
        if (once_busy_find(location) < 0) {
            if (once_busy_n == once_busy_cap) {
                once_busy_cap = once_busy_cap ? once_busy_cap * 2 : 16;
                once_busy = realloc((void *)once_busy,
                                    once_busy_cap * sizeof(*once_busy));
            }
            once_busy[once_busy_n++] = location;
            mine = TRUE;
        } else {
            while (once_busy_find(location) >= 0) {
                pthread_cond_wait(&once_cond, &once_lock);
            }
        }
    }
    pthread_mutex_unlock(&once_lock);
    return mine;
}

void g_once_init_leave(volatile gsize *location, gsize result)
{
    int i;

    __atomic_store_n(location, result, __ATOMIC_RELEASE);
    pthread_mutex_lock(&once_lock);
    i = once_busy_find(location);
    if (i >= 0) {
        once_busy[i] = once_busy[--once_busy_n];
    }
    pthread_cond_broadcast(&once_cond);
    pthread_mutex_unlock(&once_lock);
}

/* GPrivate: a pthread key, created on first use. */
static void private_ready(GPrivate *key)
{
    if (__atomic_load_n(&key->ready, __ATOMIC_ACQUIRE)) {
        return;
    }
    pthread_mutex_lock(&once_lock);
    if (!key->ready) {
        pthread_key_create(&key->key, key->notify);
        __atomic_store_n(&key->ready, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&once_lock);
}

gpointer g_private_get(GPrivate *key)
{
    private_ready(key);
    return pthread_getspecific(key->key);
}

void g_private_set(GPrivate *key, gpointer value)
{
    private_ready(key);
    pthread_setspecific(key->key, value);
}

void g_private_replace(GPrivate *key, gpointer value)
{
    gpointer old = g_private_get(key);

    pthread_setspecific(key->key, value);
    if (old && key->notify) {
        key->notify(old);
    }
}

/* GThread */
static __thread GThread *this_thread;

static void *thread_start(void *arg)
{
    GThread *thread = arg;

    this_thread = thread;
    thread->retval = thread->func(thread->data);
    return thread->retval;
}

GThread *g_thread_self(void)
{
    if (!this_thread) {
        this_thread = calloc(1, sizeof(GThread));
        this_thread->thread = pthread_self();
    }
    return this_thread;
}

GThread *g_thread_try_new(const gchar *name, GThreadFunc func, gpointer data,
                          GError **error)
{
    GThread *thread = calloc(1, sizeof(GThread));

    (void)name;
    (void)error;
    thread->func = func;
    thread->data = data;
    if (pthread_create(&thread->thread, NULL, thread_start, thread) != 0) {
        free(thread);
        return NULL;
    }
    return thread;
}

GThread *g_thread_new(const gchar *name, GThreadFunc func, gpointer data)
{
    GThread *thread = g_thread_try_new(name, func, data, NULL);

    if (!thread) {
        abort();
    }
    return thread;
}

void g_thread_yield(void)
{
    sched_yield();
}

gpointer g_thread_join(GThread *thread)
{
    void *retval = NULL;

    pthread_join(thread->thread, &retval);
    free(thread);
    return retval;
}

#else /* !WASI_THREADS */

/* ------------------------------------------------------------------ */
/*  Mutex (no-ops)                                                    */
/* ------------------------------------------------------------------ */

void     g_mutex_init   (GMutex *mutex)    { (void)mutex; }
void     g_mutex_clear  (GMutex *mutex)    { (void)mutex; }
void     g_mutex_lock   (GMutex *mutex)    { (void)mutex; }
gboolean g_mutex_trylock(GMutex *mutex)    { (void)mutex; return TRUE; }
void     g_mutex_unlock (GMutex *mutex)    { (void)mutex; }

/* ------------------------------------------------------------------ */
/*  Recursive mutex (no-ops)                                          */
/* ------------------------------------------------------------------ */

void     g_rec_mutex_init   (GRecMutex *rec_mutex)  { (void)rec_mutex; }
void     g_rec_mutex_clear  (GRecMutex *rec_mutex)  { (void)rec_mutex; }
void     g_rec_mutex_lock   (GRecMutex *rec_mutex)  { (void)rec_mutex; }
gboolean g_rec_mutex_trylock(GRecMutex *rec_mutex)  { (void)rec_mutex; return TRUE; }
void     g_rec_mutex_unlock (GRecMutex *rec_mutex)  { (void)rec_mutex; }

/* ------------------------------------------------------------------ */
/*  Read-write lock (no-ops)                                          */
/* ------------------------------------------------------------------ */

void     g_rw_lock_init          (GRWLock *rw_lock)  { (void)rw_lock; }
void     g_rw_lock_clear         (GRWLock *rw_lock)  { (void)rw_lock; }
void     g_rw_lock_reader_lock   (GRWLock *rw_lock)  { (void)rw_lock; }
gboolean g_rw_lock_reader_trylock(GRWLock *rw_lock)  { (void)rw_lock; return TRUE; }
void     g_rw_lock_reader_unlock (GRWLock *rw_lock)  { (void)rw_lock; }
void     g_rw_lock_writer_lock   (GRWLock *rw_lock)  { (void)rw_lock; }
gboolean g_rw_lock_writer_trylock(GRWLock *rw_lock)  { (void)rw_lock; return TRUE; }
void     g_rw_lock_writer_unlock (GRWLock *rw_lock)  { (void)rw_lock; }

/* ------------------------------------------------------------------ */
/*  Condition variable                                                */
/* ------------------------------------------------------------------ */

void g_cond_init (GCond *cond)  { (void)cond; }
void g_cond_clear(GCond *cond)  { (void)cond; }

/*
 * g_cond_wait should never actually be called in a single-threaded
 * program — there is nobody to signal the condition.  We include the
 * stub so linking succeeds.
 */
void g_cond_wait(GCond *cond, GMutex *mutex)
{
    (void)cond;
    (void)mutex;
}

gboolean g_cond_wait_until(GCond *cond, GMutex *mutex, gint64 end_time)
{
    (void)cond;
    (void)mutex;
    (void)end_time;
    /* Timeout immediately — no other thread can signal us. */
    return FALSE;
}

void g_cond_signal   (GCond *cond)  { (void)cond; }
void g_cond_broadcast(GCond *cond)  { (void)cond; }

/* ------------------------------------------------------------------ */
/*  GOnce                                                             */
/* ------------------------------------------------------------------ */

gpointer g_once_impl(GOnce *once, GThreadFunc func, gpointer arg)
{
    if (once->status == G_ONCE_STATUS_NOTCALLED) {
        once->status = G_ONCE_STATUS_PROGRESS;
        once->retval = func(arg);
        once->status = G_ONCE_STATUS_READY;
    }
    return once->retval;
}

/* ------------------------------------------------------------------ */
/*  GPrivate (thread-local storage — single value)                    */
/* ------------------------------------------------------------------ */

gpointer g_private_get(GPrivate *key)
{
    return key->value;
}

void g_private_set(GPrivate *key, gpointer value)
{
    key->value = value;
}

void g_private_replace(GPrivate *key, gpointer value)
{
    if (key->value != NULL && key->notify != NULL) {
        key->notify(key->value);
    }
    key->value = value;
}

/* ------------------------------------------------------------------ */
/*  GThread                                                           */
/* ------------------------------------------------------------------ */

/* A single static dummy thread — g_thread_self() always returns this. */
static GThread _wasi_main_thread = { NULL };

GThread *g_thread_self(void)
{
    return &_wasi_main_thread;
}

/*
 * "Creating" a thread in single-threaded wasm32 just calls the
 * function synchronously and returns the dummy thread handle.
 */
GThread *g_thread_new(const gchar *name, GThreadFunc func, gpointer data)
{
    (void)name;
    if (func) {
        _wasi_main_thread.dummy = func(data);
    }
    return &_wasi_main_thread;
}

GThread *g_thread_try_new(const gchar *name, GThreadFunc func, gpointer data,
                          GError **error)
{
    (void)error;
    return g_thread_new(name, func, data);
}

void g_thread_yield(void)
{
    /* No-op: nothing to yield to. */
}

gpointer g_thread_join(GThread *thread)
{
    (void)thread;
    return _wasi_main_thread.dummy;
}

#endif /* WASI_THREADS */
