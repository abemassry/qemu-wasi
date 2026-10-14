/*
 * GLib GTree (balanced binary tree) shim for QEMU wasm32-wasi builds.
 *
 * Declares the GTree API used by QEMU.  The implementation is an AVL
 * tree and lives in src/gtree.c.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GTREE_H
#define GLIB_WASI_GTREE_H

#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Traverse-order enum                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    G_IN_ORDER,
    G_PRE_ORDER,
    G_POST_ORDER,
    G_LEVEL_ORDER
} GTraverseType;

/* ------------------------------------------------------------------ */
/*  Traverse callback                                                 */
/* ------------------------------------------------------------------ */

typedef gboolean (*GTraverseFunc)(gpointer key, gpointer value,
                                  gpointer data);

/* ------------------------------------------------------------------ */
/*  Opaque tree type                                                  */
/* ------------------------------------------------------------------ */

typedef struct _GTree GTree;

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

GTree  *g_tree_new           (GCompareFunc key_compare_func);
GTree  *g_tree_new_with_data (GCompareDataFunc key_compare_func,
                              gpointer key_compare_data);
GTree  *g_tree_new_full      (GCompareDataFunc key_compare_func,
                              gpointer key_compare_data,
                              GDestroyNotify key_destroy_func,
                              GDestroyNotify value_destroy_func);

GTree  *g_tree_ref           (GTree *tree);
void    g_tree_unref         (GTree *tree);
void    g_tree_destroy       (GTree *tree);

/* ------------------------------------------------------------------ */
/*  Insert / Replace                                                  */
/* ------------------------------------------------------------------ */

void    g_tree_insert        (GTree *tree, gpointer key, gpointer value);
void    g_tree_replace       (GTree *tree, gpointer key, gpointer value);

/* ------------------------------------------------------------------ */
/*  Remove / Steal                                                    */
/* ------------------------------------------------------------------ */

gboolean g_tree_remove       (GTree *tree, gconstpointer key);
gboolean g_tree_steal        (GTree *tree, gconstpointer key);

/* ------------------------------------------------------------------ */
/*  Lookup                                                            */
/* ------------------------------------------------------------------ */

gpointer g_tree_lookup           (GTree *tree, gconstpointer key);
gboolean g_tree_lookup_extended  (GTree *tree, gconstpointer lookup_key,
                                  gpointer *orig_key, gpointer *value);

/* ------------------------------------------------------------------ */
/*  Traversal / Search                                                */
/* ------------------------------------------------------------------ */

void     g_tree_foreach      (GTree *tree, GTraverseFunc func,
                              gpointer user_data);
void     g_tree_traverse     (GTree *tree, GTraverseFunc traverse_func,
                              GTraverseType traverse_type,
                              gpointer user_data);
gpointer g_tree_search       (GTree *tree, GCompareFunc search_func,
                              gconstpointer user_data);

/* ------------------------------------------------------------------ */
/*  Size / Height                                                     */
/* ------------------------------------------------------------------ */

gint     g_tree_height       (GTree *tree);
gint     g_tree_nnodes       (GTree *tree);

#ifdef __cplusplus
}
#endif

#endif /* GLIB_WASI_GTREE_H */
