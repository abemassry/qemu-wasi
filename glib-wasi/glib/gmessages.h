/*
 * GLib assertion / logging shim for QEMU wasm32-wasi builds.
 *
 * Provides g_assert*, g_return_if_fail, g_log, g_warning, etc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GMESSAGES_H
#define GLIB_WASI_GMESSAGES_H

#include <stdio.h>
#include <stdarg.h>

/*
 * Basic GLib types -- these will also appear in the top-level glib.h;
 * guard them so they can be included from either place.
 */
#ifndef GLIB_WASI_BASIC_TYPES_DEFINED
#define GLIB_WASI_BASIC_TYPES_DEFINED

typedef int            gboolean;
typedef int            gint;
typedef unsigned int   guint;
typedef void *         gpointer;
typedef const void *   gconstpointer;
typedef unsigned long  gsize;
typedef long           gssize;
typedef char           gchar;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* GLIB_WASI_BASIC_TYPES_DEFINED */

/* We need G_STRFUNC; guard in case glib.h was already included. */
#ifndef G_STRFUNC
#define G_STRFUNC ((const char *)(__func__))
#endif

/* We need G_GNUC_PRINTF; guard in case glib.h was already included. */
#ifndef G_GNUC_PRINTF
#if defined(__GNUC__) || defined(__clang__)
#define G_GNUC_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define G_GNUC_PRINTF(f, a)
#endif
#endif

/* ------------------------------------------------------------------ */
/*  Log level flags                                                   */
/* ------------------------------------------------------------------ */

typedef enum {
    G_LOG_FLAG_RECURSION = 1 << 0,
    G_LOG_FLAG_FATAL     = 1 << 1,
    G_LOG_LEVEL_ERROR    = 1 << 2,   /* always fatal */
    G_LOG_LEVEL_CRITICAL = 1 << 3,
    G_LOG_LEVEL_WARNING  = 1 << 4,
    G_LOG_LEVEL_MESSAGE  = 1 << 5,
    G_LOG_LEVEL_INFO     = 1 << 6,
    G_LOG_LEVEL_DEBUG    = 1 << 7,
    G_LOG_LEVEL_MASK     = ~(G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL)
} GLogLevelFlags;

/* ------------------------------------------------------------------ */
/*  Log handler callback type                                         */
/* ------------------------------------------------------------------ */

typedef void (*GLogFunc)(const gchar    *log_domain,
                         GLogLevelFlags  log_level,
                         const gchar    *message,
                         gpointer        user_data);

/* ------------------------------------------------------------------ */
/*  Function declarations (implementations in src/gmessages.c)        */
/* ------------------------------------------------------------------ */

void g_log(const gchar *log_domain, GLogLevelFlags log_level,
           const gchar *format, ...) G_GNUC_PRINTF(3, 4);

void g_logv(const gchar *log_domain, GLogLevelFlags log_level,
            const gchar *format, va_list args);

void g_assertion_message(const gchar *domain,
                         const gchar *file,
                         int          line,
                         const gchar *func,
                         const gchar *expr);

guint    g_log_set_handler(const gchar    *log_domain,
                           GLogLevelFlags  log_levels,
                           GLogFunc        log_func,
                           gpointer        user_data);

void     g_log_remove_handler(const gchar *log_domain,
                              guint        handler_id);

GLogFunc g_log_set_default_handler(GLogFunc log_func,
                                   gpointer user_data);

void     g_log_default_handler(const gchar    *log_domain,
                               GLogLevelFlags  log_level,
                               const gchar    *message,
                               gpointer        unused_data);

/* ------------------------------------------------------------------ */
/*  Convenience logging macros                                        */
/* ------------------------------------------------------------------ */

#define g_error(...)    g_log("", G_LOG_LEVEL_ERROR,    __VA_ARGS__)
#define g_message(...)  g_log("", G_LOG_LEVEL_MESSAGE,  __VA_ARGS__)
#define g_critical(...) g_log("", G_LOG_LEVEL_CRITICAL, __VA_ARGS__)
#define g_warning(...)  g_log("", G_LOG_LEVEL_WARNING,  __VA_ARGS__)
#define g_info(...)     g_log("", G_LOG_LEVEL_INFO,     __VA_ARGS__)
#define g_debug(...)    g_log("", G_LOG_LEVEL_DEBUG,    __VA_ARGS__)

#define g_print(...)    fprintf(stdout, __VA_ARGS__)
#define g_printerr(...) fprintf(stderr, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/*  Assertions                                                        */
/* ------------------------------------------------------------------ */

#define g_assert(expr) \
    do { \
        if (!(expr)) { \
            g_assertion_message("", __FILE__, __LINE__, G_STRFUNC, #expr); \
        } \
    } while (0)

#define g_assert_cmpint(a, op, b)   g_assert((a) op (b))
#define g_assert_cmpuint(a, op, b)  g_assert((guint)(a) op (guint)(b))
#define g_assert_cmphex(a, op, b)   g_assert((a) op (b))
#define g_assert_cmpstr(a, op, b)   g_assert(g_strcmp0((a), (b)) op 0)

#define g_assert_not_reached() \
    g_assertion_message("", __FILE__, __LINE__, G_STRFUNC, \
                        "code should not be reached")

#define g_assert_null(x)    g_assert((x) == NULL)
#define g_assert_nonnull(x) g_assert((x) != NULL)
#define g_assert_true(x)    g_assert(x)
#define g_assert_false(x)   g_assert(!(x))

/* ------------------------------------------------------------------ */
/*  Precondition / postcondition checks                               */
/* ------------------------------------------------------------------ */

#define g_return_if_fail(expr) \
    do { \
        if (!(expr)) { \
            g_log("", G_LOG_LEVEL_CRITICAL, \
                  "%s: assertion '%s' failed", G_STRFUNC, #expr); \
            return; \
        } \
    } while (0)

#define g_return_val_if_fail(expr, val) \
    do { \
        if (!(expr)) { \
            g_log("", G_LOG_LEVEL_CRITICAL, \
                  "%s: assertion '%s' failed", G_STRFUNC, #expr); \
            return (val); \
        } \
    } while (0)

#define g_return_if_reached() \
    do { \
        g_log("", G_LOG_LEVEL_CRITICAL, \
              "%s: should not be reached", G_STRFUNC); \
        return; \
    } while (0)

#define g_return_val_if_reached(val) \
    do { \
        g_log("", G_LOG_LEVEL_CRITICAL, \
              "%s: should not be reached", G_STRFUNC); \
        return (val); \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Soft warnings                                                     */
/* ------------------------------------------------------------------ */

#define g_warn_if_fail(expr) \
    do { \
        if (!(expr)) \
            g_log("", G_LOG_LEVEL_WARNING, \
                  "%s: assertion '%s' failed", G_STRFUNC, #expr); \
    } while (0)

#define g_warn_if_reached() \
    g_log("", G_LOG_LEVEL_WARNING, "%s: should not be reached", G_STRFUNC)

#endif /* GLIB_WASI_GMESSAGES_H */
