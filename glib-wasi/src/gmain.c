/*
 * Minimal GLib GMainLoop / GMainContext / GSource stubs for wasm32-wasi.
 *
 * QEMU's own event loop (AioContext) does the real work; these stubs
 * exist only so that code which references GLib main-loop symbols can
 * link.  Most functions are intentional no-ops or return safe defaults.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../glib/glib.h"
#include "../glib/glist.h"
#include "../glib/gstrfuncs.h"
#include "../glib/gmain.h"

/* ------------------------------------------------------------------ */
/*  Global source-ID counter (starts at 1, 0 is "invalid")            */
/* ------------------------------------------------------------------ */

static guint g_next_source_id = 1;

/* ------------------------------------------------------------------ */
/*  Default GMainContext singleton                                     */
/* ------------------------------------------------------------------ */

static GMainContext g_default_context = {
    .ref_count  = 1,
    .is_running = FALSE,
};

/* ================================================================== */
/*  GMainContext                                                       */
/* ================================================================== */

GMainContext *
g_main_context_new(void)
{
    GMainContext *ctx = g_malloc0(sizeof(GMainContext));
    ctx->ref_count  = 1;
    ctx->is_running = FALSE;
    return ctx;
}

GMainContext *
g_main_context_ref(GMainContext *context)
{
    if (context) {
        context->ref_count++;
    }
    return context;
}

void
g_main_context_unref(GMainContext *context)
{
    if (context == NULL || context == &g_default_context) {
        return;
    }
    if (--context->ref_count <= 0) {
        g_free(context);
    }
}

GMainContext *
g_main_context_default(void)
{
    return &g_default_context;
}

void
g_main_context_push_thread_default(GMainContext *context)
{
    (void)context;
}

void
g_main_context_pop_thread_default(GMainContext *context)
{
    (void)context;
}

GMainContext *
g_main_context_get_thread_default(void)
{
    return NULL;
}

gboolean
g_main_context_iteration(GMainContext *context, gboolean may_block)
{
    (void)context;
    (void)may_block;
    return FALSE;
}

gboolean
g_main_context_pending(GMainContext *context)
{
    (void)context;
    return FALSE;
}

gboolean
g_main_context_acquire(GMainContext *context)
{
    (void)context;
    return TRUE;
}

void
g_main_context_release(GMainContext *context)
{
    (void)context;
}

gboolean
g_main_context_is_owner(GMainContext *context)
{
    (void)context;
    return TRUE;
}

gboolean
g_main_context_wait(GMainContext *context, void *cond, void *mutex)
{
    (void)context;
    (void)cond;
    (void)mutex;
    return TRUE;
}

void
g_main_context_wakeup(GMainContext *context)
{
    (void)context;
}

gboolean
g_main_context_prepare(GMainContext *context, gint *priority)
{
    (void)context;
    if (priority) {
        *priority = 0;
    }
    return FALSE;
}

gint
g_main_context_query(GMainContext *context, gint max_priority,
                     gint *timeout_, GPollFD *fds, gint n_fds)
{
    (void)context;
    (void)max_priority;
    (void)fds;
    (void)n_fds;
    if (timeout_) {
        *timeout_ = -1;
    }
    return 0;
}

gboolean
g_main_context_check(GMainContext *context, gint max_priority,
                     GPollFD *fds, gint n_fds)
{
    (void)context;
    (void)max_priority;
    (void)fds;
    (void)n_fds;
    return FALSE;
}

void
g_main_context_dispatch(GMainContext *context)
{
    (void)context;
}

void
g_main_context_set_poll_func(GMainContext *context, GPollFunc func)
{
    (void)context;
    (void)func;
}

GPollFunc
g_main_context_get_poll_func(GMainContext *context)
{
    (void)context;
    return NULL;
}

GSource *
g_main_context_find_source_by_id(GMainContext *context, guint source_id)
{
    (void)context;
    (void)source_id;
    return NULL;
}

/* ================================================================== */
/*  GMainLoop                                                         */
/* ================================================================== */

GMainLoop *
g_main_loop_new(GMainContext *context, gboolean is_running)
{
    GMainLoop *loop = g_malloc0(sizeof(GMainLoop));
    loop->context    = context ? g_main_context_ref(context)
                               : g_main_context_ref(g_main_context_default());
    loop->is_running = is_running;
    loop->ref_count  = 1;
    return loop;
}

GMainLoop *
g_main_loop_ref(GMainLoop *loop)
{
    if (loop) {
        loop->ref_count++;
    }
    return loop;
}

void
g_main_loop_unref(GMainLoop *loop)
{
    if (loop == NULL) {
        return;
    }
    if (--loop->ref_count <= 0) {
        g_main_context_unref(loop->context);
        g_free(loop);
    }
}

void
g_main_loop_run(GMainLoop *loop)
{
    /*
     * On WASI there is no blocking event loop.  QEMU drives its own
     * main loop via AioContext so we simply mark the loop as running
     * and return immediately.
     */
    if (loop) {
        loop->is_running = TRUE;
    }
}

void
g_main_loop_quit(GMainLoop *loop)
{
    if (loop) {
        loop->is_running = FALSE;
    }
}

gboolean
g_main_loop_is_running(GMainLoop *loop)
{
    return loop ? loop->is_running : FALSE;
}

GMainContext *
g_main_loop_get_context(GMainLoop *loop)
{
    return loop ? loop->context : NULL;
}

/* ================================================================== */
/*  GSource                                                           */
/* ================================================================== */

GSource *
g_source_new(GSourceFuncs *source_funcs, guint struct_size)
{
    GSource *source;

    if (struct_size < sizeof(GSource)) {
        struct_size = sizeof(GSource);
    }

    source = (GSource *)g_malloc0(struct_size);
    source->source_funcs = source_funcs;
    source->ref_count    = 1;
    source->priority     = G_PRIORITY_DEFAULT;
    source->context      = NULL;
    source->poll_fds     = NULL;
    source->name         = NULL;
    source->source_id    = 0;
    return source;
}

GSource *
g_source_ref(GSource *source)
{
    if (source) {
        source->ref_count++;
    }
    return source;
}

void
g_source_unref(GSource *source)
{
    if (source == NULL) {
        return;
    }
    if (--source->ref_count <= 0) {
        if (source->source_funcs && source->source_funcs->finalize) {
            source->source_funcs->finalize(source);
        }
        g_slist_free(source->poll_fds);
        g_free(source->name);
        g_free(source);
    }
}

void
g_source_destroy(GSource *source)
{
    if (source == NULL) {
        return;
    }
    source->context = NULL;
    g_source_unref(source);
}

guint
g_source_attach(GSource *source, GMainContext *context)
{
    if (source == NULL) {
        return 0;
    }
    source->context   = context ? context : g_main_context_default();
    source->source_id = g_next_source_id++;
    return source->source_id;
}

gboolean
g_source_is_destroyed(GSource *source)
{
    (void)source;
    return FALSE;
}

void
g_source_set_priority(GSource *source, gint priority)
{
    if (source) {
        source->priority = priority;
    }
}

gint
g_source_get_priority(GSource *source)
{
    return source ? source->priority : G_PRIORITY_DEFAULT;
}

void
g_source_set_can_recurse(GSource *source, gboolean can_recurse)
{
    (void)source;
    (void)can_recurse;
}

gboolean
g_source_get_can_recurse(GSource *source)
{
    (void)source;
    return FALSE;
}

guint
g_source_get_id(GSource *source)
{
    return source ? source->source_id : 0;
}

GMainContext *
g_source_get_context(GSource *source)
{
    return source ? source->context : NULL;
}

/*
 * Minimal callback storage.  We store the function pointer and data
 * directly in the GSource's callback_data field (via a small helper
 * struct allocated on the heap).
 */
typedef struct {
    GSourceFunc    func;
    gpointer       data;
    GDestroyNotify notify;
} GSourceCallback;

static void
_source_callback_ref(gpointer cb_data)
{
    (void)cb_data;
}

static void
_source_callback_unref(gpointer cb_data)
{
    if (cb_data) {
        GSourceCallback *cb = (GSourceCallback *)cb_data;
        if (cb->notify && cb->data) {
            cb->notify(cb->data);
        }
        g_free(cb);
    }
}

static void
_source_callback_get(gpointer cb_data, GSource *source,
                     GSourceFunc *func, gpointer *data)
{
    (void)source;
    GSourceCallback *cb = (GSourceCallback *)cb_data;
    if (cb) {
        *func = cb->func;
        *data = cb->data;
    } else {
        *func = NULL;
        *data = NULL;
    }
}

static GSourceCallbackFuncs _default_cb_funcs = {
    _source_callback_ref,
    _source_callback_unref,
    _source_callback_get,
};

void
g_source_set_callback(GSource *source, GSourceFunc func,
                      gpointer data, GDestroyNotify notify)
{
    GSourceCallback *cb;

    if (source == NULL) {
        return;
    }

    /* Free previous callback if we own it */
    if (source->callback_funcs && source->callback_data) {
        source->callback_funcs->unref(source->callback_data);
    }

    cb = g_malloc0(sizeof(GSourceCallback));
    cb->func   = func;
    cb->data   = data;
    cb->notify = notify;

    source->callback_data  = cb;
    source->callback_funcs = &_default_cb_funcs;
}

void
g_source_set_callback_indirect(GSource *source, gpointer callback_data,
                               GSourceCallbackFuncs *callback_funcs)
{
    if (source == NULL) {
        return;
    }

    /* Free previous callback if we own it */
    if (source->callback_funcs && source->callback_data) {
        source->callback_funcs->unref(source->callback_data);
    }

    source->callback_data  = callback_data;
    source->callback_funcs = callback_funcs;

    if (callback_funcs && callback_data) {
        callback_funcs->ref(callback_data);
    }
}

void
g_source_add_poll(GSource *source, GPollFD *fd)
{
    if (source == NULL || fd == NULL) {
        return;
    }
    source->poll_fds = g_slist_prepend(source->poll_fds, fd);
}

void
g_source_remove_poll(GSource *source, GPollFD *fd)
{
    if (source == NULL || fd == NULL) {
        return;
    }
    source->poll_fds = g_slist_remove(source->poll_fds, fd);
}

void
g_source_set_name(GSource *source, const gchar *name)
{
    if (source == NULL) {
        return;
    }
    g_free(source->name);
    source->name = name ? g_strdup(name) : NULL;
}

const gchar *
g_source_get_name(GSource *source)
{
    return source ? source->name : NULL;
}

gboolean
g_source_remove(guint tag)
{
    (void)tag;
    return FALSE;
}

void
g_source_set_ready_time(GSource *source, gint64 ready_time)
{
    (void)source;
    (void)ready_time;
}

gint64
g_source_get_ready_time(GSource *source)
{
    (void)source;
    return -1;
}

/* ================================================================== */
/*  Timeout / idle source constructors                                */
/* ================================================================== */

/*
 * Stub GSourceFuncs for timeout and idle sources.  These will never
 * actually fire because we have no real main loop, but the structs
 * must exist so that g_source_new returns valid objects.
 */
static gboolean _timeout_prepare (GSource *s, gint *t) { (void)s; *t = -1; return FALSE; }
static gboolean _timeout_check   (GSource *s)          { (void)s; return FALSE; }
static gboolean _timeout_dispatch(GSource *s, GSourceFunc cb, gpointer d)
{
    (void)s;
    if (cb) {
        return cb(d);
    }
    return FALSE;
}

static GSourceFuncs _timeout_source_funcs = {
    _timeout_prepare,
    _timeout_check,
    _timeout_dispatch,
    NULL,  /* finalize */
    NULL,  /* closure_callback */
    NULL,  /* closure_marshal */
};

static gboolean _idle_prepare (GSource *s, gint *t) { (void)s; *t = 0; return TRUE; }
static gboolean _idle_check   (GSource *s)          { (void)s; return TRUE; }
static gboolean _idle_dispatch(GSource *s, GSourceFunc cb, gpointer d)
{
    (void)s;
    if (cb) {
        return cb(d);
    }
    return FALSE;
}

static GSourceFuncs _idle_source_funcs = {
    _idle_prepare,
    _idle_check,
    _idle_dispatch,
    NULL,  /* finalize */
    NULL,  /* closure_callback */
    NULL,  /* closure_marshal */
};

GSource *
g_timeout_source_new(guint interval)
{
    (void)interval;
    return g_source_new(&_timeout_source_funcs, sizeof(GSource));
}

GSource *
g_timeout_source_new_seconds(guint interval)
{
    (void)interval;
    return g_source_new(&_timeout_source_funcs, sizeof(GSource));
}

GSource *
g_idle_source_new(void)
{
    return g_source_new(&_idle_source_funcs, sizeof(GSource));
}

guint
g_timeout_add(guint interval, GSourceFunc function, gpointer data)
{
    (void)interval;
    (void)function;
    (void)data;
    return 0;
}

guint
g_timeout_add_full(gint priority, guint interval, GSourceFunc function,
                   gpointer data, GDestroyNotify notify)
{
    (void)priority;
    (void)interval;
    (void)function;
    (void)data;
    (void)notify;
    return 0;
}

guint
g_timeout_add_seconds(guint interval, GSourceFunc function, gpointer data)
{
    (void)interval;
    (void)function;
    (void)data;
    return 0;
}

guint
g_idle_add(GSourceFunc function, gpointer data)
{
    (void)function;
    (void)data;
    return 0;
}

guint
g_idle_add_full(gint priority, GSourceFunc function,
                gpointer data, GDestroyNotify notify)
{
    (void)priority;
    (void)function;
    (void)data;
    (void)notify;
    return 0;
}

/* ================================================================== */
/*  g_source_add_child_source                                         */
/* ================================================================== */

void
g_source_add_child_source(GSource *source, GSource *child_source)
{
    (void)source;
    (void)child_source;
}

/* ================================================================== */
/*  g_poll                                                            */
/* ================================================================== */

gint
g_poll(GPollFD *fds, guint nfds, gint timeout)
{
    (void)fds;
    (void)nfds;
    (void)timeout;
    return 0;
}

/* g_clear_handle_id is now a macro in gmain.h — no function body needed */
