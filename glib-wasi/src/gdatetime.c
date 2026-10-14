/*
 * GDateTime minimal implementation for QEMU wasm32-wasi.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "../glib/glib.h"

/* Internal GDateTime structure */
struct _GDateTime {
    gint64 unix_usec;   /* microseconds since Unix epoch (UTC) */
    gint   ref_count;
    gint   tz_offset;   /* timezone offset in seconds (0 for UTC) */
};

static GDateTime *gdatetime_new_from_usec(gint64 unix_usec, gint tz_offset)
{
    GDateTime *dt = (GDateTime *)g_malloc(sizeof(GDateTime));
    dt->unix_usec = unix_usec;
    dt->ref_count = 1;
    dt->tz_offset = tz_offset;
    return dt;
}

GDateTime *g_date_time_new_from_unix_utc(gint64 t)
{
    return gdatetime_new_from_usec(t * G_TIME_SPAN_SECOND, 0);
}

GDateTime *g_date_time_new_from_unix_local(gint64 t)
{
    /* WASI: no real timezone support, treat as UTC */
    return gdatetime_new_from_usec(t * G_TIME_SPAN_SECOND, 0);
}

GDateTime *g_date_time_new_now_utc(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return gdatetime_new_from_usec(0, 0);
    }
    gint64 usec = (gint64)ts.tv_sec * G_TIME_SPAN_SECOND +
                  (gint64)(ts.tv_nsec / 1000);
    return gdatetime_new_from_usec(usec, 0);
}

GDateTime *g_date_time_new_now_local(void)
{
    /* WASI: no timezone, same as UTC */
    return g_date_time_new_now_utc();
}

gint64 g_date_time_to_unix(GDateTime *dt)
{
    if (dt == NULL) return 0;
    return dt->unix_usec / G_TIME_SPAN_SECOND;
}

gint g_date_time_get_microsecond(GDateTime *dt)
{
    if (dt == NULL) return 0;
    gint64 remainder = dt->unix_usec % G_TIME_SPAN_SECOND;
    if (remainder < 0) remainder += G_TIME_SPAN_SECOND;
    return (gint)remainder;
}

gint g_date_time_get_second(GDateTime *dt)
{
    if (dt == NULL) return 0;
    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_sec;
}

gint g_date_time_get_minute(GDateTime *dt)
{
    if (dt == NULL) return 0;
    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_min;
}

gint g_date_time_get_hour(GDateTime *dt)
{
    if (dt == NULL) return 0;
    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_hour;
}

gint g_date_time_get_day_of_month(GDateTime *dt)
{
    if (dt == NULL) return 0;
    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_mday;
}

gint g_date_time_get_month(GDateTime *dt)
{
    if (dt == NULL) return 0;
    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_mon + 1;  /* GLib months are 1-based */
}

gint g_date_time_get_year(GDateTime *dt)
{
    if (dt == NULL) return 0;
    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_year + 1900;
}

void g_date_time_unref(GDateTime *dt)
{
    if (dt == NULL) return;
    if (--dt->ref_count <= 0) {
        g_free(dt);
    }
}

gchar *g_date_time_format(GDateTime *dt, const gchar *format)
{
    if (dt == NULL || format == NULL) return g_strdup("");

    time_t t = (time_t)g_date_time_to_unix(dt);
    struct tm tm;
    gmtime_r(&t, &tm);

    char buf[256];
    size_t n = strftime(buf, sizeof(buf), format, &tm);
    if (n == 0) {
        return g_strdup("");
    }
    return g_strdup(buf);
}

gchar *g_date_time_format_iso8601(GDateTime *dt)
{
    return g_date_time_format(dt, "%Y-%m-%dT%H:%M:%S");
}
