/*
 * GLib miscellaneous utility functions – wasm32-wasi implementation.
 *
 * Many of these return static/stub values since WASI does not have a
 * full POSIX user/host environment.  Path utilities, time functions,
 * and base64 are fully implemented.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "../glib/gutils.h"
#include "../glib/gstrfuncs.h"   /* g_strdup */
#include "../glib/gerror.h"      /* GError, g_set_error_literal */

/* ------------------------------------------------------------------ */
/*  Static storage for program / application names                    */
/* ------------------------------------------------------------------ */

static gchar *stored_prgname = NULL;
static gchar *stored_appname = NULL;

/* ------------------------------------------------------------------ */
/*  User / host identity (stubs for WASI)                             */
/* ------------------------------------------------------------------ */

const gchar *
g_get_user_name(void)
{
    return "wasi-user";
}

const gchar *
g_get_real_name(void)
{
    return "WASI User";
}

const gchar *
g_get_home_dir(void)
{
    return "/home";
}

const gchar *
g_get_tmp_dir(void)
{
    return "/tmp";
}

const gchar *
g_get_host_name(void)
{
    return "wasi-host";
}

const gchar *
g_get_prgname(void)
{
    return stored_prgname;
}

void
g_set_prgname(const gchar *prgname)
{
    g_free(stored_prgname);
    stored_prgname = g_strdup(prgname);
}

const gchar *
g_get_application_name(void)
{
    return stored_appname;
}

void
g_set_application_name(const gchar *application_name)
{
    g_free(stored_appname);
    stored_appname = g_strdup(application_name);
}

/* ------------------------------------------------------------------ */
/*  Path utilities                                                    */
/* ------------------------------------------------------------------ */

gchar *
g_path_get_basename(const gchar *file_name)
{
    if (!file_name || *file_name == '\0') {
        return g_strdup(".");
    }

    /* Skip trailing slashes. */
    gsize len = strlen(file_name);
    while (len > 1 && file_name[len - 1] == '/') {
        len--;
    }

    /* Root slash only. */
    if (len == 1 && file_name[0] == '/') {
        return g_strdup("/");
    }

    /* Find last separator within the trimmed portion. */
    const gchar *base = file_name + len;
    while (base > file_name && *(base - 1) != '/') {
        base--;
    }

    gsize blen = (gsize)((file_name + len) - base);
    gchar *result = (gchar *)g_malloc(blen + 1);
    memcpy(result, base, blen);
    result[blen] = '\0';
    return result;
}

gchar *
g_path_get_dirname(const gchar *file_name)
{
    if (!file_name || *file_name == '\0') {
        return g_strdup(".");
    }

    /* Skip trailing slashes. */
    gsize len = strlen(file_name);
    while (len > 1 && file_name[len - 1] == '/') {
        len--;
    }

    /* Walk backwards to find the last separator. */
    const gchar *p = file_name + len - 1;
    while (p > file_name && *p != '/') {
        p--;
    }

    if (p == file_name) {
        if (*p == '/') {
            return g_strdup("/");
        }
        return g_strdup(".");
    }

    /* Trim trailing slashes from the directory part. */
    while (p > file_name && *(p - 1) == '/') {
        p--;
    }

    if (p == file_name && *p == '/') {
        return g_strdup("/");
    }

    gsize dlen = (gsize)(p - file_name);
    gchar *result = (gchar *)g_malloc(dlen + 1);
    memcpy(result, file_name, dlen);
    result[dlen] = '\0';
    return result;
}

gboolean
g_path_is_absolute(const gchar *file_name)
{
    return file_name && file_name[0] == '/';
}

gchar *
g_build_path(const gchar *separator, const gchar *first_element, ...)
{
    if (!separator || !first_element) {
        return g_strdup("");
    }

    gsize sep_len = strlen(separator);

    /* First pass: compute total length. */
    va_list ap;
    va_start(ap, first_element);

    gsize total = strlen(first_element);
    const gchar *elem;
    while ((elem = va_arg(ap, const gchar *)) != NULL) {
        total += sep_len + strlen(elem);
    }
    va_end(ap);

    gchar *result = (gchar *)g_malloc(total + 1);
    gchar *dst = result;

    /* Copy first element. */
    gsize flen = strlen(first_element);
    memcpy(dst, first_element, flen);
    dst += flen;

    /* Copy remaining elements separated by separator. */
    va_start(ap, first_element);
    while ((elem = va_arg(ap, const gchar *)) != NULL) {
        /* Strip trailing separators from what we have so far. */
        while (dst > result && sep_len > 0 &&
               (gsize)(dst - result) >= sep_len &&
               memcmp(dst - sep_len, separator, sep_len) == 0) {
            dst -= sep_len;
        }

        /* Strip leading separators from the new element. */
        while (sep_len > 0 && strncmp(elem, separator, sep_len) == 0) {
            elem += sep_len;
        }

        /* Add one separator. */
        memcpy(dst, separator, sep_len);
        dst += sep_len;

        /* Copy element. */
        gsize elen = strlen(elem);
        memcpy(dst, elem, elen);
        dst += elen;
    }
    va_end(ap);

    *dst = '\0';
    return result;
}

gchar *
g_build_filename(const gchar *first_element, ...)
{
    if (!first_element) {
        return g_strdup("");
    }

    /* First pass: compute total length. */
    va_list ap;
    va_start(ap, first_element);

    gsize total = strlen(first_element);
    const gchar *elem;
    while ((elem = va_arg(ap, const gchar *)) != NULL) {
        total += 1 + strlen(elem);  /* 1 for '/' */
    }
    va_end(ap);

    gchar *result = (gchar *)g_malloc(total + 1);
    gchar *dst = result;

    /* Copy first element. */
    gsize flen = strlen(first_element);
    memcpy(dst, first_element, flen);
    dst += flen;

    /* Copy remaining elements. */
    va_start(ap, first_element);
    while ((elem = va_arg(ap, const gchar *)) != NULL) {
        /* Strip trailing slashes. */
        while (dst > result && *(dst - 1) == '/') {
            dst--;
        }
        /* Strip leading slashes from the element. */
        while (*elem == '/') {
            elem++;
        }
        /* Add one slash. */
        *dst++ = '/';
        /* Copy element. */
        gsize elen = strlen(elem);
        memcpy(dst, elem, elen);
        dst += elen;
    }
    va_end(ap);

    *dst = '\0';
    return result;
}

/* ------------------------------------------------------------------ */
/*  Environment                                                       */
/* ------------------------------------------------------------------ */

const gchar *
g_getenv(const gchar *variable)
{
    if (!variable) {
        return NULL;
    }
    return getenv(variable);
}

gboolean
g_setenv(const gchar *variable, const gchar *value, gboolean overwrite)
{
    if (!variable || !value) {
        return FALSE;
    }
#ifdef __wasi__
    /* WASI may not have setenv; try it but tolerate absence. */
#if defined(setenv) || defined(_POSIX_VERSION)
    return setenv(variable, value, overwrite) == 0;
#else
    (void)overwrite;
    return FALSE;
#endif
#else
    return setenv(variable, value, overwrite) == 0;
#endif
}

void
g_unsetenv(const gchar *variable)
{
    if (!variable) {
        return;
    }
#ifdef __wasi__
#if defined(unsetenv) || defined(_POSIX_VERSION)
    unsetenv(variable);
#endif
#else
    unsetenv(variable);
#endif
}

extern char **environ;

gchar **
g_get_environ(void)
{
    /* Count entries. */
    gsize count = 0;
    if (environ) {
        while (environ[count]) {
            count++;
        }
    }

    gchar **copy = (gchar **)g_malloc((count + 1) * sizeof(gchar *));
    for (gsize i = 0; i < count; i++) {
        copy[i] = g_strdup(environ[i]);
    }
    copy[count] = NULL;
    return copy;
}

const gchar *
g_environ_getenv(gchar **envp, const gchar *variable)
{
    if (!envp || !variable) {
        return NULL;
    }

    gsize vlen = strlen(variable);
    for (gsize i = 0; envp[i] != NULL; i++) {
        if (strncmp(envp[i], variable, vlen) == 0 && envp[i][vlen] == '=') {
            return envp[i] + vlen + 1;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Time                                                              */
/* ------------------------------------------------------------------ */

gint64
g_get_monotonic_time(void)
{
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (gint64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    }
#endif
    /* Fallback to CLOCK_REALTIME. */
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (gint64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    }
    return 0;
}

gint64
g_get_real_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (gint64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    }
    return 0;
}

void
g_usleep(gulong microseconds)
{
    /*
     * WASI does not provide usleep/nanosleep.  Busy-wait is not
     * useful in a wasm environment, so this is a no-op.
     */
    (void)microseconds;
}

/* ------------------------------------------------------------------ */
/*  Miscellaneous                                                     */
/* ------------------------------------------------------------------ */

gchar *
g_find_program_in_path(const gchar *program)
{
    /* No PATH search in WASI. */
    (void)program;
    return NULL;
}

/*
 * g_qsort_with_data – qsort() with a user_data parameter.
 *
 * Standard qsort() does not pass user_data, so we use a static
 * variable to thread the context through.  This is *not* thread-safe,
 * but wasm32-wasi is single-threaded.
 */
static GCompareDataFunc g_qsort_compare_func;
static gpointer         g_qsort_user_data;

static int
qsort_adapter(const void *a, const void *b)
{
    return g_qsort_compare_func(a, b, g_qsort_user_data);
}

void
g_qsort_with_data(gconstpointer    pbase,
                   gint             total_elems,
                   gsize            size,
                   GCompareDataFunc compare_func,
                   gpointer         user_data)
{
    if (!pbase || total_elems <= 0 || size == 0 || !compare_func) {
        return;
    }
    g_qsort_compare_func = compare_func;
    g_qsort_user_data    = user_data;
    qsort((void *)pbase, (size_t)total_elems, size, qsort_adapter);
}

/* ------------------------------------------------------------------ */
/*  Base64                                                            */
/* ------------------------------------------------------------------ */

static const gchar base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

gchar *
g_base64_encode(const guchar *data, gsize len)
{
    if (!data || len == 0) {
        return g_strdup("");
    }

    /* Output length: 4 chars per 3 bytes, rounded up, plus NUL. */
    gsize out_len = ((len + 2) / 3) * 4;
    gchar *out = (gchar *)g_malloc(out_len + 1);
    gchar *dst = out;

    gsize i;
    for (i = 0; i + 2 < len; i += 3) {
        guint32 n = ((guint32)data[i] << 16) |
                    ((guint32)data[i + 1] << 8) |
                    ((guint32)data[i + 2]);
        *dst++ = base64_alphabet[(n >> 18) & 0x3f];
        *dst++ = base64_alphabet[(n >> 12) & 0x3f];
        *dst++ = base64_alphabet[(n >>  6) & 0x3f];
        *dst++ = base64_alphabet[ n        & 0x3f];
    }

    if (i < len) {
        guint32 n = (guint32)data[i] << 16;
        if (i + 1 < len) {
            n |= (guint32)data[i + 1] << 8;
        }

        *dst++ = base64_alphabet[(n >> 18) & 0x3f];
        *dst++ = base64_alphabet[(n >> 12) & 0x3f];
        *dst++ = (i + 1 < len) ? base64_alphabet[(n >> 6) & 0x3f] : '=';
        *dst++ = '=';
    }

    *dst = '\0';
    return out;
}

/* Decode table: maps ASCII value -> 6-bit value, or -1 for invalid. */
static inline gint
base64_decode_char(gchar c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  /* padding or invalid */
}

guchar *
g_base64_decode(const gchar *text, gsize *out_len)
{
    if (!text || *text == '\0') {
        if (out_len) {
            *out_len = 0;
        }
        return (guchar *)g_strdup("");
    }

    gsize in_len = strlen(text);
    /* Upper bound for output size. */
    gsize max_out = (in_len / 4) * 3 + 3;
    guchar *out = (guchar *)g_malloc(max_out);
    guchar *dst = out;

    guint32 accum = 0;
    gint    bits  = 0;

    for (gsize i = 0; i < in_len; i++) {
        gchar c = text[i];

        /* Skip whitespace. */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        if (c == '=') {
            break;
        }

        gint val = base64_decode_char(c);
        if (val < 0) {
            continue;  /* skip invalid chars */
        }

        accum = (accum << 6) | (guint32)val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            *dst++ = (guchar)((accum >> bits) & 0xff);
        }
    }

    if (out_len) {
        *out_len = (gsize)(dst - out);
    }
    return out;
}

/* Main-loop stubs (g_timeout_add, g_idle_add, etc.) are in gmain.c */

/* ------------------------------------------------------------------ */
/*  g_get_current_dir                                                 */
/* ------------------------------------------------------------------ */

gchar *
g_get_current_dir(void)
{
    return g_strdup(".");
}

/* ------------------------------------------------------------------ */
/*  File I/O utilities                                                */
/* ------------------------------------------------------------------ */

gboolean
g_file_get_contents(const gchar *filename, gchar **contents,
                    gsize *length, GError **error)
{
    if (!filename || !contents) {
        if (error) {
            g_set_error_literal(error, 0, 0, "Invalid arguments");
        }
        return FALSE;
    }

    FILE *f = fopen(filename, "rb");
    if (!f) {
        if (error) {
            g_set_error_literal(error, 0, 0, "Failed to open file");
        }
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        if (error) {
            g_set_error_literal(error, 0, 0, "Failed to determine file size");
        }
        return FALSE;
    }

    gchar *buf = (gchar *)g_malloc((gsize)size + 1);
    gsize read_size = fread(buf, 1, (gsize)size, f);
    fclose(f);

    buf[read_size] = '\0';
    *contents = buf;
    if (length) {
        *length = read_size;
    }
    return TRUE;
}

gboolean
g_file_set_contents(const gchar *filename, const gchar *contents,
                    gssize length, GError **error)
{
    if (!filename || !contents) {
        if (error) {
            g_set_error_literal(error, 0, 0, "Invalid arguments");
        }
        return FALSE;
    }

    gsize len = (length < 0) ? strlen(contents) : (gsize)length;

    FILE *f = fopen(filename, "wb");
    if (!f) {
        if (error) {
            g_set_error_literal(error, 0, 0, "Failed to open file for writing");
        }
        return FALSE;
    }

    gsize written = fwrite(contents, 1, len, f);
    fclose(f);

    if (written != len) {
        if (error) {
            g_set_error_literal(error, 0, 0, "Failed to write all data");
        }
        return FALSE;
    }

    return TRUE;
}

gint
g_file_open_tmp(const gchar *tmpl, gchar **name_used, GError **error)
{
    (void)tmpl;
    (void)name_used;
    if (error) {
        g_set_error_literal(error, 0, 0,
                            "Temporary files not supported on WASI");
    }
    return -1;
}

/* g_mkstemp is provided as static inline in gstdio.h */

/* ------------------------------------------------------------------ */
/*  Base64 decode in-place                                            */
/* ------------------------------------------------------------------ */

guchar *
g_base64_decode_inplace(gchar *text, gsize *out_len)
{
    if (!text || *text == '\0') {
        if (out_len) {
            *out_len = 0;
        }
        return (guchar *)text;
    }

    guchar *dst = (guchar *)text;
    guchar *src = (guchar *)text;
    guint32 accum = 0;
    gint bits = 0;
    gsize written = 0;

    while (*src) {
        gchar c = (gchar)*src++;
        gint val;

        if (c >= 'A' && c <= 'Z')      val = c - 'A';
        else if (c >= 'a' && c <= 'z')  val = c - 'a' + 26;
        else if (c >= '0' && c <= '9')  val = c - '0' + 52;
        else if (c == '+')              val = 62;
        else if (c == '/')              val = 63;
        else if (c == '=')             break;
        else continue; /* skip whitespace/invalid */

        accum = (accum << 6) | (guint32)val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            dst[written++] = (guchar)((accum >> bits) & 0xff);
        }
    }

    if (out_len) {
        *out_len = written;
    }
    return (guchar *)text;
}

/* ------------------------------------------------------------------ */
/*  UTF-8 validation                                                  */
/* ------------------------------------------------------------------ */

gboolean
g_utf8_validate(const gchar *str, gssize max_len, const gchar **end)
{
    if (!str) {
        if (end) *end = NULL;
        return FALSE;
    }

    const guchar *p = (const guchar *)str;
    const guchar *limit = (max_len >= 0) ? p + max_len : NULL;

    while (limit ? (p < limit) : (*p != 0)) {
        guchar c = *p;
        gsize seq_len;

        if (c < 0x80) {
            seq_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            seq_len = 2;
            if (c < 0xC2) goto invalid; /* overlong */
        } else if ((c & 0xF0) == 0xE0) {
            seq_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            seq_len = 4;
            if (c > 0xF4) goto invalid;
        } else {
            goto invalid;
        }

        /* Check continuation bytes */
        for (gsize i = 1; i < seq_len; i++) {
            if (limit && (p + i >= limit)) goto invalid;
            if ((p[i] & 0xC0) != 0x80) goto invalid;
        }

        p += seq_len;
    }

    if (end) *end = (const gchar *)p;
    return TRUE;

invalid:
    if (end) *end = (const gchar *)p;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Pattern matching (simple glob: * and ?)                           */
/* ------------------------------------------------------------------ */

static gboolean
pattern_match_impl(const gchar *pattern, const gchar *string)
{
    while (*pattern && *string) {
        if (*pattern == '*') {
            pattern++;
            /* Skip consecutive stars */
            while (*pattern == '*') pattern++;
            if (*pattern == '\0') return TRUE;
            /* Try matching the rest from each position */
            while (*string) {
                if (pattern_match_impl(pattern, string)) return TRUE;
                string++;
            }
            return pattern_match_impl(pattern, string);
        } else if (*pattern == '?') {
            pattern++;
            string++;
        } else {
            if (*pattern != *string) return FALSE;
            pattern++;
            string++;
        }
    }

    /* Skip trailing stars in pattern */
    while (*pattern == '*') pattern++;
    return (*pattern == '\0' && *string == '\0');
}

gboolean
g_pattern_match_simple(const gchar *pattern, const gchar *string)
{
    if (!pattern || !string) return FALSE;
    return pattern_match_impl(pattern, string);
}

/* GMappedFile — moved to gmappedfile.c */

/* ------------------------------------------------------------------ */
/*  g_once_init_enter / g_once_init_leave (single-threaded)           */
/* ------------------------------------------------------------------ */

#ifndef WASI_THREADS  /* with threads: in gthread.c */
gboolean
g_once_init_enter(volatile gsize *location)
{
    return (*location == 0);
}

void
g_once_init_leave(volatile gsize *location, gsize result)
{
    *location = result;
}
#endif

/* ------------------------------------------------------------------ */
/*  Crypto stubs (GChecksum / GHmac)                                  */
/* ------------------------------------------------------------------ */

GChecksum *
g_checksum_new(GChecksumType type)
{
    (void)type;
    return NULL;
}

void
g_checksum_free(GChecksum *checksum)
{
    (void)checksum;
}

void
g_checksum_update(GChecksum *checksum, const guchar *data, gssize length)
{
    (void)checksum;
    (void)data;
    (void)length;
}

void
g_checksum_get_digest(GChecksum *checksum, guint8 *buffer, gsize *digest_len)
{
    (void)checksum;
    if (buffer && digest_len && *digest_len > 0) {
        memset(buffer, 0, *digest_len);
    }
    if (digest_len) {
        *digest_len = 0;
    }
}

gssize
g_checksum_type_get_length(GChecksumType type)
{
    switch (type) {
    case G_CHECKSUM_MD5:    return 16;
    case G_CHECKSUM_SHA1:   return 20;
    case G_CHECKSUM_SHA256: return 32;
    case G_CHECKSUM_SHA384: return 48;
    case G_CHECKSUM_SHA512: return 64;
    default:                return -1;
    }
}

GHmac *
g_hmac_new(GChecksumType type, const guchar *key, gsize key_len)
{
    (void)type;
    (void)key;
    (void)key_len;
    return NULL;
}

void
g_hmac_update(GHmac *hmac, const guchar *data, gssize length)
{
    (void)hmac;
    (void)data;
    (void)length;
}

void
g_hmac_get_digest(GHmac *hmac, guint8 *buffer, gsize *digest_len)
{
    (void)hmac;
    if (buffer && digest_len && *digest_len > 0) {
        memset(buffer, 0, *digest_len);
    }
    if (digest_len) {
        *digest_len = 0;
    }
}

void
g_hmac_unref(GHmac *hmac)
{
    (void)hmac;
}

/* ------------------------------------------------------------------ */
/*  GTimer                                                            */
/* ------------------------------------------------------------------ */

struct _GTimer {
    struct timespec start;
};

GTimer *
g_timer_new(void)
{
    GTimer *timer = g_new0(GTimer, 1);
    clock_gettime(CLOCK_REALTIME, &timer->start);
    return timer;
}

gdouble
g_timer_elapsed(GTimer *timer, gulong *microseconds)
{
    if (!timer) {
        if (microseconds) *microseconds = 0;
        return 0.0;
    }

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    gdouble elapsed = (gdouble)(now.tv_sec - timer->start.tv_sec)
                    + (gdouble)(now.tv_nsec - timer->start.tv_nsec) / 1e9;

    if (microseconds) {
        *microseconds = (gulong)(elapsed * 1e6);
    }
    return elapsed;
}

void
g_timer_destroy(GTimer *timer)
{
    g_free(timer);
}

/* ------------------------------------------------------------------ */
/*  GSequence stubs                                                   */
/* ------------------------------------------------------------------ */

struct _GSequence {
    GDestroyNotify data_destroy;
};

struct _GSequenceIter {
    gpointer data;
};

GSequence *
g_sequence_new(GDestroyNotify data_destroy)
{
    GSequence *seq = g_new0(GSequence, 1);
    seq->data_destroy = data_destroy;
    return seq;
}

GSequenceIter *
g_sequence_insert_sorted(GSequence *seq, gpointer data,
                         GCompareDataFunc cmp_func, gpointer cmp_data)
{
    (void)seq;
    (void)cmp_func;
    (void)cmp_data;
    GSequenceIter *iter = g_new0(GSequenceIter, 1);
    iter->data = data;
    return iter;
}

GSequenceIter *
g_sequence_lookup(GSequence *seq, gpointer data,
                  GCompareDataFunc cmp_func, gpointer cmp_data)
{
    (void)seq;
    (void)data;
    (void)cmp_func;
    (void)cmp_data;
    return NULL;
}

void
g_sequence_remove(GSequenceIter *iter)
{
    if (iter) {
        g_free(iter);
    }
}

/* ------------------------------------------------------------------ */
/*  GData stubs                                                       */
/* ------------------------------------------------------------------ */

gpointer
g_datalist_get_data(GData **datalist, const gchar *key)
{
    (void)datalist;
    (void)key;
    return NULL;
}

void
g_datalist_set_data(GData **datalist, const gchar *key, gpointer data)
{
    (void)datalist;
    (void)key;
    (void)data;
}

void
g_datalist_remove_data(GData **datalist, const gchar *key)
{
    (void)datalist;
    (void)key;
}

/* ------------------------------------------------------------------ */
/*  Spawn stubs                                                       */
/* ------------------------------------------------------------------ */

gboolean
g_spawn_async_with_pipes(const gchar *working_directory,
                         gchar **argv, gchar **envp,
                         GSpawnFlags flags,
                         GFunc child_setup,
                         gpointer user_data,
                         GPid *child_pid,
                         gint *standard_input,
                         gint *standard_output,
                         gint *standard_error,
                         GError **error)
{
    (void)working_directory;
    (void)argv;
    (void)envp;
    (void)flags;
    (void)child_setup;
    (void)user_data;
    (void)child_pid;
    (void)standard_input;
    (void)standard_output;
    (void)standard_error;
    if (error) {
        g_set_error_literal(error, 0, 0,
                            "Spawning processes not supported on WASI");
    }
    return FALSE;
}

void
g_spawn_close_pid(GPid pid)
{
    (void)pid;
}

/* ------------------------------------------------------------------ */
/*  GUri stubs                                                        */
/* ------------------------------------------------------------------ */

GUri *
g_uri_parse(const gchar *uri_string, gint flags, GError **error)
{
    (void)uri_string;
    (void)flags;
    if (error) {
        g_set_error_literal(error, 0, 0, "URI parsing not supported on WASI");
    }
    return NULL;
}

const gchar *
g_uri_get_scheme(GUri *uri)
{
    (void)uri;
    return NULL;
}

const gchar *
g_uri_get_host(GUri *uri)
{
    (void)uri;
    return NULL;
}

const gchar *
g_uri_get_path(GUri *uri)
{
    (void)uri;
    return NULL;
}

gint
g_uri_get_port(GUri *uri)
{
    (void)uri;
    return -1;
}

const gchar *
g_uri_get_query(GUri *uri)
{
    (void)uri;
    return NULL;
}

GHashTable *
g_uri_parse_params(const gchar *params, gssize length,
                   const gchar *separators, gint flags,
                   GError **error)
{
    (void)params;
    (void)length;
    (void)separators;
    (void)flags;
    (void)error;
    return NULL;
}

void
g_uri_unref(GUri *uri)
{
    (void)uri;
}

/* GDateTime — moved to gdatetime.c */

/* ------------------------------------------------------------------ */
/*  GUnix stubs                                                       */
/* ------------------------------------------------------------------ */

gboolean
g_unix_open_pipe(gint *fds, gint flags, GError **error)
{
    (void)fds;
    (void)flags;
    if (error) {
        g_set_error_literal(error, 0, 0, "Pipes not supported on WASI");
    }
    return FALSE;
}

gboolean
g_unix_set_fd_nonblocking(gint fd, gboolean nonblock, GError **error)
{
    (void)fd;
    (void)nonblock;
    (void)error;
    return TRUE;
}

/* g_sprintf — moved to gstrfuncs.c */
