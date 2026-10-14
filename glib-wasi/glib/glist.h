/*
 * GLib GList / GSList shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GLIST_H
#define GLIB_WASI_GLIST_H

#include <stddef.h>

#ifndef GLIB_WASI_BASIC_TYPES_DEFINED
#define GLIB_WASI_BASIC_TYPES_DEFINED

typedef int            gboolean;
typedef unsigned int   guint;
typedef int            gint;
typedef void          *gpointer;
typedef const void    *gconstpointer;
typedef unsigned long  gsize;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef void (*GDestroyNotify)(gpointer data);
typedef gint (*GCompareFunc)(gconstpointer a, gconstpointer b);
typedef gint (*GCompareDataFunc)(gconstpointer a, gconstpointer b,
                                 gpointer user_data);
typedef void (*GFunc)(gpointer data, gpointer user_data);
typedef gpointer (*GCopyFunc)(gconstpointer src, gpointer user_data);

#endif /* GLIB_WASI_BASIC_TYPES_DEFINED */

/* ------------------------------------------------------------------ */
/*  GList — doubly-linked list                                        */
/* ------------------------------------------------------------------ */

typedef struct _GList {
    gpointer       data;
    struct _GList *next;
    struct _GList *prev;
} GList;

/* Append / prepend / insert */
GList *g_list_append(GList *list, gpointer data);
GList *g_list_prepend(GList *list, gpointer data);
GList *g_list_insert(GList *list, gpointer data, gint position);
GList *g_list_insert_sorted(GList *list, gpointer data,
                            GCompareFunc func);
GList *g_list_insert_sorted_with_data(GList *list, gpointer data,
                                      GCompareDataFunc func,
                                      gpointer user_data);
GList *g_list_insert_before(GList *list, GList *sibling, gpointer data);

/* Remove */
GList *g_list_remove(GList *list, gconstpointer data);
GList *g_list_remove_link(GList *list, GList *llink);
GList *g_list_delete_link(GList *list, GList *link_);
GList *g_list_remove_all(GList *list, gconstpointer data);

/* Free */
void   g_list_free(GList *list);
void   g_list_free_full(GList *list, GDestroyNotify free_func);
void   g_list_free_1(GList *list);

/* Query */
guint  g_list_length(GList *list);

/* Copy / transform */
GList *g_list_copy(GList *list);
GList *g_list_copy_deep(GList *list, GCopyFunc func, gpointer user_data);
GList *g_list_reverse(GList *list);
GList *g_list_sort(GList *list, GCompareFunc compare_func);
GList *g_list_sort_with_data(GList *list, GCompareDataFunc compare_func,
                             gpointer user_data);
GList *g_list_concat(GList *list1, GList *list2);

/* Iteration */
void   g_list_foreach(GList *list, GFunc func, gpointer user_data);

/* Navigation */
GList *g_list_first(GList *list);
GList *g_list_last(GList *list);
GList *g_list_nth(GList *list, guint n);
gpointer g_list_nth_data(GList *list, guint n);
GList *g_list_nth_prev(GList *list, guint n);

/* Search */
GList *g_list_find(GList *list, gconstpointer data);
GList *g_list_find_custom(GList *list, gconstpointer data,
                          GCompareFunc func);
gint   g_list_position(GList *list, GList *llink);
gint   g_list_index(GList *list, gconstpointer data);

/* ------------------------------------------------------------------ */
/*  GSList — singly-linked list                                       */
/* ------------------------------------------------------------------ */

typedef struct _GSList {
    gpointer        data;
    struct _GSList *next;
} GSList;

/* Append / prepend / insert */
GSList *g_slist_append(GSList *list, gpointer data);
GSList *g_slist_prepend(GSList *list, gpointer data);
GSList *g_slist_insert(GSList *list, gpointer data, gint position);
GSList *g_slist_insert_sorted(GSList *list, gpointer data,
                              GCompareFunc func);
GSList *g_slist_insert_sorted_with_data(GSList *list, gpointer data,
                                        GCompareDataFunc func,
                                        gpointer user_data);

/* Remove */
GSList *g_slist_remove(GSList *list, gconstpointer data);
GSList *g_slist_remove_link(GSList *list, GSList *link_);
GSList *g_slist_delete_link(GSList *list, GSList *link_);
GSList *g_slist_remove_all(GSList *list, gconstpointer data);

/* Free */
void    g_slist_free(GSList *list);
void    g_slist_free_full(GSList *list, GDestroyNotify free_func);
void    g_slist_free_1(GSList *list);

/* Query */
guint   g_slist_length(GSList *list);

/* Copy / transform */
GSList *g_slist_copy(GSList *list);
GSList *g_slist_copy_deep(GSList *list, GCopyFunc func,
                          gpointer user_data);
GSList *g_slist_reverse(GSList *list);
GSList *g_slist_sort(GSList *list, GCompareFunc compare_func);
GSList *g_slist_sort_with_data(GSList *list,
                               GCompareDataFunc compare_func,
                               gpointer user_data);
GSList *g_slist_concat(GSList *list1, GSList *list2);

/* Iteration */
void    g_slist_foreach(GSList *list, GFunc func, gpointer user_data);

/* Navigation */
GSList *g_slist_last(GSList *list);
GSList *g_slist_nth(GSList *list, guint n);
gpointer g_slist_nth_data(GSList *list, guint n);

/* Search */
GSList *g_slist_find(GSList *list, gconstpointer data);
GSList *g_slist_find_custom(GSList *list, gconstpointer data,
                            GCompareFunc func);
gint    g_slist_position(GSList *list, GSList *llink);
gint    g_slist_index(GSList *list, gconstpointer data);

/* Convenience macros */
#define g_list_next(list) ((list) ? (((GList *)(list))->next) : NULL)
#define g_list_previous(list) ((list) ? (((GList *)(list))->prev) : NULL)
#define g_slist_next(slist) ((slist) ? (slist)->next : NULL)

#endif /* GLIB_WASI_GLIST_H */
