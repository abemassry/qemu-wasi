/*
 * GLib miscellaneous utility functions shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GUTILS_H
#define GLIB_WASI_GUTILS_H

#include <stdarg.h>
#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  GSourceFunc callback (needed for timeout/idle stubs)              */
/* ------------------------------------------------------------------ */

#ifndef __G_SOURCE_FUNC_DEFINED
#define __G_SOURCE_FUNC_DEFINED
typedef gboolean (*GSourceFunc)(gpointer user_data);
#endif

/* ------------------------------------------------------------------ */
/*  User / host / application identity                                */
/* ------------------------------------------------------------------ */

const gchar *g_get_user_name       (void);
const gchar *g_get_real_name       (void);
const gchar *g_get_home_dir        (void);
const gchar *g_get_tmp_dir         (void);
const gchar *g_get_host_name       (void);

const gchar *g_get_prgname         (void);
void         g_set_prgname         (const gchar *prgname);
const gchar *g_get_application_name(void);
void         g_set_application_name(const gchar *application_name);

/* ------------------------------------------------------------------ */
/*  Path utilities                                                    */
/* ------------------------------------------------------------------ */

gchar   *g_path_get_basename  (const gchar *file_name);
gchar   *g_path_get_dirname   (const gchar *file_name);
gboolean g_path_is_absolute   (const gchar *file_name);

gchar   *g_build_filename     (const gchar *first_element, ...)
                               G_GNUC_NULL_TERMINATED;
gchar   *g_build_path         (const gchar *separator,
                               const gchar *first_element, ...)
                               G_GNUC_NULL_TERMINATED;

/* ------------------------------------------------------------------ */
/*  Environment                                                       */
/* ------------------------------------------------------------------ */

const gchar *g_getenv          (const gchar *variable);
gboolean     g_setenv          (const gchar *variable, const gchar *value,
                                gboolean overwrite);
void         g_unsetenv        (const gchar *variable);

gchar      **g_get_environ     (void);
const gchar *g_environ_getenv  (gchar **envp, const gchar *variable);

/* ------------------------------------------------------------------ */
/*  Time                                                              */
/* ------------------------------------------------------------------ */

gint64  g_get_monotonic_time   (void);
gint64  g_get_real_time        (void);
void    g_usleep               (gulong microseconds);

/* ------------------------------------------------------------------ */
/*  Miscellaneous                                                     */
/* ------------------------------------------------------------------ */

gchar  *g_find_program_in_path(const gchar *program);

void    g_qsort_with_data     (gconstpointer pbase, gint total_elems,
                                gsize size,
                                GCompareDataFunc compare_func,
                                gpointer user_data);

/* ------------------------------------------------------------------ */
/*  Base64                                                            */
/* ------------------------------------------------------------------ */

gchar  *g_base64_encode       (const guchar *data, gsize len);
guchar *g_base64_decode       (const gchar *text, gsize *out_len);

/* ------------------------------------------------------------------ */
/*  Main-loop stubs (WASI has no event loop)                          */
/*  NOTE: g_timeout_add, g_idle_add also declared in gmain.h         */
/* ------------------------------------------------------------------ */

#ifndef __GUTILS_MAIN_LOOP_STUBS_DECLARED
#define __GUTILS_MAIN_LOOP_STUBS_DECLARED
/* These are declared in gmain.h; avoid double-declaration. */
#endif

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GUTILS_H */
