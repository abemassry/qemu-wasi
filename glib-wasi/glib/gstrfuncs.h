/*
 * GLib string utility functions shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GSTRFUNCS_H
#define GLIB_WASI_GSTRFUNCS_H

#include <stdarg.h>
#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  String duplication / creation                                     */
/* ------------------------------------------------------------------ */

gchar  *g_strdup        (const gchar *str);
gchar  *g_strndup       (const gchar *str, gsize n);
gchar  *g_strdup_printf (const gchar *format, ...) G_GNUC_PRINTF(1, 2);
gchar  *g_strdup_vprintf(const gchar *format, va_list args);
gchar  *g_strnfill      (gsize length, gchar fill_char);

/* ------------------------------------------------------------------ */
/*  Concatenation / joining                                           */
/* ------------------------------------------------------------------ */

gchar  *g_strconcat     (const gchar *string1, ...) G_GNUC_NULL_TERMINATED;
gchar  *g_strjoin       (const gchar *separator, ...) G_GNUC_NULL_TERMINATED;
gchar  *g_strjoinv      (const gchar *separator, gchar **str_array);

/* ------------------------------------------------------------------ */
/*  Splitting                                                         */
/* ------------------------------------------------------------------ */

gchar **g_strsplit      (const gchar *string, const gchar *delimiter,
                         gint max_tokens);
gchar **g_strsplit_set  (const gchar *string, const gchar *delimiters,
                         gint max_tokens);

/* ------------------------------------------------------------------ */
/*  String arrays                                                     */
/* ------------------------------------------------------------------ */

void     g_strfreev     (gchar **str_array);
guint    g_strv_length  (gchar **str_array);
gboolean g_strv_contains(const gchar *const *strv, const gchar *str);

/* ------------------------------------------------------------------ */
/*  Whitespace stripping                                              */
/* ------------------------------------------------------------------ */

gchar  *g_strchug       (gchar *string);
gchar  *g_strchomp      (gchar *string);
#define g_strstrip(string)  g_strchomp(g_strchug(string))

/* ------------------------------------------------------------------ */
/*  Miscellaneous                                                     */
/* ------------------------------------------------------------------ */

gchar  *g_strreverse    (gchar *string);
gchar  *g_strup         (gchar *string);
gchar  *g_strdown       (gchar *string);

gint    g_strcmp0        (const gchar *str1, const gchar *str2);
gboolean g_str_has_prefix(const gchar *str, const gchar *prefix);
gboolean g_str_has_suffix(const gchar *str, const gchar *suffix);

/* ------------------------------------------------------------------ */
/*  ASCII case conversion / classification                            */
/* ------------------------------------------------------------------ */

gchar  *g_ascii_strup       (const gchar *str, gssize len);
gchar  *g_ascii_strdown     (const gchar *str, gssize len);
gint    g_ascii_strcasecmp  (const gchar *s1, const gchar *s2);
gint    g_ascii_strncasecmp (const gchar *s1, const gchar *s2, gsize n);

static inline gboolean g_ascii_isalpha (gchar c)  { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline gboolean g_ascii_isupper (gchar c)  { return c >= 'A' && c <= 'Z'; }
static inline gboolean g_ascii_islower (gchar c)  { return c >= 'a' && c <= 'z'; }
static inline gboolean g_ascii_isdigit (gchar c)  { return c >= '0' && c <= '9'; }
static inline gboolean g_ascii_isxdigit(gchar c)  {
    return g_ascii_isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
static inline gboolean g_ascii_isalnum (gchar c)  { return g_ascii_isalpha(c) || g_ascii_isdigit(c); }
static inline gboolean g_ascii_isspace (gchar c)  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static inline gchar    g_ascii_toupper (gchar c)  { return g_ascii_islower(c) ? c - 32 : c; }
static inline gchar    g_ascii_tolower (gchar c)  { return g_ascii_isupper(c) ? c + 32 : c; }
static inline gint     g_ascii_xdigit_value(gchar c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static inline gint     g_ascii_digit_value(gchar c) {
    if (c >= '0' && c <= '9') return c - '0';
    return -1;
}

/* ------------------------------------------------------------------ */
/*  ASCII number parsing                                              */
/* ------------------------------------------------------------------ */

guint64  g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base);
gint64   g_ascii_strtoll (const gchar *nptr, gchar **endptr, guint base);
gdouble  g_ascii_strtod  (const gchar *nptr, gchar **endptr);
gchar   *g_ascii_dtostr  (gchar *buffer, gint buf_len, gdouble d);
gchar   *g_ascii_formatd (gchar *buffer, gint buf_len,
                           const gchar *format, gdouble d);

/* ------------------------------------------------------------------ */
/*  Formatted printing                                                */
/* ------------------------------------------------------------------ */

gint     g_snprintf      (gchar *string, gulong n,
                           const gchar *format, ...) G_GNUC_PRINTF(3, 4);
gint     g_vsnprintf     (gchar *string, gulong n,
                           const gchar *format, va_list args);
gint     g_vasprintf     (gchar **strp, const gchar *fmt, va_list ap);

/* ------------------------------------------------------------------ */
/*  Error string                                                      */
/* ------------------------------------------------------------------ */

const gchar *g_strerror   (gint errnum);

/* ------------------------------------------------------------------ */
/*  Safe string operations                                            */
/* ------------------------------------------------------------------ */

gsize    g_strlcpy        (gchar *dest, const gchar *src, gsize dest_size);
gsize    g_strlcat        (gchar *dest, const gchar *src, gsize dest_size);
gchar   *g_stpcpy         (gchar *dest, const gchar *src);

/* ------------------------------------------------------------------ */
/*  Substring search                                                  */
/* ------------------------------------------------------------------ */

gchar   *g_strstr_len     (const gchar *haystack, gssize haystack_len,
                            const gchar *needle);
gchar   *g_strrstr        (const gchar *haystack, const gchar *needle);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GSTRFUNCS_H */
