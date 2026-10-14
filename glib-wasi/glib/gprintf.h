/*
 * GLib printf helpers shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GPRINTF_H
#define GLIB_WASI_GPRINTF_H

#include <stdio.h>
#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

gint g_snprintf(gchar *string, gulong n, const gchar *format, ...)
     G_GNUC_PRINTF(3, 4);
gint g_sprintf(gchar *string, const gchar *format, ...)
     G_GNUC_PRINTF(2, 3);
gint g_vasprintf(gchar **strp, const gchar *format, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GPRINTF_H */
