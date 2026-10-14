/*
 * GLib GArray / GPtrArray / GByteArray implementation for QEMU wasm32-wasi.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "glib/garray.h"

#include <stdlib.h>
#include <string.h>

/* --- Memory helpers (provided elsewhere) --- */
extern gpointer g_malloc(gsize n_bytes);
extern gpointer g_malloc0(gsize n_bytes);
extern gpointer g_realloc(gpointer mem, gsize n_bytes);
extern void     g_free(gpointer mem);
extern gpointer g_memdup2(gconstpointer mem, gsize byte_size);

/* ================================================================== */
/*  GArray internals                                                  */
/* ================================================================== */

typedef struct _RealArray {
    gchar         *data;
    guint          len;
    /* --- private fields below the public struct --- */
    guint          alloc;         /* allocated element slots */
    guint          element_size;
    guint          ref_count;
    gboolean       zero_terminated;
    gboolean       clear;
    GDestroyNotify clear_func;
} RealArray;

#define ARRAY_REAL(a) ((RealArray *)(a))

static guint array_elt_zero_len(RealArray *r)
{
    return r->zero_terminated ? 1 : 0;
}

static void array_maybe_grow(RealArray *r, guint extra)
{
    guint need = r->len + extra + array_elt_zero_len(r);
    if (need <= r->alloc) {
        return;
    }
    guint new_alloc = r->alloc ? r->alloc : 4;
    while (new_alloc < need) {
        new_alloc *= 2;
    }
    r->data = (gchar *)g_realloc(r->data,
                                 (gsize)new_alloc * r->element_size);
    r->alloc = new_alloc;
}

static void array_zero_terminate(RealArray *r)
{
    if (r->zero_terminated) {
        memset(r->data + (gsize)r->len * r->element_size,
               0, r->element_size);
    }
}

static void array_clear_elements(RealArray *r, guint index_, guint count)
{
    if (r->clear_func) {
        guint i;
        for (i = 0; i < count; i++) {
            r->clear_func(r->data +
                          (gsize)(index_ + i) * r->element_size);
        }
    }
}

/* ================================================================== */
/*  GArray public API                                                 */
/* ================================================================== */

GArray *g_array_new(gboolean zero_terminated, gboolean clear_,
                    guint element_size)
{
    return g_array_sized_new(zero_terminated, clear_, element_size, 0);
}

GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear_,
                          guint element_size, guint reserved_size)
{
    RealArray *r = (RealArray *)g_malloc0(sizeof(RealArray));
    r->element_size    = element_size;
    r->zero_terminated = zero_terminated;
    r->clear           = clear_;
    r->ref_count       = 1;

    guint alloc = reserved_size + array_elt_zero_len(r);
    if (alloc > 0) {
        r->data = (gchar *)g_malloc0((gsize)alloc * element_size);
        r->alloc = alloc;
    }

    return (GArray *)r;
}

GArray *g_array_ref(GArray *array)
{
    RealArray *r = ARRAY_REAL(array);
    r->ref_count++;
    return array;
}

void g_array_unref(GArray *array)
{
    RealArray *r = ARRAY_REAL(array);
    if (--r->ref_count == 0) {
        g_array_free(array, TRUE);
    }
}

gchar *g_array_free(GArray *array, gboolean free_segment)
{
    RealArray *r = ARRAY_REAL(array);
    gchar *data = r->data;

    if (free_segment) {
        if (r->clear_func) {
            array_clear_elements(r, 0, r->len);
        }
        g_free(data);
        data = NULL;
    }

    g_free(r);
    return data;
}

GArray *g_array_append_vals(GArray *array, gconstpointer data, guint len)
{
    RealArray *r = ARRAY_REAL(array);
    array_maybe_grow(r, len);
    memcpy(r->data + (gsize)r->len * r->element_size,
           data, (gsize)len * r->element_size);
    r->len += len;
    array_zero_terminate(r);
    return array;
}

GArray *g_array_prepend_vals(GArray *array, gconstpointer data, guint len)
{
    RealArray *r = ARRAY_REAL(array);
    array_maybe_grow(r, len);
    memmove(r->data + (gsize)len * r->element_size,
            r->data,
            (gsize)r->len * r->element_size);
    memcpy(r->data, data, (gsize)len * r->element_size);
    r->len += len;
    array_zero_terminate(r);
    return array;
}

GArray *g_array_insert_vals(GArray *array, guint index_,
                            gconstpointer data, guint len)
{
    RealArray *r = ARRAY_REAL(array);
    if (index_ > r->len) {
        index_ = r->len;
    }
    array_maybe_grow(r, len);
    memmove(r->data + (gsize)(index_ + len) * r->element_size,
            r->data + (gsize)index_ * r->element_size,
            (gsize)(r->len - index_) * r->element_size);
    memcpy(r->data + (gsize)index_ * r->element_size,
           data, (gsize)len * r->element_size);
    r->len += len;
    array_zero_terminate(r);
    return array;
}

GArray *g_array_remove_index(GArray *array, guint index_)
{
    RealArray *r = ARRAY_REAL(array);
    array_clear_elements(r, index_, 1);
    if (index_ + 1 < r->len) {
        memmove(r->data + (gsize)index_ * r->element_size,
                r->data + (gsize)(index_ + 1) * r->element_size,
                (gsize)(r->len - index_ - 1) * r->element_size);
    }
    r->len--;
    array_zero_terminate(r);
    return array;
}

GArray *g_array_remove_index_fast(GArray *array, guint index_)
{
    RealArray *r = ARRAY_REAL(array);
    array_clear_elements(r, index_, 1);
    if (index_ + 1 < r->len) {
        /* Replace with last element */
        memcpy(r->data + (gsize)index_ * r->element_size,
               r->data + (gsize)(r->len - 1) * r->element_size,
               r->element_size);
    }
    r->len--;
    array_zero_terminate(r);
    return array;
}

GArray *g_array_remove_range(GArray *array, guint index_, guint length)
{
    RealArray *r = ARRAY_REAL(array);
    array_clear_elements(r, index_, length);
    if (index_ + length < r->len) {
        memmove(r->data + (gsize)index_ * r->element_size,
                r->data + (gsize)(index_ + length) * r->element_size,
                (gsize)(r->len - index_ - length) * r->element_size);
    }
    r->len -= length;
    array_zero_terminate(r);
    return array;
}

GArray *g_array_set_size(GArray *array, guint length)
{
    RealArray *r = ARRAY_REAL(array);

    if (length > r->len) {
        array_maybe_grow(r, length - r->len);
        if (r->clear) {
            memset(r->data + (gsize)r->len * r->element_size,
                   0,
                   (gsize)(length - r->len) * r->element_size);
        }
    } else if (length < r->len) {
        array_clear_elements(r, length, r->len - length);
    }

    r->len = length;
    array_zero_terminate(r);
    return array;
}

void g_array_set_clear_func(GArray *array, GDestroyNotify clear_func)
{
    ARRAY_REAL(array)->clear_func = clear_func;
}

void g_array_sort(GArray *array, GCompareFunc compare_func)
{
    RealArray *r = ARRAY_REAL(array);
    if (r->len > 0) {
        qsort(r->data, r->len, r->element_size,
              (int (*)(const void *, const void *))compare_func);
    }
}

/* For sort_with_data we need a thread-local context for qsort. */
static GCompareDataFunc g_array_sort_data_func;
static gpointer         g_array_sort_data_user;

static int g_array_sort_data_wrapper(const void *a, const void *b)
{
    return g_array_sort_data_func(a, b, g_array_sort_data_user);
}

void g_array_sort_with_data(GArray *array,
                            GCompareDataFunc compare_func,
                            gpointer user_data)
{
    RealArray *r = ARRAY_REAL(array);
    if (r->len > 0) {
        g_array_sort_data_func = compare_func;
        g_array_sort_data_user = user_data;
        qsort(r->data, r->len, r->element_size,
              g_array_sort_data_wrapper);
    }
}

/* ================================================================== */
/*  GPtrArray internals                                               */
/* ================================================================== */

typedef struct _RealPtrArray {
    gpointer      *pdata;
    guint           len;
    /* --- private --- */
    guint           alloc;
    guint           ref_count;
    GDestroyNotify  element_free_func;
} RealPtrArray;

#define PTR_REAL(a) ((RealPtrArray *)(a))

static void ptr_array_maybe_grow(RealPtrArray *r, guint extra)
{
    guint need = r->len + extra;
    if (need <= r->alloc) {
        return;
    }
    guint new_alloc = r->alloc ? r->alloc : 4;
    while (new_alloc < need) {
        new_alloc *= 2;
    }
    r->pdata = (gpointer *)g_realloc(r->pdata,
                                     (gsize)new_alloc * sizeof(gpointer));
    r->alloc = new_alloc;
}

/* ================================================================== */
/*  GPtrArray public API                                              */
/* ================================================================== */

GPtrArray *g_ptr_array_new(void)
{
    return g_ptr_array_sized_new(0);
}

GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func)
{
    GPtrArray *arr = g_ptr_array_sized_new(0);
    PTR_REAL(arr)->element_free_func = element_free_func;
    return arr;
}

GPtrArray *g_ptr_array_new_full(guint reserved_size,
                                GDestroyNotify element_free_func)
{
    GPtrArray *arr = g_ptr_array_sized_new(reserved_size);
    PTR_REAL(arr)->element_free_func = element_free_func;
    return arr;
}

GPtrArray *g_ptr_array_sized_new(guint reserved_size)
{
    RealPtrArray *r = (RealPtrArray *)g_malloc0(sizeof(RealPtrArray));
    r->ref_count = 1;
    if (reserved_size > 0) {
        r->pdata = (gpointer *)g_malloc0((gsize)reserved_size *
                                         sizeof(gpointer));
        r->alloc = reserved_size;
    }
    return (GPtrArray *)r;
}

GPtrArray *g_ptr_array_ref(GPtrArray *array)
{
    PTR_REAL(array)->ref_count++;
    return array;
}

void g_ptr_array_unref(GPtrArray *array)
{
    RealPtrArray *r = PTR_REAL(array);
    if (--r->ref_count == 0) {
        g_ptr_array_free(array, TRUE);
    }
}

gpointer *g_ptr_array_free(GPtrArray *array, gboolean free_seg)
{
    RealPtrArray *r = PTR_REAL(array);
    gpointer *data = r->pdata;

    if (free_seg) {
        if (r->element_free_func) {
            guint i;
            for (i = 0; i < r->len; i++) {
                if (r->pdata[i]) {
                    r->element_free_func(r->pdata[i]);
                }
            }
        }
        g_free(data);
        data = NULL;
    }

    g_free(r);
    return data;
}

void g_ptr_array_add(GPtrArray *array, gpointer data)
{
    RealPtrArray *r = PTR_REAL(array);
    ptr_array_maybe_grow(r, 1);
    r->pdata[r->len++] = data;
}

void g_ptr_array_insert(GPtrArray *array, gint index_, gpointer data)
{
    RealPtrArray *r = PTR_REAL(array);

    if (index_ < 0 || (guint)index_ >= r->len) {
        g_ptr_array_add(array, data);
        return;
    }

    ptr_array_maybe_grow(r, 1);
    memmove(&r->pdata[index_ + 1], &r->pdata[index_],
            (gsize)(r->len - (guint)index_) * sizeof(gpointer));
    r->pdata[index_] = data;
    r->len++;
}

gboolean g_ptr_array_remove(GPtrArray *array, gpointer data)
{
    guint i;
    RealPtrArray *r = PTR_REAL(array);
    for (i = 0; i < r->len; i++) {
        if (r->pdata[i] == data) {
            g_ptr_array_remove_index(array, i);
            return TRUE;
        }
    }
    return FALSE;
}

gboolean g_ptr_array_remove_fast(GPtrArray *array, gpointer data)
{
    guint i;
    RealPtrArray *r = PTR_REAL(array);
    for (i = 0; i < r->len; i++) {
        if (r->pdata[i] == data) {
            g_ptr_array_remove_index_fast(array, i);
            return TRUE;
        }
    }
    return FALSE;
}

gpointer g_ptr_array_remove_index(GPtrArray *array, guint index_)
{
    RealPtrArray *r = PTR_REAL(array);
    gpointer result = r->pdata[index_];

    if (r->element_free_func) {
        r->element_free_func(result);
    }

    if (index_ + 1 < r->len) {
        memmove(&r->pdata[index_], &r->pdata[index_ + 1],
                (gsize)(r->len - index_ - 1) * sizeof(gpointer));
    }
    r->len--;
    r->pdata[r->len] = NULL;
    return result;
}

gpointer g_ptr_array_remove_index_fast(GPtrArray *array, guint index_)
{
    RealPtrArray *r = PTR_REAL(array);
    gpointer result = r->pdata[index_];

    if (r->element_free_func) {
        r->element_free_func(result);
    }

    if (index_ + 1 < r->len) {
        r->pdata[index_] = r->pdata[r->len - 1];
    }
    r->len--;
    r->pdata[r->len] = NULL;
    return result;
}

GPtrArray *g_ptr_array_remove_range(GPtrArray *array, guint index_,
                                    guint length)
{
    RealPtrArray *r = PTR_REAL(array);

    if (r->element_free_func) {
        guint i;
        for (i = index_; i < index_ + length; i++) {
            if (r->pdata[i]) {
                r->element_free_func(r->pdata[i]);
            }
        }
    }

    if (index_ + length < r->len) {
        memmove(&r->pdata[index_], &r->pdata[index_ + length],
                (gsize)(r->len - index_ - length) * sizeof(gpointer));
    }
    r->len -= length;

    /* Clear trailing slots */
    {
        guint i;
        for (i = r->len; i < r->len + length; i++) {
            r->pdata[i] = NULL;
        }
    }

    return array;
}

void g_ptr_array_set_size(GPtrArray *array, gint length)
{
    RealPtrArray *r = PTR_REAL(array);

    if ((guint)length > r->len) {
        ptr_array_maybe_grow(r, (guint)length - r->len);
        /* Zero new slots */
        memset(&r->pdata[r->len], 0,
               (gsize)((guint)length - r->len) * sizeof(gpointer));
    } else if ((guint)length < r->len) {
        if (r->element_free_func) {
            guint i;
            for (i = (guint)length; i < r->len; i++) {
                if (r->pdata[i]) {
                    r->element_free_func(r->pdata[i]);
                }
            }
        }
    }
    r->len = (guint)length;
}

void g_ptr_array_set_free_func(GPtrArray *array,
                               GDestroyNotify element_free_func)
{
    PTR_REAL(array)->element_free_func = element_free_func;
}

void g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func)
{
    RealPtrArray *r = PTR_REAL(array);
    if (r->len > 0) {
        qsort(r->pdata, r->len, sizeof(gpointer),
              (int (*)(const void *, const void *))compare_func);
    }
}

static GCompareDataFunc g_ptr_sort_data_func;
static gpointer         g_ptr_sort_data_user;

static int g_ptr_sort_data_wrapper(const void *a, const void *b)
{
    return g_ptr_sort_data_func(a, b, g_ptr_sort_data_user);
}

void g_ptr_array_sort_with_data(GPtrArray *array,
                                GCompareDataFunc compare_func,
                                gpointer user_data)
{
    RealPtrArray *r = PTR_REAL(array);
    if (r->len > 0) {
        g_ptr_sort_data_func = compare_func;
        g_ptr_sort_data_user = user_data;
        qsort(r->pdata, r->len, sizeof(gpointer),
              g_ptr_sort_data_wrapper);
    }
}

void g_ptr_array_foreach(GPtrArray *array, GFunc func,
                         gpointer user_data)
{
    guint i;
    for (i = 0; i < array->len; i++) {
        func(array->pdata[i], user_data);
    }
}

gboolean g_ptr_array_find(GPtrArray *haystack, gconstpointer needle,
                          guint *index_)
{
    guint i;
    for (i = 0; i < haystack->len; i++) {
        if (haystack->pdata[i] == needle) {
            if (index_) {
                *index_ = i;
            }
            return TRUE;
        }
    }
    return FALSE;
}

gboolean g_ptr_array_find_with_equal_func(GPtrArray *haystack,
                                          gconstpointer needle,
                                          GEqualFunc equal_func,
                                          guint *index_)
{
    guint i;
    for (i = 0; i < haystack->len; i++) {
        if (equal_func) {
            if (equal_func(haystack->pdata[i], needle)) {
                if (index_) {
                    *index_ = i;
                }
                return TRUE;
            }
        } else {
            if (haystack->pdata[i] == needle) {
                if (index_) {
                    *index_ = i;
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

gpointer g_ptr_array_steal_index(GPtrArray *array, guint index_)
{
    RealPtrArray *r = PTR_REAL(array);
    gpointer result = r->pdata[index_];

    if (index_ + 1 < r->len) {
        memmove(&r->pdata[index_], &r->pdata[index_ + 1],
                (gsize)(r->len - index_ - 1) * sizeof(gpointer));
    }
    r->len--;
    r->pdata[r->len] = NULL;
    return result;
}

gpointer g_ptr_array_steal_index_fast(GPtrArray *array, guint index_)
{
    RealPtrArray *r = PTR_REAL(array);
    gpointer result = r->pdata[index_];

    if (index_ + 1 < r->len) {
        r->pdata[index_] = r->pdata[r->len - 1];
    }
    r->len--;
    r->pdata[r->len] = NULL;
    return result;
}

/* ================================================================== */
/*  GByteArray internals                                              */
/* ================================================================== */

typedef struct _RealByteArray {
    guint8 *data;
    guint   len;
    /* --- private --- */
    guint   alloc;
    guint   ref_count;
} RealByteArray;

#define BYTE_REAL(a) ((RealByteArray *)(a))

static void byte_array_maybe_grow(RealByteArray *r, guint extra)
{
    guint need = r->len + extra;
    if (need <= r->alloc) {
        return;
    }
    guint new_alloc = r->alloc ? r->alloc : 16;
    while (new_alloc < need) {
        new_alloc *= 2;
    }
    r->data = (guint8 *)g_realloc(r->data, (gsize)new_alloc);
    r->alloc = new_alloc;
}

/* ================================================================== */
/*  GByteArray public API                                             */
/* ================================================================== */

GByteArray *g_byte_array_new(void)
{
    return g_byte_array_sized_new(0);
}

GByteArray *g_byte_array_sized_new(guint reserved_size)
{
    RealByteArray *r = (RealByteArray *)g_malloc0(sizeof(RealByteArray));
    r->ref_count = 1;
    if (reserved_size > 0) {
        r->data = (guint8 *)g_malloc(reserved_size);
        r->alloc = reserved_size;
    }
    return (GByteArray *)r;
}

GByteArray *g_byte_array_ref(GByteArray *array)
{
    BYTE_REAL(array)->ref_count++;
    return array;
}

void g_byte_array_unref(GByteArray *array)
{
    RealByteArray *r = BYTE_REAL(array);
    if (--r->ref_count == 0) {
        g_byte_array_free(array, TRUE);
    }
}

guint8 *g_byte_array_free(GByteArray *array, gboolean free_segment)
{
    RealByteArray *r = BYTE_REAL(array);
    guint8 *data = r->data;

    if (free_segment) {
        g_free(data);
        data = NULL;
    }

    g_free(r);
    return data;
}

GByteArray *g_byte_array_append(GByteArray *array, const guint8 *data,
                                guint len)
{
    RealByteArray *r = BYTE_REAL(array);
    byte_array_maybe_grow(r, len);
    memcpy(r->data + r->len, data, len);
    r->len += len;
    return array;
}

GByteArray *g_byte_array_prepend(GByteArray *array, const guint8 *data,
                                 guint len)
{
    RealByteArray *r = BYTE_REAL(array);
    byte_array_maybe_grow(r, len);
    memmove(r->data + len, r->data, r->len);
    memcpy(r->data, data, len);
    r->len += len;
    return array;
}

GByteArray *g_byte_array_set_size(GByteArray *array, guint length)
{
    RealByteArray *r = BYTE_REAL(array);
    if (length > r->len) {
        byte_array_maybe_grow(r, length - r->len);
        memset(r->data + r->len, 0, length - r->len);
    }
    r->len = length;
    return array;
}

GByteArray *g_byte_array_remove_index(GByteArray *array, guint index_)
{
    RealByteArray *r = BYTE_REAL(array);
    if (index_ + 1 < r->len) {
        memmove(r->data + index_, r->data + index_ + 1,
                r->len - index_ - 1);
    }
    r->len--;
    return array;
}

GByteArray *g_byte_array_remove_index_fast(GByteArray *array, guint index_)
{
    RealByteArray *r = BYTE_REAL(array);
    if (index_ + 1 < r->len) {
        r->data[index_] = r->data[r->len - 1];
    }
    r->len--;
    return array;
}

GByteArray *g_byte_array_remove_range(GByteArray *array, guint index_,
                                      guint length)
{
    RealByteArray *r = BYTE_REAL(array);
    if (index_ + length < r->len) {
        memmove(r->data + index_, r->data + index_ + length,
                r->len - index_ - length);
    }
    r->len -= length;
    return array;
}

/* ================================================================== */
/*  GArray additional API                                             */
/* ================================================================== */

guint g_array_get_element_size(GArray *array)
{
    return ARRAY_REAL(array)->element_size;
}

void g_byte_array_sort(GByteArray *array, GCompareFunc compare_func)
{
    RealByteArray *r = BYTE_REAL(array);
    if (r->len > 0) {
        qsort(r->data, r->len, sizeof(guint8),
              (int (*)(const void *, const void *))compare_func);
    }
}
