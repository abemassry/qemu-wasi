/*
 * GLib assertion / logging shim – wasm32-wasi implementation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "../glib/gmessages.h"

/* ------------------------------------------------------------------ */
/*  Internal state for custom log handlers                            */
/* ------------------------------------------------------------------ */

static GLogFunc _default_log_func = NULL;
static gpointer _default_log_data = NULL;

/* ------------------------------------------------------------------ */
/*  Default log handler                                               */
/* ------------------------------------------------------------------ */

static const char *
_log_level_prefix(GLogLevelFlags log_level)
{
    if (log_level & G_LOG_LEVEL_ERROR)    return "ERROR";
    if (log_level & G_LOG_LEVEL_CRITICAL) return "CRITICAL";
    if (log_level & G_LOG_LEVEL_WARNING)  return "WARNING";
    if (log_level & G_LOG_LEVEL_MESSAGE)  return "Message";
    if (log_level & G_LOG_LEVEL_INFO)     return "INFO";
    if (log_level & G_LOG_LEVEL_DEBUG)    return "DEBUG";
    return "LOG";
}

void
g_log_default_handler(const gchar    *log_domain,
                      GLogLevelFlags  log_level,
                      const gchar    *message,
                      gpointer        unused_data)
{
    (void)unused_data;

    const char *prefix = _log_level_prefix(log_level);

    if (log_domain && log_domain[0] != '\0') {
        fprintf(stderr, "** (%s): %s: %s\n", log_domain, prefix, message);
    } else {
        fprintf(stderr, "** %s: %s\n", prefix, message);
    }

    if (log_level & G_LOG_LEVEL_ERROR) {
        abort();
    }
}

/* ------------------------------------------------------------------ */
/*  g_logv / g_log                                                    */
/* ------------------------------------------------------------------ */

void
g_logv(const gchar    *log_domain,
       GLogLevelFlags  log_level,
       const gchar    *format,
       va_list         args)
{
    char buf[1024];
    vsnprintf(buf, sizeof(buf), format, args);

    if (_default_log_func) {
        _default_log_func(log_domain, log_level, buf, _default_log_data);
    } else {
        g_log_default_handler(log_domain, log_level, buf, NULL);
    }
}

void
g_log(const gchar    *log_domain,
      GLogLevelFlags  log_level,
      const gchar    *format, ...)
{
    va_list args;
    va_start(args, format);
    g_logv(log_domain, log_level, format, args);
    va_end(args);
}

/* ------------------------------------------------------------------ */
/*  Assertion failure                                                 */
/* ------------------------------------------------------------------ */

void
g_assertion_message(const gchar *domain,
                    const gchar *file,
                    int          line,
                    const gchar *func,
                    const gchar *expr)
{
    (void)domain;
    fprintf(stderr, "**\n** ERROR:%s:%d:%s: assertion failed: (%s)\n",
            file, line, func, expr);
    abort();
}

/* ------------------------------------------------------------------ */
/*  Handler registration                                              */
/* ------------------------------------------------------------------ */

guint
g_log_set_handler(const gchar    *log_domain,
                  GLogLevelFlags  log_levels,
                  GLogFunc        log_func,
                  gpointer        user_data)
{
    /*
     * Minimal implementation: we only support one global handler.
     * Domain-specific / level-specific routing is not implemented.
     */
    (void)log_domain;
    (void)log_levels;
    _default_log_func = log_func;
    _default_log_data = user_data;
    return 1; /* dummy handler id */
}

void
g_log_remove_handler(const gchar *log_domain, guint handler_id)
{
    (void)log_domain;
    (void)handler_id;
    /* No-op: we don't track individual handlers. */
}

GLogFunc
g_log_set_default_handler(GLogFunc log_func, gpointer user_data)
{
    GLogFunc old = _default_log_func;
    _default_log_func = log_func;
    _default_log_data = user_data;
    return old;
}
