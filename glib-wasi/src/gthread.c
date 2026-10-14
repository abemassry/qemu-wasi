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
