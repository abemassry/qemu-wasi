/*
 * GLib random number generation shim for QEMU wasm32-wasi builds.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GRAND_H
#define GLIB_WASI_GRAND_H

#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Opaque random-state type                                          */
/* ------------------------------------------------------------------ */

typedef struct _GRand GRand;

/* ------------------------------------------------------------------ */
/*  GRand lifecycle                                                   */
/* ------------------------------------------------------------------ */

GRand   *g_rand_new                (void);
GRand   *g_rand_new_with_seed      (guint32 seed);
GRand   *g_rand_new_with_seed_array(const guint32 *seed, guint seed_length);
void     g_rand_free               (GRand *rand_);
void     g_rand_set_seed           (GRand *rand_, guint32 seed);

/* ------------------------------------------------------------------ */
/*  GRand number generators                                           */
/* ------------------------------------------------------------------ */

guint32  g_rand_int                (GRand *rand_);
gint32   g_rand_int_range          (GRand *rand_, gint32 begin, gint32 end);
gdouble  g_rand_double             (GRand *rand_);
gdouble  g_rand_double_range       (GRand *rand_, gdouble begin, gdouble end);

/* ------------------------------------------------------------------ */
/*  Global convenience (use a shared GRand instance)                  */
/* ------------------------------------------------------------------ */

guint32  g_random_int              (void);
gint32   g_random_int_range        (gint32 begin, gint32 end);
gdouble  g_random_double           (void);
gdouble  g_random_double_range     (gdouble begin, gdouble end);
void     g_random_set_seed         (guint32 seed);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GRAND_H */
