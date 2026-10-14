/*
 * GLib GString shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GSTRING_H
#define GLIB_WASI_GSTRING_H

#include <stdarg.h>
#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  GBytes — opaque wrapper (simplified stub)                         */
/* ------------------------------------------------------------------ */

typedef struct _GBytes GBytes;

GBytes       *g_bytes_new          (gconstpointer data, gsize size);
GBytes       *g_bytes_new_take     (gpointer data, gsize size);
gconstpointer g_bytes_get_data     (GBytes *bytes, gsize *size);
gsize         g_bytes_get_size     (GBytes *bytes);
GBytes       *g_bytes_ref          (GBytes *bytes);
void          g_bytes_unref        (GBytes *bytes);

/* ------------------------------------------------------------------ */
/*  GString — dynamically-growing string buffer                       */
/* ------------------------------------------------------------------ */

typedef struct _GString {
    gchar *str;
    gsize  len;
    gsize  allocated_len;
} GString;

/* Lifecycle */
GString *g_string_new          (const gchar *init);
GString *g_string_new_len      (const gchar *init, gssize len);
GString *g_string_sized_new    (gsize dfl_size);
gchar   *g_string_free         (GString *string, gboolean free_segment);
GBytes  *g_string_free_to_bytes(GString *string);

/* Assign / set */
GString *g_string_assign       (GString *string, const gchar *rval);
GString *g_string_truncate     (GString *string, gsize len);
GString *g_string_set_size     (GString *string, gsize len);

/* Append */
GString *g_string_append       (GString *string, const gchar *val);
GString *g_string_append_c     (GString *string, gchar c);
GString *g_string_append_len   (GString *string, const gchar *val, gssize len);
GString *g_string_append_unichar(GString *string, guint32 wc);

/* Prepend */
GString *g_string_prepend      (GString *string, const gchar *val);
GString *g_string_prepend_c    (GString *string, gchar c);
GString *g_string_prepend_len  (GString *string, const gchar *val, gssize len);

/* Insert */
GString *g_string_insert       (GString *string, gssize pos, const gchar *val);
GString *g_string_insert_c     (GString *string, gssize pos, gchar c);
GString *g_string_insert_len   (GString *string, gssize pos,
                                const gchar *val, gssize len);

/* Erase */
GString *g_string_erase        (GString *string, gssize pos, gssize len);

/* printf-style */
void     g_string_printf       (GString *string, const gchar *format, ...)
                                G_GNUC_PRINTF(2, 3);
void     g_string_append_printf(GString *string, const gchar *format, ...)
                                G_GNUC_PRINTF(2, 3);
void     g_string_vprintf      (GString *string, const gchar *format,
                                va_list args);
void     g_string_append_vprintf(GString *string, const gchar *format,
                                 va_list args);

/* Comparison / hashing */
gboolean g_string_equal        (const GString *v, const GString *v2);
guint    g_string_hash         (const GString *str);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GSTRING_H */
