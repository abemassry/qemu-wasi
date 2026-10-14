/*
 * GLib atomic operations shim for QEMU wasm32-wasi builds.
 *
 * wasm32-wasi is single-threaded, so all atomic operations reduce to
 * plain memory reads and writes.  No .c file is needed.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GATOMIC_H
#define GLIB_WASI_GATOMIC_H

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

/* ------------------------------------------------------------------ */
/*  Integer atomics (single-threaded: plain memory operations)        */
/* ------------------------------------------------------------------ */

#define g_atomic_int_get(ptr)           (*(ptr))
#define g_atomic_int_set(ptr, val)      (*(ptr) = (val))
#define g_atomic_int_inc(ptr)           ((*(ptr))++)
#define g_atomic_int_dec_and_test(ptr)  ((--(*(ptr))) == 0)

/*
 * The add / and / or / xor macros return the OLD value, so we use
 * GNU statement expressions.
 */
#define g_atomic_int_add(ptr, val) \
    ({ gint _gai_old = *(ptr); *(ptr) += (val); _gai_old; })

#define g_atomic_int_and(ptr, val) \
    ({ guint _gaa_old = *(guint *)(ptr); *(guint *)(ptr) &= (val); _gaa_old; })

#define g_atomic_int_or(ptr, val) \
    ({ guint _gao_old = *(guint *)(ptr); *(guint *)(ptr) |= (val); _gao_old; })

#define g_atomic_int_xor(ptr, val) \
    ({ guint _gax_old = *(guint *)(ptr); *(guint *)(ptr) ^= (val); _gax_old; })

#define g_atomic_int_compare_and_exchange(ptr, oldval, newval) \
    ((*(ptr) == (oldval)) ? (*(ptr) = (newval), TRUE) : FALSE)

#define g_atomic_int_compare_and_exchange_full(ptr, oldval, newval, preval) \
    (*(preval) = *(ptr), \
     ((*(ptr) == (oldval)) ? (*(ptr) = (newval), TRUE) : FALSE))

#define g_atomic_int_exchange(ptr, newval) \
    ({ gint _gie_old = *(ptr); *(ptr) = (newval); _gie_old; })

/* ------------------------------------------------------------------ */
/*  Pointer atomics (single-threaded: plain memory operations)        */
/* ------------------------------------------------------------------ */

#define g_atomic_pointer_get(ptr)           (*(ptr))
#define g_atomic_pointer_set(ptr, val)      (*(ptr) = (val))

#define g_atomic_pointer_compare_and_exchange(ptr, oldval, newval) \
    ((*(ptr) == (oldval)) ? (*(ptr) = (newval), TRUE) : FALSE)

#define g_atomic_pointer_compare_and_exchange_full(ptr, oldval, newval, preval) \
    (*(preval) = *(ptr), \
     ((*(ptr) == (oldval)) ? (*(ptr) = (newval), TRUE) : FALSE))

#define g_atomic_pointer_exchange(ptr, newval) \
    ({ gpointer _gpe_old = *(ptr); *(ptr) = (newval); _gpe_old; })

#define g_atomic_pointer_add(ptr, val) \
    ({ gsize _gpa_old = (gsize)*(ptr); \
       *(ptr) = (gpointer)((gsize)*(ptr) + (val)); \
       (gssize)_gpa_old; })

#define g_atomic_pointer_and(ptr, val) \
    ({ gsize _gpn_old = (gsize)*(ptr); \
       *(ptr) = (gpointer)((gsize)*(ptr) & (val)); \
       _gpn_old; })

#define g_atomic_pointer_or(ptr, val) \
    ({ gsize _gpo_old = (gsize)*(ptr); \
       *(ptr) = (gpointer)((gsize)*(ptr) | (val)); \
       _gpo_old; })

#define g_atomic_pointer_xor(ptr, val) \
    ({ gsize _gpx_old = (gsize)*(ptr); \
       *(ptr) = (gpointer)((gsize)*(ptr) ^ (val)); \
       _gpx_old; })

#endif /* GLIB_WASI_GATOMIC_H */
