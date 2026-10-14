/*
 * Minimal GError / GQuark implementation for QEMU wasm32-wasi.
 *
 * GQuark: simple array-based string interning with linear search.
 * GError: standard creation / propagation helpers.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "../glib/glib.h"
#include "../glib/gstrfuncs.h"
#include "../glib/gerror.h"

/* ================================================================== */
/*  GQuark — lightweight string-interning                             */
/* ================================================================== */

#define QUARK_INITIAL_CAPACITY 256

static gchar  **quark_strings    = NULL;
static gboolean *quark_is_static = NULL;   /* TRUE if pointer is static */
static guint32   quark_count     = 0;      /* 0 is "invalid" quark */
static guint32   quark_capacity  = 0;

static void
quark_ensure_init(void)
{
    if (quark_strings != NULL) {
        return;
    }
    quark_capacity = QUARK_INITIAL_CAPACITY;
    quark_strings  = (gchar **)g_malloc0(sizeof(gchar *) * quark_capacity);
    quark_is_static = (gboolean *)g_malloc0(sizeof(gboolean) * quark_capacity);
    /* Index 0 is reserved (invalid quark) */
    quark_strings[0] = NULL;
    quark_is_static[0] = FALSE;
    quark_count = 1;
}

static void
quark_grow(void)
{
    guint32 new_cap = quark_capacity * 2;
    quark_strings = (gchar **)g_realloc(quark_strings,
                                         sizeof(gchar *) * new_cap);
    quark_is_static = (gboolean *)g_realloc(quark_is_static,
                                              sizeof(gboolean) * new_cap);
    memset(quark_strings + quark_capacity, 0,
           sizeof(gchar *) * (new_cap - quark_capacity));
    memset(quark_is_static + quark_capacity, 0,
           sizeof(gboolean) * (new_cap - quark_capacity));
    quark_capacity = new_cap;
}

GQuark
g_quark_try_string(const gchar *string)
{
    if (string == NULL) {
        return 0;
    }
    quark_ensure_init();
    for (guint32 i = 1; i < quark_count; i++) {
        if (quark_strings[i] && strcmp(quark_strings[i], string) == 0) {
            return (GQuark)i;
        }
    }
    return 0;
}

GQuark
g_quark_from_string(const gchar *string)
{
    if (string == NULL) {
        return 0;
    }
    GQuark q = g_quark_try_string(string);
    if (q != 0) {
        return q;
    }

    quark_ensure_init();
    if (quark_count >= quark_capacity) {
        quark_grow();
    }

    guint32 idx = quark_count++;
    quark_strings[idx]   = g_strdup(string);
    quark_is_static[idx] = FALSE;
    return (GQuark)idx;
}

GQuark
g_quark_from_static_string(const gchar *string)
{
    if (string == NULL) {
        return 0;
    }
    GQuark q = g_quark_try_string(string);
    if (q != 0) {
        return q;
    }

    quark_ensure_init();
    if (quark_count >= quark_capacity) {
        quark_grow();
    }

    guint32 idx = quark_count++;
    quark_strings[idx]   = (gchar *)string;  /* no copy — caller owns */
    quark_is_static[idx] = TRUE;
    return (GQuark)idx;
}

const gchar *
g_quark_to_string(GQuark quark)
{
    quark_ensure_init();
    if (quark == 0 || quark >= quark_count) {
        return NULL;
    }
    return quark_strings[quark];
}

/* ================================================================== */
/*  GError                                                            */
/* ================================================================== */

GError *
g_error_new_valist(GQuark domain, gint code,
                   const gchar *format, va_list args)
{
    GError *error = g_new(GError, 1);
    error->domain  = domain;
    error->code    = code;
    error->message = g_strdup_vprintf(format, args);
    return error;
}

GError *
g_error_new(GQuark domain, gint code, const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    GError *error = g_error_new_valist(domain, code, format, args);
    va_end(args);
    return error;
}

GError *
g_error_new_literal(GQuark domain, gint code, const gchar *message)
{
    GError *error = g_new(GError, 1);
    error->domain  = domain;
    error->code    = code;
    error->message = g_strdup(message);
    return error;
}

void
g_error_free(GError *error)
{
    if (error == NULL) {
        return;
    }
    g_free(error->message);
    g_free(error);
}

GError *
g_error_copy(const GError *error)
{
    if (error == NULL) {
        return NULL;
    }
    return g_error_new_literal(error->domain, error->code, error->message);
}

gboolean
g_error_matches(const GError *error, GQuark domain, gint code)
{
    if (error == NULL) {
        return FALSE;
    }
    return error->domain == domain && error->code == code;
}

/* ------------------------------------------------------------------ */
/*  Convenience setters                                               */
/* ------------------------------------------------------------------ */

void
g_set_error(GError **err, GQuark domain, gint code,
            const gchar *format, ...)
{
    if (err == NULL) {
        return;
    }
    if (*err != NULL) {
        fprintf(stderr, "GLib-WASI: g_set_error: *err is non-NULL "
                "(overwriting existing error)\n");
        g_error_free(*err);
    }

    va_list args;
    va_start(args, format);
    *err = g_error_new_valist(domain, code, format, args);
    va_end(args);
}

void
g_set_error_literal(GError **err, GQuark domain, gint code,
                    const gchar *message)
{
    if (err == NULL) {
        return;
    }
    if (*err != NULL) {
        fprintf(stderr, "GLib-WASI: g_set_error_literal: *err is non-NULL "
                "(overwriting existing error)\n");
        g_error_free(*err);
    }
    *err = g_error_new_literal(domain, code, message);
}

void
g_propagate_error(GError **dest, GError *src)
{
    if (src == NULL) {
        return;
    }
    if (dest == NULL) {
        g_error_free(src);
        return;
    }
    if (*dest != NULL) {
        fprintf(stderr, "GLib-WASI: g_propagate_error: *dest is non-NULL "
                "(overwriting existing error)\n");
        g_error_free(*dest);
    }
    *dest = src;
}

void
g_clear_error(GError **err)
{
    if (err && *err) {
        g_error_free(*err);
        *err = NULL;
    }
}

void
g_prefix_error(GError **err, const gchar *format, ...)
{
    if (err == NULL || *err == NULL) {
        return;
    }

    va_list args;
    va_start(args, format);
    gchar *prefix = g_strdup_vprintf(format, args);
    va_end(args);

    gchar *old_message = (*err)->message;
    (*err)->message = g_strconcat(prefix, old_message, NULL);
    g_free(old_message);
    g_free(prefix);
}

void
g_propagate_prefixed_error(GError **dest, GError *src,
                           const gchar *format, ...)
{
    if (src == NULL) {
        return;
    }

    va_list args;
    va_start(args, format);
    gchar *prefix = g_strdup_vprintf(format, args);
    va_end(args);

    gchar *old_message = src->message;
    src->message = g_strconcat(prefix, old_message, NULL);
    g_free(old_message);
    g_free(prefix);

    g_propagate_error(dest, src);
}
