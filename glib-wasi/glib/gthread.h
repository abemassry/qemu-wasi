/*
 * GLib threading primitives shim for QEMU wasm32-wasi builds.
 *
 * wasm32-wasi is single-threaded, so every mutex / lock / condition
 * variable is a no-op, and "thread creation" just calls the function
 * directly in the current (only) thread.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GTHREAD_H
#define GLIB_WASI_GTHREAD_H

#include <stdint.h>

/*
 * Basic GLib types -- these will also appear in the top-level glib.h;
 * guard them so they can be included from either place.
 */
#ifndef GLIB_WASI_BASIC_TYPES_DEFINED
#define GLIB_WASI_BASIC_TYPES_DEFINED

typedef int            gboolean;
typedef int            gint;
typedef unsigned int   guint;
typedef void *         gpointer;
typedef const void *   gconstpointer;
typedef unsigned long  gsize;
typedef long           gssize;
typedef char           gchar;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* GLIB_WASI_BASIC_TYPES_DEFINED */

/* We need gint64 for g_cond_wait_until. */
#ifndef GLIB_WASI_INT64_DEFINED
#define GLIB_WASI_INT64_DEFINED
typedef int64_t gint64;
typedef uint64_t guint64;
#endif

/* Forward declarations used by function signatures below. */
typedef void (*GDestroyNotify)(gpointer data);

/* GError — just enough for g_thread_try_new's signature. */
#ifndef GLIB_WASI_GERROR_DEFINED
#define GLIB_WASI_GERROR_DEFINED
typedef struct _GError GError;
#endif

/* ------------------------------------------------------------------ */
/*  Thread function type                                              */
/* ------------------------------------------------------------------ */

typedef gpointer (*GThreadFunc)(gpointer data);

/* ------------------------------------------------------------------ */
/*  Synchronisation primitive types (all trivially empty)             */
/* ------------------------------------------------------------------ */

typedef struct _GMutex    { int dummy; } GMutex;
typedef struct _GRecMutex { int dummy; } GRecMutex;
typedef struct _GRWLock   { int dummy; } GRWLock;
typedef struct _GCond     { int dummy; } GCond;

typedef struct _GOnce {
    volatile gint     status;
    volatile gpointer retval;
} GOnce;

typedef struct _GPrivate {
    gpointer       value;
    GDestroyNotify notify;
} GPrivate;

typedef struct _GThread {
    gpointer dummy;
} GThread;

/* ------------------------------------------------------------------ */
/*  Initialisers                                                      */
/* ------------------------------------------------------------------ */

#define G_ONCE_INIT    { 0, NULL }
#define G_PRIVATE_INIT(notify_fn) { NULL, (notify_fn) }
#define G_MUTEX_INIT   { 0 }

/* ------------------------------------------------------------------ */
/*  GOnce status enum                                                 */
/* ------------------------------------------------------------------ */

enum {
    G_ONCE_STATUS_NOTCALLED = 0,
    G_ONCE_STATUS_PROGRESS  = 1,
    G_ONCE_STATUS_READY     = 2
};

/* ------------------------------------------------------------------ */
/*  G_LOCK macros (all no-ops in single-threaded wasm32)              */
/* ------------------------------------------------------------------ */

#define G_LOCK_NAME(name)          g__##name##_lock
#define G_LOCK_DEFINE(name)        GMutex G_LOCK_NAME(name) = G_MUTEX_INIT
#define G_LOCK_DEFINE_STATIC(name) static GMutex G_LOCK_NAME(name) = G_MUTEX_INIT
#define G_LOCK_EXTERN(name)        extern GMutex G_LOCK_NAME(name)
#define G_LOCK(name)               (void)0
#define G_UNLOCK(name)             (void)0
#define G_TRYLOCK(name)            TRUE

/* ------------------------------------------------------------------ */
/*  Function declarations (implementations in src/gthread.c)          */
/* ------------------------------------------------------------------ */

/* Mutex */
void     g_mutex_init    (GMutex *mutex);
void     g_mutex_clear   (GMutex *mutex);
void     g_mutex_lock    (GMutex *mutex);
gboolean g_mutex_trylock (GMutex *mutex);
void     g_mutex_unlock  (GMutex *mutex);

/* Recursive mutex */
void     g_rec_mutex_init    (GRecMutex *rec_mutex);
void     g_rec_mutex_clear   (GRecMutex *rec_mutex);
void     g_rec_mutex_lock    (GRecMutex *rec_mutex);
gboolean g_rec_mutex_trylock (GRecMutex *rec_mutex);
void     g_rec_mutex_unlock  (GRecMutex *rec_mutex);

/* Read-write lock */
void     g_rw_lock_init          (GRWLock *rw_lock);
void     g_rw_lock_clear         (GRWLock *rw_lock);
void     g_rw_lock_reader_lock   (GRWLock *rw_lock);
gboolean g_rw_lock_reader_trylock(GRWLock *rw_lock);
void     g_rw_lock_reader_unlock (GRWLock *rw_lock);
void     g_rw_lock_writer_lock   (GRWLock *rw_lock);
gboolean g_rw_lock_writer_trylock(GRWLock *rw_lock);
void     g_rw_lock_writer_unlock (GRWLock *rw_lock);

/* Condition variable */
void     g_cond_init      (GCond *cond);
void     g_cond_clear     (GCond *cond);
void     g_cond_wait      (GCond *cond, GMutex *mutex);
gboolean g_cond_wait_until(GCond *cond, GMutex *mutex, gint64 end_time);
void     g_cond_signal    (GCond *cond);
void     g_cond_broadcast (GCond *cond);

/* GOnce */
gpointer g_once_impl(GOnce *once, GThreadFunc func, gpointer arg);

/*
 * g_once() macro — calls func exactly once (single-threaded on wasm32).
 * Real GLib implements this with mutex-based double-checked locking;
 * for single-threaded WASI we just check status directly.
 */
#define g_once(once, func, arg) \
    (((once)->status == G_ONCE_STATUS_READY) ? \
     (once)->retval : \
     g_once_impl((once), (func), (arg)))

/* GPrivate (thread-local storage — single value in single-threaded) */
gpointer g_private_get    (GPrivate *key);
void     g_private_set    (GPrivate *key, gpointer value);
void     g_private_replace(GPrivate *key, gpointer value);

/* GThread */
GThread *g_thread_self   (void);
GThread *g_thread_new    (const gchar *name, GThreadFunc func, gpointer data);
GThread *g_thread_try_new(const gchar *name, GThreadFunc func, gpointer data,
                          GError **error);
void     g_thread_yield  (void);
gpointer g_thread_join   (GThread *thread);

#endif /* GLIB_WASI_GTHREAD_H */
