/*
 * GLib GList / GSList implementation for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "glib/glist.h"

#include <stdlib.h>
#include <string.h>

/* --- Memory helpers (provided elsewhere) --- */
extern gpointer g_malloc(gsize n_bytes);
extern gpointer g_malloc0(gsize n_bytes);
extern gpointer g_realloc(gpointer mem, gsize n_bytes);
extern void     g_free(gpointer mem);
extern gpointer g_memdup2(gconstpointer mem, gsize byte_size);

/* ================================================================== */
/*  GList helpers                                                     */
/* ================================================================== */

static GList *g_list_alloc(void)
{
    return (GList *)g_malloc0(sizeof(GList));
}

/* ================================================================== */
/*  GList — public API                                                */
/* ================================================================== */

GList *g_list_append(GList *list, gpointer data)
{
    GList *new_node = g_list_alloc();
    new_node->data = data;

    if (!list) {
        return new_node;
    }

    GList *last = g_list_last(list);
    last->next = new_node;
    new_node->prev = last;
    return list;
}

GList *g_list_prepend(GList *list, gpointer data)
{
    GList *new_node = g_list_alloc();
    new_node->data = data;
    new_node->next = list;

    if (list) {
        list->prev = new_node;
    }
    return new_node;
}

GList *g_list_insert(GList *list, gpointer data, gint position)
{
    if (position < 0) {
        return g_list_append(list, data);
    }
    if (position == 0) {
        return g_list_prepend(list, data);
    }

    GList *tmp = g_list_nth(list, (guint)position);
    if (!tmp) {
        return g_list_append(list, data);
    }

    GList *new_node = g_list_alloc();
    new_node->data = data;
    new_node->prev = tmp->prev;
    new_node->next = tmp;
    if (tmp->prev) {
        tmp->prev->next = new_node;
    }
    tmp->prev = new_node;

    return (new_node->prev) ? list : new_node;
}

GList *g_list_insert_sorted(GList *list, gpointer data,
                            GCompareFunc func)
{
    if (!list) {
        GList *new_node = g_list_alloc();
        new_node->data = data;
        return new_node;
    }

    GList *tmp = list;
    while (tmp->next && func(data, tmp->data) > 0) {
        tmp = tmp->next;
    }

    if (func(data, tmp->data) <= 0) {
        /* Insert before tmp */
        GList *new_node = g_list_alloc();
        new_node->data = data;
        new_node->prev = tmp->prev;
        new_node->next = tmp;
        if (tmp->prev) {
            tmp->prev->next = new_node;
        }
        tmp->prev = new_node;
        return (new_node->prev) ? list : new_node;
    } else {
        /* Append after tmp (end of list) */
        GList *new_node = g_list_alloc();
        new_node->data = data;
        new_node->prev = tmp;
        tmp->next = new_node;
        return list;
    }
}

GList *g_list_insert_sorted_with_data(GList *list, gpointer data,
                                      GCompareDataFunc func,
                                      gpointer user_data)
{
    if (!list) {
        GList *new_node = g_list_alloc();
        new_node->data = data;
        return new_node;
    }

    GList *tmp = list;
    while (tmp->next && func(data, tmp->data, user_data) > 0) {
        tmp = tmp->next;
    }

    if (func(data, tmp->data, user_data) <= 0) {
        GList *new_node = g_list_alloc();
        new_node->data = data;
        new_node->prev = tmp->prev;
        new_node->next = tmp;
        if (tmp->prev) {
            tmp->prev->next = new_node;
        }
        tmp->prev = new_node;
        return (new_node->prev) ? list : new_node;
    } else {
        GList *new_node = g_list_alloc();
        new_node->data = data;
        new_node->prev = tmp;
        tmp->next = new_node;
        return list;
    }
}

GList *g_list_insert_before(GList *list, GList *sibling, gpointer data)
{
    GList *new_node = g_list_alloc();
    new_node->data = data;

    if (!list) {
        /* Empty list: new_node is the only element */
        return new_node;
    }

    if (!sibling) {
        /* sibling is NULL: append to end */
        GList *last = g_list_last(list);
        last->next = new_node;
        new_node->prev = last;
        return list;
    }

    /* Insert before sibling */
    new_node->prev = sibling->prev;
    new_node->next = sibling;
    sibling->prev = new_node;

    if (new_node->prev) {
        new_node->prev->next = new_node;
        return list;
    } else {
        /* sibling was the head: new_node is the new head */
        return new_node;
    }
}

GList *g_list_remove(GList *list, gconstpointer data)
{
    GList *tmp = list;
    while (tmp) {
        if (tmp->data == data) {
            return g_list_delete_link(list, tmp);
        }
        tmp = tmp->next;
    }
    return list;
}

GList *g_list_remove_link(GList *list, GList *llink)
{
    if (!llink) {
        return list;
    }

    if (llink->prev) {
        llink->prev->next = llink->next;
    }
    if (llink->next) {
        llink->next->prev = llink->prev;
    }

    GList *new_head = (llink == list) ? llink->next : list;

    llink->next = NULL;
    llink->prev = NULL;

    return new_head;
}

GList *g_list_delete_link(GList *list, GList *link_)
{
    list = g_list_remove_link(list, link_);
    g_free(link_);
    return list;
}

GList *g_list_remove_all(GList *list, gconstpointer data)
{
    GList *tmp = list;
    while (tmp) {
        GList *next = tmp->next;
        if (tmp->data == data) {
            list = g_list_delete_link(list, tmp);
        }
        tmp = next;
    }
    return list;
}

void g_list_free(GList *list)
{
    while (list) {
        GList *next = list->next;
        g_free(list);
        list = next;
    }
}

void g_list_free_full(GList *list, GDestroyNotify free_func)
{
    GList *tmp = list;
    while (tmp) {
        GList *next = tmp->next;
        if (free_func) {
            free_func(tmp->data);
        }
        g_free(tmp);
        tmp = next;
    }
}

void g_list_free_1(GList *list)
{
    if (list) {
        g_free(list);
    }
}

guint g_list_length(GList *list)
{
    guint n = 0;
    while (list) {
        n++;
        list = list->next;
    }
    return n;
}

GList *g_list_copy(GList *list)
{
    GList *new_list = NULL;
    GList *last = NULL;

    while (list) {
        GList *node = g_list_alloc();
        node->data = list->data;
        node->prev = last;
        if (last) {
            last->next = node;
        } else {
            new_list = node;
        }
        last = node;
        list = list->next;
    }
    return new_list;
}

GList *g_list_copy_deep(GList *list, GCopyFunc func, gpointer user_data)
{
    GList *new_list = NULL;
    GList *last = NULL;

    while (list) {
        GList *node = g_list_alloc();
        node->data = func ? func(list->data, user_data) : list->data;
        node->prev = last;
        if (last) {
            last->next = node;
        } else {
            new_list = node;
        }
        last = node;
        list = list->next;
    }
    return new_list;
}

GList *g_list_reverse(GList *list)
{
    GList *last = NULL;
    while (list) {
        GList *next = list->next;
        list->next = list->prev;
        list->prev = next;
        last = list;
        list = next;
    }
    return last;
}

/* Merge sort for GList */
static GList *g_list_sort_merge(GList *l1, GList *l2,
                                GCompareDataFunc compare_func,
                                gpointer user_data)
{
    GList head;
    GList *tail = &head;
    head.next = NULL;

    while (l1 && l2) {
        if (compare_func(l1->data, l2->data, user_data) <= 0) {
            tail->next = l1;
            l1->prev = tail;
            l1 = l1->next;
        } else {
            tail->next = l2;
            l2->prev = tail;
            l2 = l2->next;
        }
        tail = tail->next;
    }
    tail->next = l1 ? l1 : l2;
    if (tail->next) {
        tail->next->prev = tail;
    }

    GList *result = head.next;
    if (result) {
        result->prev = NULL;
    }
    return result;
}

static GList *g_list_sort_impl(GList *list,
                               GCompareDataFunc compare_func,
                               gpointer user_data)
{
    if (!list || !list->next) {
        return list;
    }

    /* Split into halves using slow/fast pointer */
    GList *slow = list;
    GList *fast = list->next;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }

    GList *second = slow->next;
    slow->next = NULL;
    if (second) {
        second->prev = NULL;
    }

    GList *left  = g_list_sort_impl(list, compare_func, user_data);
    GList *right = g_list_sort_impl(second, compare_func, user_data);

    return g_list_sort_merge(left, right, compare_func, user_data);
}

/* Wrapper: GCompareFunc -> GCompareDataFunc */
static gint g_list_compare_plain(gconstpointer a, gconstpointer b,
                                 gpointer user_data)
{
    GCompareFunc func = (GCompareFunc)(void (*)(void))user_data;
    return func(a, b);
}

GList *g_list_sort(GList *list, GCompareFunc compare_func)
{
    return g_list_sort_impl(list, g_list_compare_plain,
                            (gpointer)(void (*)(void))compare_func);
}

GList *g_list_sort_with_data(GList *list, GCompareDataFunc compare_func,
                             gpointer user_data)
{
    return g_list_sort_impl(list, compare_func, user_data);
}

GList *g_list_concat(GList *list1, GList *list2)
{
    if (!list1) {
        return list2;
    }
    if (!list2) {
        return list1;
    }

    GList *last = g_list_last(list1);
    last->next = list2;
    list2->prev = last;
    return list1;
}

void g_list_foreach(GList *list, GFunc func, gpointer user_data)
{
    while (list) {
        GList *next = list->next;
        func(list->data, user_data);
        list = next;
    }
}

GList *g_list_first(GList *list)
{
    if (!list) {
        return NULL;
    }
    while (list->prev) {
        list = list->prev;
    }
    return list;
}

GList *g_list_last(GList *list)
{
    if (!list) {
        return NULL;
    }
    while (list->next) {
        list = list->next;
    }
    return list;
}

GList *g_list_nth(GList *list, guint n)
{
    while (list && n > 0) {
        list = list->next;
        n--;
    }
    return list;
}

gpointer g_list_nth_data(GList *list, guint n)
{
    GList *node = g_list_nth(list, n);
    return node ? node->data : NULL;
}

GList *g_list_nth_prev(GList *list, guint n)
{
    while (list && n > 0) {
        list = list->prev;
        n--;
    }
    return list;
}

GList *g_list_find(GList *list, gconstpointer data)
{
    while (list) {
        if (list->data == data) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

GList *g_list_find_custom(GList *list, gconstpointer data,
                          GCompareFunc func)
{
    while (list) {
        if (func(list->data, data) == 0) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

gint g_list_position(GList *list, GList *llink)
{
    gint pos = 0;
    while (list) {
        if (list == llink) {
            return pos;
        }
        pos++;
        list = list->next;
    }
    return -1;
}

gint g_list_index(GList *list, gconstpointer data)
{
    gint pos = 0;
    while (list) {
        if (list->data == data) {
            return pos;
        }
        pos++;
        list = list->next;
    }
    return -1;
}

/* ================================================================== */
/*  GSList helpers                                                    */
/* ================================================================== */

static GSList *g_slist_alloc(void)
{
    return (GSList *)g_malloc0(sizeof(GSList));
}

/* ================================================================== */
/*  GSList — public API                                               */
/* ================================================================== */

GSList *g_slist_append(GSList *list, gpointer data)
{
    GSList *new_node = g_slist_alloc();
    new_node->data = data;

    if (!list) {
        return new_node;
    }

    GSList *last = g_slist_last(list);
    last->next = new_node;
    return list;
}

GSList *g_slist_prepend(GSList *list, gpointer data)
{
    GSList *new_node = g_slist_alloc();
    new_node->data = data;
    new_node->next = list;
    return new_node;
}

GSList *g_slist_insert(GSList *list, gpointer data, gint position)
{
    if (position < 0) {
        return g_slist_append(list, data);
    }
    if (position == 0) {
        return g_slist_prepend(list, data);
    }

    GSList *prev = g_slist_nth(list, (guint)(position - 1));
    if (!prev) {
        return g_slist_append(list, data);
    }

    GSList *new_node = g_slist_alloc();
    new_node->data = data;
    new_node->next = prev->next;
    prev->next = new_node;
    return list;
}

GSList *g_slist_insert_sorted(GSList *list, gpointer data,
                              GCompareFunc func)
{
    if (!list || func(data, list->data) <= 0) {
        return g_slist_prepend(list, data);
    }

    GSList *prev = list;
    GSList *tmp = list->next;
    while (tmp && func(data, tmp->data) > 0) {
        prev = tmp;
        tmp = tmp->next;
    }

    GSList *new_node = g_slist_alloc();
    new_node->data = data;
    new_node->next = tmp;
    prev->next = new_node;
    return list;
}

GSList *g_slist_insert_sorted_with_data(GSList *list, gpointer data,
                                        GCompareDataFunc func,
                                        gpointer user_data)
{
    if (!list || func(data, list->data, user_data) <= 0) {
        return g_slist_prepend(list, data);
    }

    GSList *prev = list;
    GSList *tmp = list->next;
    while (tmp && func(data, tmp->data, user_data) > 0) {
        prev = tmp;
        tmp = tmp->next;
    }

    GSList *new_node = g_slist_alloc();
    new_node->data = data;
    new_node->next = tmp;
    prev->next = new_node;
    return list;
}

GSList *g_slist_remove(GSList *list, gconstpointer data)
{
    GSList *prev = NULL;
    GSList *tmp = list;

    while (tmp) {
        if (tmp->data == data) {
            if (prev) {
                prev->next = tmp->next;
            } else {
                list = tmp->next;
            }
            g_free(tmp);
            return list;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    return list;
}

GSList *g_slist_remove_link(GSList *list, GSList *link_)
{
    if (!link_) {
        return list;
    }

    if (link_ == list) {
        list = list->next;
    } else {
        GSList *prev = list;
        while (prev && prev->next != link_) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = link_->next;
        }
    }

    link_->next = NULL;
    return list;
}

GSList *g_slist_delete_link(GSList *list, GSList *link_)
{
    list = g_slist_remove_link(list, link_);
    g_free(link_);
    return list;
}

GSList *g_slist_remove_all(GSList *list, gconstpointer data)
{
    GSList *prev = NULL;
    GSList *tmp = list;

    while (tmp) {
        GSList *next = tmp->next;
        if (tmp->data == data) {
            if (prev) {
                prev->next = next;
            } else {
                list = next;
            }
            g_free(tmp);
        } else {
            prev = tmp;
        }
        tmp = next;
    }
    return list;
}

void g_slist_free(GSList *list)
{
    while (list) {
        GSList *next = list->next;
        g_free(list);
        list = next;
    }
}

void g_slist_free_full(GSList *list, GDestroyNotify free_func)
{
    while (list) {
        GSList *next = list->next;
        if (free_func) {
            free_func(list->data);
        }
        g_free(list);
        list = next;
    }
}

void g_slist_free_1(GSList *list)
{
    if (list) {
        g_free(list);
    }
}

guint g_slist_length(GSList *list)
{
    guint n = 0;
    while (list) {
        n++;
        list = list->next;
    }
    return n;
}

GSList *g_slist_copy(GSList *list)
{
    GSList *new_list = NULL;
    GSList *last = NULL;

    while (list) {
        GSList *node = g_slist_alloc();
        node->data = list->data;
        if (last) {
            last->next = node;
        } else {
            new_list = node;
        }
        last = node;
        list = list->next;
    }
    return new_list;
}

GSList *g_slist_copy_deep(GSList *list, GCopyFunc func,
                          gpointer user_data)
{
    GSList *new_list = NULL;
    GSList *last = NULL;

    while (list) {
        GSList *node = g_slist_alloc();
        node->data = func ? func(list->data, user_data) : list->data;
        if (last) {
            last->next = node;
        } else {
            new_list = node;
        }
        last = node;
        list = list->next;
    }
    return new_list;
}

GSList *g_slist_reverse(GSList *list)
{
    GSList *prev = NULL;
    while (list) {
        GSList *next = list->next;
        list->next = prev;
        prev = list;
        list = next;
    }
    return prev;
}

/* Merge sort for GSList */
static GSList *g_slist_sort_merge(GSList *l1, GSList *l2,
                                  GCompareDataFunc compare_func,
                                  gpointer user_data)
{
    GSList head;
    GSList *tail = &head;
    head.next = NULL;

    while (l1 && l2) {
        if (compare_func(l1->data, l2->data, user_data) <= 0) {
            tail->next = l1;
            l1 = l1->next;
        } else {
            tail->next = l2;
            l2 = l2->next;
        }
        tail = tail->next;
    }
    tail->next = l1 ? l1 : l2;

    return head.next;
}

static GSList *g_slist_sort_impl(GSList *list,
                                 GCompareDataFunc compare_func,
                                 gpointer user_data)
{
    if (!list || !list->next) {
        return list;
    }

    GSList *slow = list;
    GSList *fast = list->next;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }

    GSList *second = slow->next;
    slow->next = NULL;

    GSList *left  = g_slist_sort_impl(list, compare_func, user_data);
    GSList *right = g_slist_sort_impl(second, compare_func, user_data);

    return g_slist_sort_merge(left, right, compare_func, user_data);
}

static gint g_slist_compare_plain(gconstpointer a, gconstpointer b,
                                  gpointer user_data)
{
    GCompareFunc func = (GCompareFunc)(void (*)(void))user_data;
    return func(a, b);
}

GSList *g_slist_sort(GSList *list, GCompareFunc compare_func)
{
    return g_slist_sort_impl(list, g_slist_compare_plain,
                             (gpointer)(void (*)(void))compare_func);
}

GSList *g_slist_sort_with_data(GSList *list,
                               GCompareDataFunc compare_func,
                               gpointer user_data)
{
    return g_slist_sort_impl(list, compare_func, user_data);
}

GSList *g_slist_concat(GSList *list1, GSList *list2)
{
    if (!list1) {
        return list2;
    }
    GSList *last = g_slist_last(list1);
    last->next = list2;
    return list1;
}

void g_slist_foreach(GSList *list, GFunc func, gpointer user_data)
{
    while (list) {
        GSList *next = list->next;
        func(list->data, user_data);
        list = next;
    }
}

GSList *g_slist_last(GSList *list)
{
    if (!list) {
        return NULL;
    }
    while (list->next) {
        list = list->next;
    }
    return list;
}

GSList *g_slist_nth(GSList *list, guint n)
{
    while (list && n > 0) {
        list = list->next;
        n--;
    }
    return list;
}

gpointer g_slist_nth_data(GSList *list, guint n)
{
    GSList *node = g_slist_nth(list, n);
    return node ? node->data : NULL;
}

GSList *g_slist_find(GSList *list, gconstpointer data)
{
    while (list) {
        if (list->data == data) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

GSList *g_slist_find_custom(GSList *list, gconstpointer data,
                            GCompareFunc func)
{
    while (list) {
        if (func(list->data, data) == 0) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

gint g_slist_position(GSList *list, GSList *llink)
{
    gint pos = 0;
    while (list) {
        if (list == llink) {
            return pos;
        }
        pos++;
        list = list->next;
    }
    return -1;
}

gint g_slist_index(GSList *list, gconstpointer data)
{
    gint pos = 0;
    while (list) {
        if (list->data == data) {
            return pos;
        }
        pos++;
        list = list->next;
    }
    return -1;
}
