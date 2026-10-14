/*
 * Minimal GString implementation for QEMU wasm32-wasi.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "../glib/glib.h"
#include "../glib/gstring.h"

/* ------------------------------------------------------------------ */
/*  GBytes — simplified stub                                          */
/* ------------------------------------------------------------------ */

struct _GBytes {
    gpointer data;
    gsize    size;
    gint     ref_count;
    GDestroyNotify free_func;
};

GBytes *
g_bytes_new(gconstpointer data, gsize size)
{
    GBytes *bytes = (GBytes *)g_malloc(sizeof(GBytes));
    bytes->data = g_malloc(size);
    memcpy(bytes->data, data, size);
    bytes->size = size;
    bytes->ref_count = 1;
    bytes->free_func = g_free;
    return bytes;
}

GBytes *
g_bytes_new_take(gpointer data, gsize size)
{
    GBytes *bytes = (GBytes *)g_malloc(sizeof(GBytes));
    bytes->data = data;
    bytes->size = size;
    bytes->ref_count = 1;
    bytes->free_func = g_free;
    return bytes;
}

gconstpointer
g_bytes_get_data(GBytes *bytes, gsize *size)
{
    if (size) {
        *size = bytes->size;
    }
    return bytes->data;
}

gsize
g_bytes_get_size(GBytes *bytes)
{
    return bytes->size;
}

GBytes *
g_bytes_ref(GBytes *bytes)
{
    bytes->ref_count++;
    return bytes;
}

void
g_bytes_unref(GBytes *bytes)
{
    if (bytes == NULL) {
        return;
    }
    bytes->ref_count--;
    if (bytes->ref_count <= 0) {
        if (bytes->free_func && bytes->data) {
            bytes->free_func(bytes->data);
        }
        g_free(bytes);
    }
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static void
g_string_maybe_expand(GString *string, gsize len)
{
    gsize need = string->len + len + 1;
    if (need <= string->allocated_len) {
        return;
    }
    /* Grow by doubling or to exactly what's needed, whichever is larger */
    gsize new_alloc = string->allocated_len ? string->allocated_len : 2;
    while (new_alloc < need) {
        new_alloc *= 2;
    }
    string->str = (gchar *)g_realloc(string->str, new_alloc);
    string->allocated_len = new_alloc;
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

GString *
g_string_sized_new(gsize dfl_size)
{
    GString *string = g_new(GString, 1);
    gsize alloc = dfl_size > 0 ? dfl_size + 1 : 2;
    string->str = (gchar *)g_malloc(alloc);
    string->str[0] = '\0';
    string->len = 0;
    string->allocated_len = alloc;
    return string;
}

GString *
g_string_new(const gchar *init)
{
    GString *string;
    if (init == NULL || *init == '\0') {
        string = g_string_sized_new(2);
    } else {
        gsize len = strlen(init);
        string = g_string_sized_new(len);
        g_string_append_len(string, init, (gssize)len);
    }
    return string;
}

GString *
g_string_new_len(const gchar *init, gssize len)
{
    if (init == NULL) {
        return g_string_sized_new(len >= 0 ? (gsize)len : 2);
    }
    if (len < 0) {
        len = (gssize)strlen(init);
    }
    GString *string = g_string_sized_new((gsize)len);
    g_string_append_len(string, init, len);
    return string;
}

gchar *
g_string_free(GString *string, gboolean free_segment)
{
    gchar *segment;

    if (string == NULL) {
        return NULL;
    }

    if (free_segment) {
        g_free(string->str);
        segment = NULL;
    } else {
        segment = string->str;
    }
    g_free(string);
    return segment;
}

GBytes *
g_string_free_to_bytes(GString *string)
{
    gsize len = string->len;
    gchar *buf = g_string_free(string, FALSE);
    return g_bytes_new_take(buf, len);
}

/* ------------------------------------------------------------------ */
/*  Assign / truncate / set_size                                      */
/* ------------------------------------------------------------------ */

GString *
g_string_assign(GString *string, const gchar *rval)
{
    g_string_truncate(string, 0);
    g_string_append(string, rval);
    return string;
}

GString *
g_string_truncate(GString *string, gsize len)
{
    if (len < string->len) {
        string->len = len;
        string->str[len] = '\0';
    }
    return string;
}

GString *
g_string_set_size(GString *string, gsize len)
{
    if (len >= string->allocated_len) {
        g_string_maybe_expand(string, len - string->len);
    }
    if (len > string->len) {
        memset(string->str + string->len, 0, len - string->len);
    }
    string->len = len;
    string->str[len] = '\0';
    return string;
}

/* ------------------------------------------------------------------ */
/*  Append                                                            */
/* ------------------------------------------------------------------ */

GString *
g_string_append(GString *string, const gchar *val)
{
    return g_string_append_len(string, val, -1);
}

GString *
g_string_append_len(GString *string, const gchar *val, gssize len)
{
    if (len < 0) {
        len = (gssize)strlen(val);
    }
    g_string_maybe_expand(string, (gsize)len);
    memcpy(string->str + string->len, val, (gsize)len);
    string->len += (gsize)len;
    string->str[string->len] = '\0';
    return string;
}

GString *
g_string_append_c(GString *string, gchar c)
{
    g_string_maybe_expand(string, 1);
    string->str[string->len++] = c;
    string->str[string->len] = '\0';
    return string;
}

GString *
g_string_append_unichar(GString *string, guint32 wc)
{
    gchar buf[6];
    gsize len;

    if (wc < 0x80) {
        buf[0] = (gchar)wc;
        len = 1;
    } else if (wc < 0x800) {
        buf[0] = (gchar)(0xC0 | (wc >> 6));
        buf[1] = (gchar)(0x80 | (wc & 0x3F));
        len = 2;
    } else if (wc < 0x10000) {
        buf[0] = (gchar)(0xE0 | (wc >> 12));
        buf[1] = (gchar)(0x80 | ((wc >> 6) & 0x3F));
        buf[2] = (gchar)(0x80 | (wc & 0x3F));
        len = 3;
    } else if (wc < 0x200000) {
        buf[0] = (gchar)(0xF0 | (wc >> 18));
        buf[1] = (gchar)(0x80 | ((wc >> 12) & 0x3F));
        buf[2] = (gchar)(0x80 | ((wc >> 6) & 0x3F));
        buf[3] = (gchar)(0x80 | (wc & 0x3F));
        len = 4;
    } else {
        /* Invalid codepoint — encode as replacement character U+FFFD */
        buf[0] = (gchar)0xEF;
        buf[1] = (gchar)0xBF;
        buf[2] = (gchar)0xBD;
        len = 3;
    }

    return g_string_append_len(string, buf, (gssize)len);
}

/* ------------------------------------------------------------------ */
/*  Prepend                                                           */
/* ------------------------------------------------------------------ */

GString *
g_string_prepend(GString *string, const gchar *val)
{
    return g_string_prepend_len(string, val, -1);
}

GString *
g_string_prepend_len(GString *string, const gchar *val, gssize len)
{
    return g_string_insert_len(string, 0, val, len);
}

GString *
g_string_prepend_c(GString *string, gchar c)
{
    return g_string_insert_c(string, 0, c);
}

/* ------------------------------------------------------------------ */
/*  Insert                                                            */
/* ------------------------------------------------------------------ */

GString *
g_string_insert(GString *string, gssize pos, const gchar *val)
{
    return g_string_insert_len(string, pos, val, -1);
}

GString *
g_string_insert_len(GString *string, gssize pos,
                    const gchar *val, gssize len)
{
    if (len < 0) {
        len = (gssize)strlen(val);
    }
    if (pos < 0) {
        pos = (gssize)string->len;
    }
    if ((gsize)pos > string->len) {
        pos = (gssize)string->len;
    }

    g_string_maybe_expand(string, (gsize)len);

    /* Move existing data to make room */
    memmove(string->str + pos + len,
            string->str + pos,
            string->len - (gsize)pos);
    memcpy(string->str + pos, val, (gsize)len);
    string->len += (gsize)len;
    string->str[string->len] = '\0';

    return string;
}

GString *
g_string_insert_c(GString *string, gssize pos, gchar c)
{
    gchar buf[1];
    buf[0] = c;
    return g_string_insert_len(string, pos, buf, 1);
}

/* ------------------------------------------------------------------ */
/*  Erase                                                             */
/* ------------------------------------------------------------------ */

GString *
g_string_erase(GString *string, gssize pos, gssize len)
{
    if (pos < 0 || (gsize)pos >= string->len) {
        return string;
    }
    if (len < 0 || (gsize)(pos + len) >= string->len) {
        /* Erase to end */
        string->len = (gsize)pos;
    } else {
        memmove(string->str + pos,
                string->str + pos + len,
                string->len - (gsize)(pos + len));
        string->len -= (gsize)len;
    }
    string->str[string->len] = '\0';
    return string;
}

/* ------------------------------------------------------------------ */
/*  printf-style                                                      */
/* ------------------------------------------------------------------ */

void
g_string_vprintf(GString *string, const gchar *format, va_list args)
{
    g_string_truncate(string, 0);
    g_string_append_vprintf(string, format, args);
}

void
g_string_append_vprintf(GString *string, const gchar *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    /* Determine required size */
    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len <= 0) {
        return;
    }

    g_string_maybe_expand(string, (gsize)len);
    vsnprintf(string->str + string->len, (gsize)len + 1, format, args);
    string->len += (gsize)len;
}

void
g_string_printf(GString *string, const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    g_string_vprintf(string, format, args);
    va_end(args);
}

void
g_string_append_printf(GString *string, const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    g_string_append_vprintf(string, format, args);
    va_end(args);
}

/* ------------------------------------------------------------------ */
/*  Comparison / hashing                                              */
/* ------------------------------------------------------------------ */

gboolean
g_string_equal(const GString *v, const GString *v2)
{
    if (v->len != v2->len) {
        return FALSE;
    }
    return memcmp(v->str, v2->str, v->len) == 0;
}

guint
g_string_hash(const GString *str)
{
    /* djb2 hash */
    const gchar *p = str->str;
    guint h = 5381;
    gsize i;
    for (i = 0; i < str->len; i++) {
        h = (h << 5) + h + (guchar)p[i];
    }
    return h;
}
