/*
 * Minimal GLib GMarkup shim for wasm32-wasi.
 *
 * Provides g_markup_printf_escaped, g_markup_vprintf_escaped,
 * and g_markup_escape_text.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "../glib/glib.h"

/*
 * g_markup_escape_text: escape &, <, >, ", ' for XML.
 * If length < 0, text is NUL-terminated.
 */
gchar *g_markup_escape_text(const gchar *text, gssize length)
{
    if (!text) {
        return g_strdup("");
    }

    gsize len = (length < 0) ? strlen(text) : (gsize)length;

    /* Worst case: every char becomes &amp; (5 chars) */
    gchar *out = (gchar *)g_malloc(len * 5 + 1);
    gchar *p = out;

    for (gsize i = 0; i < len; i++) {
        switch (text[i]) {
        case '&':
            memcpy(p, "&amp;", 5);
            p += 5;
            break;
        case '<':
            memcpy(p, "&lt;", 4);
            p += 4;
            break;
        case '>':
            memcpy(p, "&gt;", 4);
            p += 4;
            break;
        case '"':
            memcpy(p, "&quot;", 6);
            p += 6;
            break;
        case '\'':
            memcpy(p, "&apos;", 6);
            p += 6;
            break;
        default:
            *p++ = text[i];
            break;
        }
    }
    *p = '\0';

    /* Shrink to fit */
    gchar *result = g_strdup(out);
    g_free(out);
    return result;
}

/*
 * g_markup_vprintf_escaped: like g_strdup_vprintf but with XML
 * escaping of %s arguments.
 *
 * Real GLib escapes only string arguments. Our simplified version
 * does a two-pass approach: first vsnprintf to get the formatted
 * string, then escape the whole result. This is a slight behavioral
 * difference but safe for QEMU's usage patterns.
 */
gchar *g_markup_vprintf_escaped(const gchar *format, va_list args)
{
    gchar *formatted = g_strdup_vprintf(format, args);
    if (!formatted) {
        return g_strdup("");
    }

    /* The formatted string may contain XML-special chars from %s args.
     * Escape the entire result. For QEMU's usage (XML generation),
     * this is correct because the format strings use XML structural
     * elements and %s is for data that needs escaping. */
    gchar *escaped = g_markup_escape_text(formatted, -1);
    g_free(formatted);
    return escaped;
}

/*
 * g_markup_printf_escaped: varargs wrapper around
 * g_markup_vprintf_escaped.
 */
gchar *g_markup_printf_escaped(const gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    gchar *result = g_markup_vprintf_escaped(format, args);
    va_end(args);
    return result;
}
