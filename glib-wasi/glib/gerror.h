/*
 * GLib GError / GQuark shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GERROR_H
#define GLIB_WASI_GERROR_H

#include <stdarg.h>
#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  GQuark — lightweight string-interning for error domains           */
/* ------------------------------------------------------------------ */

GQuark       g_quark_from_string       (const gchar *string);
GQuark       g_quark_from_static_string(const gchar *string);
const gchar *g_quark_to_string         (GQuark quark);
GQuark       g_quark_try_string        (const gchar *string);

/* ------------------------------------------------------------------ */
/*  GError                                                            */
/* ------------------------------------------------------------------ */

typedef struct _GError {
    GQuark  domain;
    gint    code;
    gchar  *message;
} GError;

/* Creation */
GError  *g_error_new        (GQuark domain, gint code,
                             const gchar *format, ...) G_GNUC_PRINTF(3, 4);
GError  *g_error_new_literal(GQuark domain, gint code, const gchar *message);
GError  *g_error_new_valist (GQuark domain, gint code,
                             const gchar *format, va_list args);

/* Lifecycle */
void     g_error_free       (GError *error);
GError  *g_error_copy       (const GError *error);

/* Query */
gboolean g_error_matches    (const GError *error, GQuark domain, gint code);

/* Convenience setters */
void     g_set_error        (GError **err, GQuark domain, gint code,
                             const gchar *format, ...) G_GNUC_PRINTF(4, 5);
void     g_set_error_literal(GError **err, GQuark domain, gint code,
                             const gchar *message);
void     g_propagate_error  (GError **dest, GError *src);
void     g_clear_error      (GError **err);

/* Prefix helpers */
void     g_prefix_error     (GError **err, const gchar *format, ...)
                             G_GNUC_PRINTF(2, 3);
void     g_propagate_prefixed_error(GError **dest, GError *src,
                                    const gchar *format, ...)
                                    G_GNUC_PRINTF(3, 4);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GERROR_H */
