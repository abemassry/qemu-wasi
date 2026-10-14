/*
 * GLib GArray / GPtrArray / GByteArray shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GARRAY_H
#define GLIB_WASI_GARRAY_H

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

#ifndef GLIB_WASI_GCHAR_DEFINED
#define GLIB_WASI_GCHAR_DEFINED
typedef char  gchar;
typedef unsigned char guint8;
#endif

/* ------------------------------------------------------------------ */
/*  GArray — dynamic array of arbitrary-sized elements                */
/* ------------------------------------------------------------------ */

typedef struct _GArray {
    gchar *data;
    guint  len;
} GArray;

#define g_array_index(a, t, i) \
    (((t *)(void *)(a)->data)[(i)])

#define g_array_append_val(a, v) \
    g_array_append_vals((a), &(v), 1)

#define g_array_prepend_val(a, v) \
    g_array_prepend_vals((a), &(v), 1)

#define g_array_insert_val(a, i, v) \
    g_array_insert_vals((a), (i), &(v), 1)

GArray *g_array_new(gboolean zero_terminated, gboolean clear_,
                    guint element_size);
GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear_,
                          guint element_size, guint reserved_size);
GArray *g_array_ref(GArray *array);
void    g_array_unref(GArray *array);
gchar  *g_array_free(GArray *array, gboolean free_segment);

GArray *g_array_append_vals(GArray *array, gconstpointer data, guint len);
GArray *g_array_prepend_vals(GArray *array, gconstpointer data, guint len);
GArray *g_array_insert_vals(GArray *array, guint index_,
                            gconstpointer data, guint len);

GArray *g_array_remove_index(GArray *array, guint index_);
GArray *g_array_remove_index_fast(GArray *array, guint index_);
GArray *g_array_remove_range(GArray *array, guint index_, guint length);

GArray *g_array_set_size(GArray *array, guint length);

guint   g_array_get_element_size(GArray *array);
void    g_array_set_clear_func(GArray *array, GDestroyNotify clear_func);
void    g_array_sort(GArray *array, GCompareFunc compare_func);
void    g_array_sort_with_data(GArray *array,
                               GCompareDataFunc compare_func,
                               gpointer user_data);

/* ------------------------------------------------------------------ */
/*  GPtrArray — dynamic array of pointers                             */
/* ------------------------------------------------------------------ */

typedef struct _GPtrArray {
    gpointer *pdata;
    guint     len;
} GPtrArray;

typedef gboolean (*GEqualFunc)(gconstpointer a, gconstpointer b);

GPtrArray *g_ptr_array_new(void);
GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func);
GPtrArray *g_ptr_array_new_full(guint reserved_size,
                                GDestroyNotify element_free_func);
GPtrArray *g_ptr_array_sized_new(guint reserved_size);

#define g_ptr_array_index(array, index_) \
    ((array)->pdata[(index_)])

GPtrArray *g_ptr_array_ref(GPtrArray *array);
void       g_ptr_array_unref(GPtrArray *array);
gpointer  *g_ptr_array_free(GPtrArray *array, gboolean free_seg);

void       g_ptr_array_add(GPtrArray *array, gpointer data);
void       g_ptr_array_insert(GPtrArray *array, gint index_, gpointer data);

gboolean   g_ptr_array_remove(GPtrArray *array, gpointer data);
gboolean   g_ptr_array_remove_fast(GPtrArray *array, gpointer data);
gpointer   g_ptr_array_remove_index(GPtrArray *array, guint index_);
gpointer   g_ptr_array_remove_index_fast(GPtrArray *array, guint index_);
GPtrArray *g_ptr_array_remove_range(GPtrArray *array, guint index_,
                                    guint length);

void       g_ptr_array_set_size(GPtrArray *array, gint length);
void       g_ptr_array_set_free_func(GPtrArray *array,
                                     GDestroyNotify element_free_func);

void       g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func);
void       g_ptr_array_sort_with_data(GPtrArray *array,
                                      GCompareDataFunc compare_func,
                                      gpointer user_data);
void       g_ptr_array_foreach(GPtrArray *array, GFunc func,
                               gpointer user_data);

gboolean   g_ptr_array_find(GPtrArray *haystack, gconstpointer needle,
                            guint *index_);
gboolean   g_ptr_array_find_with_equal_func(GPtrArray *haystack,
                                            gconstpointer needle,
                                            GEqualFunc equal_func,
                                            guint *index_);

gpointer   g_ptr_array_steal_index(GPtrArray *array, guint index_);
gpointer   g_ptr_array_steal_index_fast(GPtrArray *array, guint index_);

/* ------------------------------------------------------------------ */
/*  GByteArray — dynamic byte array                                   */
/* ------------------------------------------------------------------ */

typedef struct _GByteArray {
    guint8 *data;
    guint   len;
} GByteArray;

GByteArray *g_byte_array_new(void);
GByteArray *g_byte_array_sized_new(guint reserved_size);
GByteArray *g_byte_array_ref(GByteArray *array);
void        g_byte_array_unref(GByteArray *array);
guint8     *g_byte_array_free(GByteArray *array, gboolean free_segment);

GByteArray *g_byte_array_append(GByteArray *array, const guint8 *data,
                                guint len);
GByteArray *g_byte_array_prepend(GByteArray *array, const guint8 *data,
                                 guint len);
GByteArray *g_byte_array_set_size(GByteArray *array, guint length);
GByteArray *g_byte_array_remove_index(GByteArray *array, guint index_);
GByteArray *g_byte_array_remove_index_fast(GByteArray *array, guint index_);
GByteArray *g_byte_array_remove_range(GByteArray *array, guint index_,
                                      guint length);
void        g_byte_array_sort(GByteArray *array, GCompareFunc compare_func);

#endif /* GLIB_WASI_GARRAY_H */
