/*
 * GLib GQueue shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GQUEUE_H
#define GLIB_WASI_GQUEUE_H

#include "glist.h"

/* ------------------------------------------------------------------ */
/*  GQueue — double-ended queue built on GList                        */
/* ------------------------------------------------------------------ */

typedef struct _GQueue {
    GList *head;
    GList *tail;
    guint  length;
} GQueue;

#define G_QUEUE_INIT { NULL, NULL, 0 }

/* Lifecycle */
GQueue  *g_queue_new(void);
void     g_queue_free(GQueue *queue);
void     g_queue_free_full(GQueue *queue, GDestroyNotify free_func);
void     g_queue_init(GQueue *queue);
void     g_queue_clear(GQueue *queue);
void     g_queue_clear_full(GQueue *queue, GDestroyNotify free_func);

/* Query */
gboolean g_queue_is_empty(GQueue *queue);
guint    g_queue_get_length(GQueue *queue);

/* Push */
void     g_queue_push_head(GQueue *queue, gpointer data);
void     g_queue_push_tail(GQueue *queue, gpointer data);
void     g_queue_push_nth(GQueue *queue, gpointer data, gint n);

/* Pop (data) */
gpointer g_queue_pop_head(GQueue *queue);
gpointer g_queue_pop_tail(GQueue *queue);
gpointer g_queue_pop_nth(GQueue *queue, guint n);

/* Peek (data) */
gpointer g_queue_peek_head(GQueue *queue);
gpointer g_queue_peek_tail(GQueue *queue);
gpointer g_queue_peek_nth(GQueue *queue, guint n);

/* Iteration / search */
void     g_queue_foreach(GQueue *queue, GFunc func, gpointer user_data);
GList   *g_queue_find(GQueue *queue, gconstpointer data);
GList   *g_queue_find_custom(GQueue *queue, gconstpointer data,
                             GCompareFunc func);
void     g_queue_sort(GQueue *queue, GCompareDataFunc compare_func,
                      gpointer user_data);
void     g_queue_insert_sorted(GQueue *queue, gpointer data,
                               GCompareDataFunc func, gpointer user_data);

/* Remove */
gboolean g_queue_remove(GQueue *queue, gconstpointer data);
guint    g_queue_remove_all(GQueue *queue, gconstpointer data);

/* Link-level operations */
void     g_queue_delete_link(GQueue *queue, GList *link_);
void     g_queue_unlink(GQueue *queue, GList *link_);
void     g_queue_push_head_link(GQueue *queue, GList *link_);
void     g_queue_push_tail_link(GQueue *queue, GList *link_);
void     g_queue_push_nth_link(GQueue *queue, gint n, GList *link_);
GList   *g_queue_pop_head_link(GQueue *queue);
GList   *g_queue_pop_tail_link(GQueue *queue);
GList   *g_queue_pop_nth_link(GQueue *queue, guint n);
GList   *g_queue_peek_head_link(GQueue *queue);
GList   *g_queue_peek_tail_link(GQueue *queue);
GList   *g_queue_peek_nth_link(GQueue *queue, guint n);
gint     g_queue_link_index(GQueue *queue, GList *link_);

/* Reverse */
void     g_queue_reverse(GQueue *queue);

#endif /* GLIB_WASI_GQUEUE_H */
