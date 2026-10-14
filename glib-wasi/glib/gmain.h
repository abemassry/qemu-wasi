/*
 * GLib GMainLoop / GMainContext / GSource shim for QEMU wasm32-wasi builds.
 *
 * QEMU has its own event loop (AioContext / qemu_main_loop) but still
 * references GLib main-loop types and functions.  These stubs satisfy
 * the linker without pulling in real GLib.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GMAIN_H
#define GLIB_WASI_GMAIN_H

#include "glib.h"
#include "glist.h"   /* GSList */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  GSourceFunc — may already be defined via gutils.h                 */
/* ------------------------------------------------------------------ */

#ifndef __G_SOURCE_FUNC_DEFINED
#define __G_SOURCE_FUNC_DEFINED
typedef gboolean (*GSourceFunc)(gpointer user_data);
#endif

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

typedef struct _GMainLoop          GMainLoop;
typedef struct _GMainContext       GMainContext;
typedef struct _GSource            GSource;
typedef struct _GSourceFuncs       GSourceFuncs;
typedef struct _GSourceCallbackFuncs GSourceCallbackFuncs;
typedef struct _GPollFD            GPollFD;

/* ------------------------------------------------------------------ */
/*  GPollFD                                                           */
/* ------------------------------------------------------------------ */

typedef struct _GPollFD {
    gint    fd;
    gushort events;
    gushort revents;
} GPollFD;

typedef gint (*GPollFunc)(GPollFD *ufds, guint nfsd, gint timeout_);

/* ------------------------------------------------------------------ */
/*  GSourceFuncs                                                      */
/* ------------------------------------------------------------------ */

typedef struct _GSourceFuncs {
    gboolean (*prepare)  (GSource *source, gint *timeout_);
    gboolean (*check)    (GSource *source);
    gboolean (*dispatch) (GSource *source, GSourceFunc callback,
                          gpointer user_data);
    void     (*finalize) (GSource *source);

    /* Padding / closure support (unused in stubs) */
    GSourceFunc closure_callback;
    void (*closure_marshal)(void);
} GSourceFuncs;

/* ------------------------------------------------------------------ */
/*  GSourceCallbackFuncs                                              */
/* ------------------------------------------------------------------ */

typedef struct _GSourceCallbackFuncs {
    void (*ref)   (gpointer cb_data);
    void (*unref) (gpointer cb_data);
    void (*get)   (gpointer cb_data, GSource *source,
                   GSourceFunc *func, gpointer *data);
} GSourceCallbackFuncs;

/* ------------------------------------------------------------------ */
/*  GSource                                                           */
/* ------------------------------------------------------------------ */

struct _GSource {
    gpointer              callback_data;
    GSourceCallbackFuncs *callback_funcs;
    const GSourceFuncs   *source_funcs;
    guint                 ref_count;
    GMainContext         *context;
    gint                  priority;
    guint                 flags;
    guint                 source_id;
    GSList               *poll_fds;
    gchar                *name;
};

/* ------------------------------------------------------------------ */
/*  GMainContext                                                      */
/* ------------------------------------------------------------------ */

struct _GMainContext {
    gint     ref_count;
    gboolean is_running;
};

/* ------------------------------------------------------------------ */
/*  GMainLoop                                                         */
/* ------------------------------------------------------------------ */

struct _GMainLoop {
    GMainContext *context;
    gboolean      is_running;
    gint          ref_count;
};

/* ------------------------------------------------------------------ */
/*  Priority constants                                                */
/* ------------------------------------------------------------------ */

#define G_PRIORITY_HIGH          -100
#define G_PRIORITY_DEFAULT        0
#define G_PRIORITY_HIGH_IDLE      100
#define G_PRIORITY_DEFAULT_IDLE   200
#define G_PRIORITY_LOW            300

/* ------------------------------------------------------------------ */
/*  GMainContext functions                                             */
/* ------------------------------------------------------------------ */

GMainContext *g_main_context_new          (void);
GMainContext *g_main_context_ref          (GMainContext *context);
void          g_main_context_unref        (GMainContext *context);
GMainContext *g_main_context_default      (void);

gboolean      g_main_context_iteration    (GMainContext *context,
                                           gboolean may_block);
gboolean      g_main_context_pending      (GMainContext *context);

void          g_main_context_push_thread_default(GMainContext *context);
void          g_main_context_pop_thread_default (GMainContext *context);
GMainContext *g_main_context_get_thread_default (void);

gboolean      g_main_context_acquire      (GMainContext *context);
void          g_main_context_release      (GMainContext *context);
gboolean      g_main_context_is_owner     (GMainContext *context);
gboolean      g_main_context_wait         (GMainContext *context,
                                           void *cond, void *mutex);
void          g_main_context_wakeup       (GMainContext *context);

gboolean      g_main_context_prepare      (GMainContext *context,
                                           gint *priority);
gint          g_main_context_query        (GMainContext *context,
                                           gint max_priority,
                                           gint *timeout_,
                                           GPollFD *fds, gint n_fds);
gboolean      g_main_context_check        (GMainContext *context,
                                           gint max_priority,
                                           GPollFD *fds, gint n_fds);
void          g_main_context_dispatch     (GMainContext *context);

void          g_main_context_set_poll_func(GMainContext *context,
                                           GPollFunc func);
GPollFunc     g_main_context_get_poll_func(GMainContext *context);

GSource      *g_main_context_find_source_by_id(GMainContext *context,
                                               guint source_id);

/* ------------------------------------------------------------------ */
/*  GMainLoop functions                                               */
/* ------------------------------------------------------------------ */

GMainLoop    *g_main_loop_new             (GMainContext *context,
                                           gboolean is_running);
GMainLoop    *g_main_loop_ref             (GMainLoop *loop);
void          g_main_loop_unref           (GMainLoop *loop);
void          g_main_loop_run             (GMainLoop *loop);
void          g_main_loop_quit            (GMainLoop *loop);
gboolean      g_main_loop_is_running      (GMainLoop *loop);
GMainContext *g_main_loop_get_context     (GMainLoop *loop);

/* ------------------------------------------------------------------ */
/*  GSource functions                                                 */
/* ------------------------------------------------------------------ */

GSource      *g_source_new                (GSourceFuncs *source_funcs,
                                           guint struct_size);
GSource      *g_source_ref                (GSource *source);
void          g_source_unref              (GSource *source);
void          g_source_destroy            (GSource *source);
guint         g_source_attach             (GSource *source,
                                           GMainContext *context);
gboolean      g_source_is_destroyed       (GSource *source);

void          g_source_set_priority       (GSource *source, gint priority);
gint          g_source_get_priority       (GSource *source);
void          g_source_set_can_recurse    (GSource *source,
                                           gboolean can_recurse);
gboolean      g_source_get_can_recurse    (GSource *source);
guint         g_source_get_id             (GSource *source);
GMainContext *g_source_get_context        (GSource *source);

void          g_source_set_callback       (GSource *source,
                                           GSourceFunc func,
                                           gpointer data,
                                           GDestroyNotify notify);
void          g_source_set_callback_indirect(GSource *source,
                                             gpointer callback_data,
                                             GSourceCallbackFuncs *callback_funcs);

void          g_source_add_poll           (GSource *source, GPollFD *fd);
void          g_source_remove_poll        (GSource *source, GPollFD *fd);

void          g_source_set_name           (GSource *source,
                                           const gchar *name);
const gchar  *g_source_get_name           (GSource *source);

gboolean      g_source_remove             (guint tag);

void          g_source_set_ready_time     (GSource *source,
                                           gint64 ready_time);
gint64        g_source_get_ready_time     (GSource *source);

void          g_source_add_child_source   (GSource *source,
                                           GSource *child_source);

/* ------------------------------------------------------------------ */
/*  g_poll stub                                                       */
/* ------------------------------------------------------------------ */

gint          g_poll                      (GPollFD *fds, guint nfds,
                                           gint timeout);

/* ------------------------------------------------------------------ */
/*  g_clear_handle_id                                                 */
/*  Implemented as a macro to avoid type mismatch with g_source_remove*/
/*  (which returns gboolean, not void).                               */
/* ------------------------------------------------------------------ */

#define g_clear_handle_id(tag_ptr, clear_func)     \
    G_STMT_START {                                  \
        guint *_tag_ptr = (tag_ptr);                \
        guint _handle_id;                           \
        _handle_id = *_tag_ptr;                     \
        if (_handle_id > 0) {                       \
            *_tag_ptr = 0;                          \
            (clear_func)(_handle_id);               \
        }                                           \
    } G_STMT_END

/* ------------------------------------------------------------------ */
/*  Timeout / idle source constructors                                */
/* ------------------------------------------------------------------ */

GSource      *g_timeout_source_new        (guint interval);
GSource      *g_timeout_source_new_seconds(guint interval);
GSource      *g_idle_source_new           (void);

guint         g_timeout_add               (guint interval,
                                           GSourceFunc function,
                                           gpointer data);
guint         g_timeout_add_full          (gint priority, guint interval,
                                           GSourceFunc function,
                                           gpointer data,
                                           GDestroyNotify notify);
guint         g_timeout_add_seconds       (guint interval,
                                           GSourceFunc function,
                                           gpointer data);
guint         g_idle_add                  (GSourceFunc function,
                                           gpointer data);
guint         g_idle_add_full             (gint priority,
                                           GSourceFunc function,
                                           gpointer data,
                                           GDestroyNotify notify);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GMAIN_H */
