/*
 * Minimal GLib string utility functions for QEMU wasm32-wasi.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include "../glib/glib.h"
#include "../glib/gstrfuncs.h"

/* ------------------------------------------------------------------ */
/*  String duplication / creation                                     */
/* ------------------------------------------------------------------ */

gchar *
g_strdup(const gchar *str)
{
    if (str == NULL) {
        return NULL;
    }
    gsize len = strlen(str) + 1;
    gchar *dup = (gchar *)g_malloc(len);
    memcpy(dup, str, len);
    return dup;
}

gchar *
g_strndup(const gchar *str, gsize n)
{
    if (str == NULL) {
        return NULL;
    }
    /* Find actual length (may be shorter than n) */
    gsize len = 0;
    while (len < n && str[len] != '\0') {
        len++;
    }
    gchar *dup = (gchar *)g_malloc(len + 1);
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

gchar *
g_strdup_vprintf(const gchar *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) {
        return g_strdup("");
    }

    gchar *result = (gchar *)g_malloc((gsize)len + 1);
    vsnprintf(result, (gsize)len + 1, format, args);
    return result;
}

gchar *
g_strdup_printf(const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    gchar *result = g_strdup_vprintf(format, args);
    va_end(args);
    return result;
}

gchar *
g_strnfill(gsize length, gchar fill_char)
{
    gchar *result = (gchar *)g_malloc(length + 1);
    memset(result, fill_char, length);
    result[length] = '\0';
    return result;
}

/* ------------------------------------------------------------------ */
/*  Concatenation / joining                                           */
/* ------------------------------------------------------------------ */

gchar *
g_strconcat(const gchar *string1, ...)
{
    if (string1 == NULL) {
        return g_strdup("");
    }

    /* First pass: compute total length */
    va_list args;
    va_start(args, string1);
    gsize total = strlen(string1);
    const gchar *s;
    while ((s = va_arg(args, const gchar *)) != NULL) {
        total += strlen(s);
    }
    va_end(args);

    /* Second pass: copy */
    gchar *result = (gchar *)g_malloc(total + 1);
    gchar *dest = result;
    gsize len = strlen(string1);
    memcpy(dest, string1, len);
    dest += len;

    va_start(args, string1);
    while ((s = va_arg(args, const gchar *)) != NULL) {
        len = strlen(s);
        memcpy(dest, s, len);
        dest += len;
    }
    va_end(args);

    *dest = '\0';
    return result;
}

gchar *
g_strjoin(const gchar *separator, ...)
{
    if (separator == NULL) {
        separator = "";
    }
    gsize sep_len = strlen(separator);

    va_list args;
    va_start(args, separator);

    /* First pass: compute total length */
    const gchar *s;
    gsize total = 0;
    gint count = 0;
    va_list args_copy;
    va_copy(args_copy, args);

    while ((s = va_arg(args_copy, const gchar *)) != NULL) {
        total += strlen(s);
        count++;
    }
    va_end(args_copy);

    if (count == 0) {
        va_end(args);
        return g_strdup("");
    }

    total += sep_len * (gsize)(count - 1);

    /* Second pass: copy */
    gchar *result = (gchar *)g_malloc(total + 1);
    gchar *dest = result;
    gint i = 0;

    while ((s = va_arg(args, const gchar *)) != NULL) {
        if (i > 0 && sep_len > 0) {
            memcpy(dest, separator, sep_len);
            dest += sep_len;
        }
        gsize len = strlen(s);
        memcpy(dest, s, len);
        dest += len;
        i++;
    }
    va_end(args);

    *dest = '\0';
    return result;
}

gchar *
g_strjoinv(const gchar *separator, gchar **str_array)
{
    if (str_array == NULL || str_array[0] == NULL) {
        return g_strdup("");
    }
    if (separator == NULL) {
        separator = "";
    }
    gsize sep_len = strlen(separator);

    /* Compute total length */
    gsize total = 0;
    guint count = 0;
    for (guint i = 0; str_array[i] != NULL; i++) {
        total += strlen(str_array[i]);
        count++;
    }
    total += sep_len * (gsize)(count - 1);

    gchar *result = (gchar *)g_malloc(total + 1);
    gchar *dest = result;

    for (guint i = 0; str_array[i] != NULL; i++) {
        if (i > 0 && sep_len > 0) {
            memcpy(dest, separator, sep_len);
            dest += sep_len;
        }
        gsize len = strlen(str_array[i]);
        memcpy(dest, str_array[i], len);
        dest += len;
    }
    *dest = '\0';
    return result;
}

/* ------------------------------------------------------------------ */
/*  Splitting                                                         */
/* ------------------------------------------------------------------ */

gchar **
g_strsplit(const gchar *string, const gchar *delimiter, gint max_tokens)
{
    if (string == NULL || delimiter == NULL || delimiter[0] == '\0') {
        gchar **result = g_new(gchar *, 1);
        result[0] = NULL;
        return result;
    }

    /* Real GLib returns an empty array when the input string is empty */
    if (string[0] == '\0') {
        gchar **result = g_new(gchar *, 1);
        result[0] = NULL;
        return result;
    }

    if (max_tokens < 1) {
        max_tokens = G_MAXINT;
    }

    gsize delim_len = strlen(delimiter);
    /* Count tokens (first pass) */
    gint n_tokens = 0;
    const gchar *remainder = string;

    while (remainder) {
        const gchar *found = strstr(remainder, delimiter);
        n_tokens++;
        if (found == NULL || n_tokens >= max_tokens) {
            break;
        }
        remainder = found + delim_len;
    }

    gchar **result = g_new(gchar *, (gsize)(n_tokens + 1));

    /* Second pass: extract tokens */
    remainder = string;
    gint i = 0;
    while (remainder && i < n_tokens) {
        if (i == max_tokens - 1) {
            /* Last token gets the remainder */
            result[i] = g_strdup(remainder);
            i++;
            break;
        }
        const gchar *found = strstr(remainder, delimiter);
        if (found == NULL) {
            result[i] = g_strdup(remainder);
            i++;
            break;
        }
        result[i] = g_strndup(remainder, (gsize)(found - remainder));
        i++;
        remainder = found + delim_len;
    }
    result[i] = NULL;
    return result;
}

gchar **
g_strsplit_set(const gchar *string, const gchar *delimiters, gint max_tokens)
{
    if (string == NULL || delimiters == NULL || delimiters[0] == '\0') {
        gchar **result = g_new(gchar *, 2);
        result[0] = string ? g_strdup(string) : NULL;
        result[string ? 1 : 0] = NULL;
        return result;
    }

    if (max_tokens < 1) {
        max_tokens = G_MAXINT;
    }

    /* Build a delimiter lookup table for O(1) checking */
    gboolean is_delim[256];
    memset(is_delim, 0, sizeof(is_delim));
    for (const gchar *d = delimiters; *d; d++) {
        is_delim[(guchar)*d] = TRUE;
    }

    /* Count tokens (first pass) */
    gint n_tokens = 0;
    const gchar *p = string;
    gboolean in_token = FALSE;

    while (*p) {
        if (is_delim[(guchar)*p]) {
            n_tokens++; /* empty token before/between delimiters */
            if (n_tokens >= max_tokens) {
                break;
            }
        }
        p++;
    }
    if (n_tokens < max_tokens) {
        n_tokens++; /* trailing token */
    }
    (void)in_token;

    /* Second pass: extract tokens */
    gchar **result = g_new(gchar *, (gsize)(n_tokens + 1));
    p = string;
    gint i = 0;

    while (*p && i < n_tokens) {
        if (i == max_tokens - 1) {
            result[i] = g_strdup(p);
            i++;
            break;
        }
        const gchar *start = p;
        while (*p && !is_delim[(guchar)*p]) {
            p++;
        }
        result[i] = g_strndup(start, (gsize)(p - start));
        i++;
        if (*p) {
            p++; /* skip delimiter */
        }
    }
    result[i] = NULL;
    return result;
}

/* ------------------------------------------------------------------ */
/*  String arrays                                                     */
/* ------------------------------------------------------------------ */

void
g_strfreev(gchar **str_array)
{
    if (str_array == NULL) {
        return;
    }
    for (gsize i = 0; str_array[i] != NULL; i++) {
        g_free(str_array[i]);
    }
    g_free(str_array);
}

guint
g_strv_length(gchar **str_array)
{
    guint len = 0;
    if (str_array) {
        while (str_array[len] != NULL) {
            len++;
        }
    }
    return len;
}

gboolean
g_strv_contains(const gchar *const *strv, const gchar *str)
{
    if (strv == NULL || str == NULL) {
        return FALSE;
    }
    for (gsize i = 0; strv[i] != NULL; i++) {
        if (strcmp(strv[i], str) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Whitespace stripping                                              */
/* ------------------------------------------------------------------ */

gchar *
g_strchug(gchar *string)
{
    if (string == NULL) {
        return NULL;
    }
    gchar *start = string;
    while (*start && g_ascii_isspace(*start)) {
        start++;
    }
    if (start != string) {
        memmove(string, start, strlen(start) + 1);
    }
    return string;
}

gchar *
g_strchomp(gchar *string)
{
    if (string == NULL) {
        return NULL;
    }
    gsize len = strlen(string);
    while (len > 0 && g_ascii_isspace(string[len - 1])) {
        len--;
    }
    string[len] = '\0';
    return string;
}

/* ------------------------------------------------------------------ */
/*  Miscellaneous                                                     */
/* ------------------------------------------------------------------ */

gchar *
g_strreverse(gchar *string)
{
    if (string == NULL || *string == '\0') {
        return string;
    }
    gsize len = strlen(string);
    gsize i = 0;
    gsize j = len - 1;
    while (i < j) {
        gchar tmp = string[i];
        string[i] = string[j];
        string[j] = tmp;
        i++;
        j--;
    }
    return string;
}

gchar *
g_strup(gchar *string)
{
    if (string == NULL) {
        return NULL;
    }
    for (gchar *p = string; *p; p++) {
        *p = g_ascii_toupper(*p);
    }
    return string;
}

gchar *
g_strdown(gchar *string)
{
    if (string == NULL) {
        return NULL;
    }
    for (gchar *p = string; *p; p++) {
        *p = g_ascii_tolower(*p);
    }
    return string;
}

gint
g_strcmp0(const gchar *str1, const gchar *str2)
{
    if (str1 == NULL && str2 == NULL) {
        return 0;
    }
    if (str1 == NULL) {
        return -1;
    }
    if (str2 == NULL) {
        return 1;
    }
    return strcmp(str1, str2);
}

gboolean
g_str_has_prefix(const gchar *str, const gchar *prefix)
{
    if (str == NULL || prefix == NULL) {
        return FALSE;
    }
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

gboolean
g_str_has_suffix(const gchar *str, const gchar *suffix)
{
    if (str == NULL || suffix == NULL) {
        return FALSE;
    }
    gsize str_len = strlen(str);
    gsize suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return FALSE;
    }
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/* ------------------------------------------------------------------ */
/*  ASCII case conversion                                             */
/* ------------------------------------------------------------------ */

gchar *
g_ascii_strup(const gchar *str, gssize len)
{
    if (str == NULL) {
        return NULL;
    }
    if (len < 0) {
        len = (gssize)strlen(str);
    }
    gchar *result = (gchar *)g_malloc((gsize)len + 1);
    for (gssize i = 0; i < len; i++) {
        result[i] = g_ascii_toupper(str[i]);
    }
    result[len] = '\0';
    return result;
}

gchar *
g_ascii_strdown(const gchar *str, gssize len)
{
    if (str == NULL) {
        return NULL;
    }
    if (len < 0) {
        len = (gssize)strlen(str);
    }
    gchar *result = (gchar *)g_malloc((gsize)len + 1);
    for (gssize i = 0; i < len; i++) {
        result[i] = g_ascii_tolower(str[i]);
    }
    result[len] = '\0';
    return result;
}

gint
g_ascii_strcasecmp(const gchar *s1, const gchar *s2)
{
    if (s1 == NULL || s2 == NULL) {
        if (s1 == s2) return 0;
        return s1 ? 1 : -1;
    }
    while (*s1 && *s2) {
        gchar c1 = g_ascii_tolower(*s1);
        gchar c2 = g_ascii_tolower(*s2);
        if (c1 != c2) {
            return (guchar)c1 - (guchar)c2;
        }
        s1++;
        s2++;
    }
    return (guchar)g_ascii_tolower(*s1) - (guchar)g_ascii_tolower(*s2);
}

gint
g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n)
{
    if (s1 == NULL || s2 == NULL) {
        if (s1 == s2) return 0;
        return s1 ? 1 : -1;
    }
    gsize i = 0;
    while (i < n && *s1 && *s2) {
        gchar c1 = g_ascii_tolower(*s1);
        gchar c2 = g_ascii_tolower(*s2);
        if (c1 != c2) {
            return (guchar)c1 - (guchar)c2;
        }
        s1++;
        s2++;
        i++;
    }
    if (i == n) {
        return 0;
    }
    return (guchar)g_ascii_tolower(*s1) - (guchar)g_ascii_tolower(*s2);
}

/* ------------------------------------------------------------------ */
/*  ASCII number parsing                                              */
/* ------------------------------------------------------------------ */

guint64
g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base)
{
    return strtoull(nptr, endptr, (int)base);
}

gint64
g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base)
{
    return strtoll(nptr, endptr, (int)base);
}

gdouble
g_ascii_strtod(const gchar *nptr, gchar **endptr)
{
    /* Real GLib uses locale-independent parsing (always '.'),
     * strtod is locale-dependent, but WASI locale is always C. */
    return strtod(nptr, endptr);
}

gchar *
g_ascii_dtostr(gchar *buffer, gint buf_len, gdouble d)
{
    return g_ascii_formatd(buffer, buf_len, "%.17g", d);
}

gchar *
g_ascii_formatd(gchar *buffer, gint buf_len, const gchar *format, gdouble d)
{
    snprintf(buffer, (gsize)buf_len, format, d);
    return buffer;
}

/* ------------------------------------------------------------------ */
/*  Formatted printing                                                */
/* ------------------------------------------------------------------ */

gint
g_snprintf(gchar *string, gulong n, const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    gint ret = (gint)vsnprintf(string, (gsize)n, format, args);
    va_end(args);
    return ret;
}

gint
g_vsnprintf(gchar *string, gulong n, const gchar *format, va_list args)
{
    return (gint)vsnprintf(string, (gsize)n, format, args);
}

/* ------------------------------------------------------------------ */
/*  vasprintf wrapper                                                 */
/* ------------------------------------------------------------------ */

gint
g_vasprintf(gchar **strp, const gchar *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) {
        *strp = NULL;
        return -1;
    }

    *strp = (gchar *)g_malloc((gsize)len + 1);
    vsnprintf(*strp, (gsize)len + 1, format, args);
    return len;
}

/* ------------------------------------------------------------------ */
/*  g_sprintf (variadic)                                              */
/* ------------------------------------------------------------------ */

gint
g_sprintf(gchar *string, const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    gint ret = (gint)vsprintf(string, format, args);
    va_end(args);
    return ret;
}

/* ------------------------------------------------------------------ */
/*  Error string                                                      */
/* ------------------------------------------------------------------ */

const gchar *
g_strerror(gint errnum)
{
    return strerror(errnum);
}

/* ------------------------------------------------------------------ */
/*  Safe string operations                                            */
/* ------------------------------------------------------------------ */

gsize
g_strlcpy(gchar *dest, const gchar *src, gsize dest_size)
{
    gsize src_len = strlen(src);
    if (dest_size > 0) {
        gsize copy_len = src_len < dest_size - 1 ? src_len : dest_size - 1;
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
    }
    return src_len;
}

gsize
g_strlcat(gchar *dest, const gchar *src, gsize dest_size)
{
    gsize dest_len = strlen(dest);
    gsize src_len = strlen(src);

    if (dest_len >= dest_size) {
        return dest_size + src_len;
    }

    gsize space = dest_size - dest_len - 1;
    gsize copy_len = src_len < space ? src_len : space;
    memcpy(dest + dest_len, src, copy_len);
    dest[dest_len + copy_len] = '\0';

    return dest_len + src_len;
}

gchar *
g_stpcpy(gchar *dest, const gchar *src)
{
    while ((*dest = *src) != '\0') {
        dest++;
        src++;
    }
    return dest;
}

/* ------------------------------------------------------------------ */
/*  Substring search                                                  */
/* ------------------------------------------------------------------ */

gchar *
g_strstr_len(const gchar *haystack, gssize haystack_len, const gchar *needle)
{
    if (haystack == NULL || needle == NULL) {
        return NULL;
    }

    gsize needle_len = strlen(needle);
    if (needle_len == 0) {
        return (gchar *)haystack;
    }

    gsize limit;
    if (haystack_len < 0) {
        limit = strlen(haystack);
    } else {
        limit = (gsize)haystack_len;
    }

    if (needle_len > limit) {
        return NULL;
    }

    for (gsize i = 0; i <= limit - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return (gchar *)(haystack + i);
        }
    }
    return NULL;
}

gchar *
g_strrstr(const gchar *haystack, const gchar *needle)
{
    if (haystack == NULL || needle == NULL) {
        return NULL;
    }

    gsize haystack_len = strlen(haystack);
    gsize needle_len = strlen(needle);

    if (needle_len == 0) {
        return (gchar *)(haystack + haystack_len);
    }
    if (needle_len > haystack_len) {
        return NULL;
    }

    for (gssize i = (gssize)(haystack_len - needle_len); i >= 0; i--) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return (gchar *)(haystack + i);
        }
    }
    return NULL;
}
