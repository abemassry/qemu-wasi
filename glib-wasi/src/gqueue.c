/*
 * GLib GQueue implementation for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "glib/gqueue.h"

#include <stdlib.h>
#include <string.h>

/* --- Memory helpers (provided elsewhere) --- */
extern gpointer g_malloc(gsize n_bytes);
extern gpointer g_malloc0(gsize n_bytes);
extern void     g_free(gpointer mem);

/* ================================================================== */
/*  GQueue — public API                                               */
/* ================================================================== */

GQueue *g_queue_new(void)
{
    GQueue *q = (GQueue *)g_malloc0(sizeof(GQueue));
    return q;
}

void g_queue_free(GQueue *queue)
{
    if (queue) {
        g_list_free(queue->head);
        g_free(queue);
    }
}

void g_queue_free_full(GQueue *queue, GDestroyNotify free_func)
{
    if (queue) {
        g_list_free_full(queue->head, free_func);
        g_free(queue);
    }
}

void g_queue_init(GQueue *queue)
{
    queue->head   = NULL;
    queue->tail   = NULL;
    queue->length = 0;
}

void g_queue_clear(GQueue *queue)
{
    g_list_free(queue->head);
    g_queue_init(queue);
}

void g_queue_clear_full(GQueue *queue, GDestroyNotify free_func)
{
    if (free_func) {
        g_list_free_full(queue->head, free_func);
    } else {
        g_list_free(queue->head);
    }
    g_queue_init(queue);
}

gboolean g_queue_is_empty(GQueue *queue)
{
    return (queue->head == NULL);
}

guint g_queue_get_length(GQueue *queue)
{
    return queue->length;
}

/* ------------------------------------------------------------------ */
/*  Push                                                              */
/* ------------------------------------------------------------------ */

void g_queue_push_head(GQueue *queue, gpointer data)
{
    queue->head = g_list_prepend(queue->head, data);
    if (!queue->tail) {
        queue->tail = queue->head;
    }
    queue->length++;
}

void g_queue_push_tail(GQueue *queue, gpointer data)
{
    queue->head = g_list_append(queue->head, data);
    if (queue->tail) {
        queue->tail = queue->tail->next;
    } else {
        queue->tail = queue->head;
    }
    queue->length++;
}

void g_queue_push_nth(GQueue *queue, gpointer data, gint n)
{
    if (n < 0 || (guint)n >= queue->length) {
        g_queue_push_tail(queue, data);
        return;
    }
    if (n == 0) {
        g_queue_push_head(queue, data);
        return;
    }

    queue->head = g_list_insert(queue->head, data, n);
    queue->length++;
}

/* ------------------------------------------------------------------ */
/*  Pop (data)                                                        */
/* ------------------------------------------------------------------ */

gpointer g_queue_pop_head(GQueue *queue)
{
    if (!queue->head) {
        return NULL;
    }

    GList *node = queue->head;
    gpointer data = node->data;

    queue->head = node->next;
    if (queue->head) {
        queue->head->prev = NULL;
    } else {
        queue->tail = NULL;
    }

    g_free(node);
    queue->length--;
    return data;
}

gpointer g_queue_pop_tail(GQueue *queue)
{
    if (!queue->tail) {
        return NULL;
    }

    GList *node = queue->tail;
    gpointer data = node->data;

    queue->tail = node->prev;
    if (queue->tail) {
        queue->tail->next = NULL;
    } else {
        queue->head = NULL;
    }

    g_free(node);
    queue->length--;
    return data;
}

gpointer g_queue_pop_nth(GQueue *queue, guint n)
{
    if (n >= queue->length) {
        return NULL;
    }

    GList *node = g_list_nth(queue->head, n);
    if (!node) {
        return NULL;
    }

    gpointer data = node->data;

    if (node == queue->head) {
        queue->head = node->next;
    }
    if (node == queue->tail) {
        queue->tail = node->prev;
    }
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }

    g_free(node);
    queue->length--;
    return data;
}

/* ------------------------------------------------------------------ */
/*  Peek (data)                                                       */
/* ------------------------------------------------------------------ */

gpointer g_queue_peek_head(GQueue *queue)
{
    return queue->head ? queue->head->data : NULL;
}

gpointer g_queue_peek_tail(GQueue *queue)
{
    return queue->tail ? queue->tail->data : NULL;
}

gpointer g_queue_peek_nth(GQueue *queue, guint n)
{
    GList *node = g_list_nth(queue->head, n);
    return node ? node->data : NULL;
}

/* ------------------------------------------------------------------ */
/*  Iteration / search                                                */
/* ------------------------------------------------------------------ */

void g_queue_foreach(GQueue *queue, GFunc func, gpointer user_data)
{
    g_list_foreach(queue->head, func, user_data);
}

GList *g_queue_find(GQueue *queue, gconstpointer data)
{
    return g_list_find(queue->head, data);
}

GList *g_queue_find_custom(GQueue *queue, gconstpointer data,
                           GCompareFunc func)
{
    return g_list_find_custom(queue->head, data, func);
}

void g_queue_sort(GQueue *queue, GCompareDataFunc compare_func,
                  gpointer user_data)
{
    queue->head = g_list_sort_with_data(queue->head, compare_func,
                                        user_data);
    queue->tail = g_list_last(queue->head);
}

void g_queue_insert_sorted(GQueue *queue, gpointer data,
                           GCompareDataFunc func, gpointer user_data)
{
    queue->head = g_list_insert_sorted_with_data(queue->head, data,
                                                  func, user_data);
    queue->tail = g_list_last(queue->head);
    queue->length++;
}

/* ------------------------------------------------------------------ */
/*  Remove                                                            */
/* ------------------------------------------------------------------ */

gboolean g_queue_remove(GQueue *queue, gconstpointer data)
{
    GList *link_ = g_list_find(queue->head, data);
    if (!link_) {
        return FALSE;
    }

    if (link_ == queue->tail) {
        queue->tail = link_->prev;
    }

    queue->head = g_list_delete_link(queue->head, link_);
    queue->length--;
    return TRUE;
}

guint g_queue_remove_all(GQueue *queue, gconstpointer data)
{
    guint old_length = queue->length;
    GList *node = queue->head;

    while (node) {
        GList *next = node->next;
        if (node->data == data) {
            if (node == queue->tail) {
                queue->tail = node->prev;
            }
            queue->head = g_list_delete_link(queue->head, node);
            queue->length--;
        }
        node = next;
    }

    /* Fix tail if head changed */
    if (queue->head) {
        queue->tail = g_list_last(queue->head);
    }

    return old_length - queue->length;
}

/* ------------------------------------------------------------------ */
/*  Link-level operations                                             */
/* ------------------------------------------------------------------ */

void g_queue_delete_link(GQueue *queue, GList *link_)
{
    if (link_ == queue->tail) {
        queue->tail = link_->prev;
    }

    queue->head = g_list_delete_link(queue->head, link_);
    queue->length--;
}

void g_queue_unlink(GQueue *queue, GList *link_)
{
    if (link_ == queue->tail) {
        queue->tail = link_->prev;
    }

    queue->head = g_list_remove_link(queue->head, link_);
    queue->length--;
}

void g_queue_push_head_link(GQueue *queue, GList *link_)
{
    link_->prev = NULL;
    link_->next = queue->head;

    if (queue->head) {
        queue->head->prev = link_;
    } else {
        queue->tail = link_;
    }

    queue->head = link_;
    queue->length++;
}

void g_queue_push_tail_link(GQueue *queue, GList *link_)
{
    link_->next = NULL;
    link_->prev = queue->tail;

    if (queue->tail) {
        queue->tail->next = link_;
    } else {
        queue->head = link_;
    }

    queue->tail = link_;
    queue->length++;
}

void g_queue_push_nth_link(GQueue *queue, gint n, GList *link_)
{
    if (n < 0 || (guint)n >= queue->length) {
        g_queue_push_tail_link(queue, link_);
        return;
    }
    if (n == 0) {
        g_queue_push_head_link(queue, link_);
        return;
    }

    GList *sibling = g_list_nth(queue->head, (guint)n);
    if (!sibling) {
        g_queue_push_tail_link(queue, link_);
        return;
    }

    link_->prev = sibling->prev;
    link_->next = sibling;
    if (sibling->prev) {
        sibling->prev->next = link_;
    }
    sibling->prev = link_;
    queue->length++;
}

GList *g_queue_pop_head_link(GQueue *queue)
{
    if (!queue->head) {
        return NULL;
    }

    GList *link_ = queue->head;
    queue->head = link_->next;
    if (queue->head) {
        queue->head->prev = NULL;
    } else {
        queue->tail = NULL;
    }

    link_->next = NULL;
    link_->prev = NULL;
    queue->length--;
    return link_;
}

GList *g_queue_pop_tail_link(GQueue *queue)
{
    if (!queue->tail) {
        return NULL;
    }

    GList *link_ = queue->tail;
    queue->tail = link_->prev;
    if (queue->tail) {
        queue->tail->next = NULL;
    } else {
        queue->head = NULL;
    }

    link_->next = NULL;
    link_->prev = NULL;
    queue->length--;
    return link_;
}

GList *g_queue_pop_nth_link(GQueue *queue, guint n)
{
    if (n >= queue->length) {
        return NULL;
    }

    GList *link_ = g_list_nth(queue->head, n);
    if (!link_) {
        return NULL;
    }

    if (link_ == queue->head) {
        queue->head = link_->next;
    }
    if (link_ == queue->tail) {
        queue->tail = link_->prev;
    }
    if (link_->prev) {
        link_->prev->next = link_->next;
    }
    if (link_->next) {
        link_->next->prev = link_->prev;
    }

    link_->next = NULL;
    link_->prev = NULL;
    queue->length--;
    return link_;
}

GList *g_queue_peek_head_link(GQueue *queue)
{
    return queue->head;
}

GList *g_queue_peek_tail_link(GQueue *queue)
{
    return queue->tail;
}

GList *g_queue_peek_nth_link(GQueue *queue, guint n)
{
    return g_list_nth(queue->head, n);
}

gint g_queue_link_index(GQueue *queue, GList *link_)
{
    return g_list_position(queue->head, link_);
}

void g_queue_reverse(GQueue *queue)
{
    GList *old_tail = queue->tail;
    queue->head = g_list_reverse(queue->head);
    queue->tail = old_tail ? g_list_last(queue->head) : NULL;
    /* After reversing, the old head is now the tail */
    if (queue->head) {
        queue->tail = g_list_last(queue->head);
    }
}
