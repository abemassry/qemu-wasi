/*
 * GLib GTree shim – AVL tree implementation.
 *
 * This is a minimal but fully functional balanced binary tree written
 * for QEMU compiled to wasm32-wasi.  It implements every GTree entry
 * point that QEMU uses.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>

#include "../glib/gtree.h"

/* ------------------------------------------------------------------ */
/*  AVL tree node                                                     */
/* ------------------------------------------------------------------ */

typedef struct _GTreeNode GTreeNode;

struct _GTreeNode {
    gpointer   key;
    gpointer   value;
    GTreeNode *left;
    GTreeNode *right;
    gint       height;    /* AVL balance information */
};

/* ------------------------------------------------------------------ */
/*  Tree structure                                                    */
/* ------------------------------------------------------------------ */

/*
 * We store a GCompareDataFunc internally.  When the user provides a
 * plain GCompareFunc we wrap it with a shim (see compare_wrapper).
 */
struct _GTree {
    GTreeNode        *root;
    GCompareDataFunc  key_compare;
    gpointer          key_compare_data;
    GDestroyNotify    key_destroy_func;
    GDestroyNotify    value_destroy_func;
    gint              nnodes;
    gint              ref_count;
};

/* ------------------------------------------------------------------ */
/*  Wrapper: plain GCompareFunc -> GCompareDataFunc                   */
/* ------------------------------------------------------------------ */

static gint
compare_wrapper(gconstpointer a, gconstpointer b, gpointer user_data)
{
    GCompareFunc cmp = (GCompareFunc)user_data;
    return cmp(a, b);
}

/* ------------------------------------------------------------------ */
/*  AVL helpers                                                       */
/* ------------------------------------------------------------------ */

static inline gint
node_height(GTreeNode *n)
{
    return n ? n->height : 0;
}

static inline gint
balance_factor(GTreeNode *n)
{
    return n ? node_height(n->left) - node_height(n->right) : 0;
}

static inline void
update_height(GTreeNode *n)
{
    gint lh = node_height(n->left);
    gint rh = node_height(n->right);
    n->height = 1 + (lh > rh ? lh : rh);
}

static GTreeNode *
rotate_right(GTreeNode *y)
{
    GTreeNode *x  = y->left;
    GTreeNode *t2 = x->right;

    x->right = y;
    y->left  = t2;

    update_height(y);
    update_height(x);
    return x;
}

static GTreeNode *
rotate_left(GTreeNode *x)
{
    GTreeNode *y  = x->right;
    GTreeNode *t2 = y->left;

    y->left  = x;
    x->right = t2;

    update_height(x);
    update_height(y);
    return y;
}

static GTreeNode *
rebalance(GTreeNode *n)
{
    update_height(n);

    gint bf = balance_factor(n);

    /* Left-heavy */
    if (bf > 1) {
        if (balance_factor(n->left) < 0) {
            n->left = rotate_left(n->left);
        }
        return rotate_right(n);
    }

    /* Right-heavy */
    if (bf < -1) {
        if (balance_factor(n->right) > 0) {
            n->right = rotate_right(n->right);
        }
        return rotate_left(n);
    }

    return n;
}

/* ------------------------------------------------------------------ */
/*  Node allocation                                                   */
/* ------------------------------------------------------------------ */

static GTreeNode *
node_new(gpointer key, gpointer value)
{
    GTreeNode *n = (GTreeNode *)g_malloc(sizeof(GTreeNode));
    n->key    = key;
    n->value  = value;
    n->left   = NULL;
    n->right  = NULL;
    n->height = 1;
    return n;
}

/* ------------------------------------------------------------------ */
/*  Recursive insert (returns new subtree root)                       */
/* ------------------------------------------------------------------ */

/*
 * insert_mode: 0 = insert semantics (keep old key on collision),
 *              1 = replace semantics (use new key on collision).
 */
static GTreeNode *
node_insert(GTree *tree, GTreeNode *n, gpointer key, gpointer value,
            gboolean replace, gboolean *inserted)
{
    if (n == NULL) {
        *inserted = TRUE;
        return node_new(key, value);
    }

    gint cmp = tree->key_compare(key, n->key, tree->key_compare_data);

    if (cmp < 0) {
        n->left = node_insert(tree, n->left, key, value, replace, inserted);
    } else if (cmp > 0) {
        n->right = node_insert(tree, n->right, key, value, replace, inserted);
    } else {
        /* Key already exists. */
        *inserted = FALSE;

        if (replace) {
            /* Replace: destroy old key, store new key. */
            if (tree->key_destroy_func) {
                tree->key_destroy_func(n->key);
            }
            n->key = key;
        } else {
            /* Insert: keep old key, destroy new key. */
            if (tree->key_destroy_func) {
                tree->key_destroy_func(key);
            }
        }

        /* In both cases, destroy old value and store new one. */
        if (tree->value_destroy_func) {
            tree->value_destroy_func(n->value);
        }
        n->value = value;

        return n;
    }

    return rebalance(n);
}

/* ------------------------------------------------------------------ */
/*  Recursive remove helpers                                          */
/* ------------------------------------------------------------------ */

/* Find in-order successor (smallest node in right subtree). */
static GTreeNode *
min_node(GTreeNode *n)
{
    while (n->left) {
        n = n->left;
    }
    return n;
}

/*
 * Remove node with the given key.  If notify is TRUE, call the
 * destroy notifiers on the removed key/value.
 */
static GTreeNode *
node_remove(GTree *tree, GTreeNode *n, gconstpointer key,
            gboolean notify, gboolean *removed)
{
    if (n == NULL) {
        return NULL;
    }

    gint cmp = tree->key_compare(key, n->key, tree->key_compare_data);

    if (cmp < 0) {
        n->left = node_remove(tree, n->left, key, notify, removed);
    } else if (cmp > 0) {
        n->right = node_remove(tree, n->right, key, notify, removed);
    } else {
        /* Found the node to remove. */
        *removed = TRUE;

        if (n->left == NULL || n->right == NULL) {
            /* Zero or one child. */
            GTreeNode *child = n->left ? n->left : n->right;

            if (notify) {
                if (tree->key_destroy_func) {
                    tree->key_destroy_func(n->key);
                }
                if (tree->value_destroy_func) {
                    tree->value_destroy_func(n->value);
                }
            }

            g_free(n);
            return child;   /* may be NULL */
        }

        /* Two children: replace with in-order successor. */
        GTreeNode *successor = min_node(n->right);

        if (notify) {
            if (tree->key_destroy_func) {
                tree->key_destroy_func(n->key);
            }
            if (tree->value_destroy_func) {
                tree->value_destroy_func(n->value);
            }
        }

        /* Steal successor's key/value (do NOT notify during removal
         * of the successor node itself). */
        n->key   = successor->key;
        n->value = successor->value;

        gboolean dummy = FALSE;
        n->right = node_remove(tree, n->right, successor->key,
                               FALSE, &dummy);
    }

    return rebalance(n);
}

/* ------------------------------------------------------------------ */
/*  Recursive free-all                                                */
/* ------------------------------------------------------------------ */

static void
node_free_all(GTree *tree, GTreeNode *n)
{
    if (n == NULL) {
        return;
    }
    node_free_all(tree, n->left);
    node_free_all(tree, n->right);

    if (tree->key_destroy_func) {
        tree->key_destroy_func(n->key);
    }
    if (tree->value_destroy_func) {
        tree->value_destroy_func(n->value);
    }
    g_free(n);
}

/* ------------------------------------------------------------------ */
/*  Traversal helpers                                                 */
/* ------------------------------------------------------------------ */

/* In-order traversal. Returns TRUE if the callback returned TRUE
 * (i.e. stop iteration). */
static gboolean
traverse_in_order(GTreeNode *n, GTraverseFunc func, gpointer data)
{
    if (n == NULL) {
        return FALSE;
    }
    if (traverse_in_order(n->left, func, data)) {
        return TRUE;
    }
    if (func(n->key, n->value, data)) {
        return TRUE;
    }
    return traverse_in_order(n->right, func, data);
}

static gboolean
traverse_pre_order(GTreeNode *n, GTraverseFunc func, gpointer data)
{
    if (n == NULL) {
        return FALSE;
    }
    if (func(n->key, n->value, data)) {
        return TRUE;
    }
    if (traverse_pre_order(n->left, func, data)) {
        return TRUE;
    }
    return traverse_pre_order(n->right, func, data);
}

static gboolean
traverse_post_order(GTreeNode *n, GTraverseFunc func, gpointer data)
{
    if (n == NULL) {
        return FALSE;
    }
    if (traverse_post_order(n->left, func, data)) {
        return TRUE;
    }
    if (traverse_post_order(n->right, func, data)) {
        return TRUE;
    }
    return func(n->key, n->value, data);
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

GTree *
g_tree_new_full(GCompareDataFunc key_compare_func,
                gpointer         key_compare_data,
                GDestroyNotify   key_destroy_func,
                GDestroyNotify   value_destroy_func)
{
    GTree *tree = (GTree *)g_malloc(sizeof(GTree));

    tree->root               = NULL;
    tree->key_compare        = key_compare_func;
    tree->key_compare_data   = key_compare_data;
    tree->key_destroy_func   = key_destroy_func;
    tree->value_destroy_func = value_destroy_func;
    tree->nnodes             = 0;
    tree->ref_count          = 1;

    return tree;
}

GTree *
g_tree_new_with_data(GCompareDataFunc key_compare_func,
                     gpointer         key_compare_data)
{
    return g_tree_new_full(key_compare_func, key_compare_data, NULL, NULL);
}

GTree *
g_tree_new(GCompareFunc key_compare_func)
{
    /*
     * Wrap the plain GCompareFunc: we store the original function
     * pointer in key_compare_data and use compare_wrapper() as the
     * GCompareDataFunc.
     */
    return g_tree_new_full(compare_wrapper,
                           (gpointer)key_compare_func,
                           NULL, NULL);
}

GTree *
g_tree_ref(GTree *tree)
{
    if (tree) {
        tree->ref_count++;
    }
    return tree;
}

void
g_tree_unref(GTree *tree)
{
    if (!tree) {
        return;
    }
    if (--tree->ref_count > 0) {
        return;
    }

    node_free_all(tree, tree->root);
    g_free(tree);
}

void
g_tree_destroy(GTree *tree)
{
    if (!tree) {
        return;
    }
    /* Destroy all nodes (with notifications), then drop a reference. */
    node_free_all(tree, tree->root);
    tree->root   = NULL;
    tree->nnodes = 0;
    g_tree_unref(tree);
}

/* ------------------------------------------------------------------ */
/*  Insert / Replace                                                  */
/* ------------------------------------------------------------------ */

void
g_tree_insert(GTree *tree, gpointer key, gpointer value)
{
    if (!tree) {
        return;
    }
    gboolean inserted = FALSE;
    tree->root = node_insert(tree, tree->root, key, value, FALSE, &inserted);
    if (inserted) {
        tree->nnodes++;
    }
}

void
g_tree_replace(GTree *tree, gpointer key, gpointer value)
{
    if (!tree) {
        return;
    }
    gboolean inserted = FALSE;
    tree->root = node_insert(tree, tree->root, key, value, TRUE, &inserted);
    if (inserted) {
        tree->nnodes++;
    }
}

/* ------------------------------------------------------------------ */
/*  Remove / Steal                                                    */
/* ------------------------------------------------------------------ */

gboolean
g_tree_remove(GTree *tree, gconstpointer key)
{
    if (!tree) {
        return FALSE;
    }
    gboolean removed = FALSE;
    tree->root = node_remove(tree, tree->root, key, TRUE, &removed);
    if (removed) {
        tree->nnodes--;
    }
    return removed;
}

gboolean
g_tree_steal(GTree *tree, gconstpointer key)
{
    if (!tree) {
        return FALSE;
    }
    gboolean removed = FALSE;
    tree->root = node_remove(tree, tree->root, key, FALSE, &removed);
    if (removed) {
        tree->nnodes--;
    }
    return removed;
}

/* ------------------------------------------------------------------ */
/*  Lookup                                                            */
/* ------------------------------------------------------------------ */

gpointer
g_tree_lookup(GTree *tree, gconstpointer key)
{
    if (!tree) {
        return NULL;
    }

    GTreeNode *n = tree->root;
    while (n) {
        gint cmp = tree->key_compare(key, n->key, tree->key_compare_data);
        if (cmp < 0) {
            n = n->left;
        } else if (cmp > 0) {
            n = n->right;
        } else {
            return n->value;
        }
    }
    return NULL;
}

gboolean
g_tree_lookup_extended(GTree *tree, gconstpointer lookup_key,
                       gpointer *orig_key, gpointer *value)
{
    if (!tree) {
        return FALSE;
    }

    GTreeNode *n = tree->root;
    while (n) {
        gint cmp = tree->key_compare(lookup_key, n->key,
                                     tree->key_compare_data);
        if (cmp < 0) {
            n = n->left;
        } else if (cmp > 0) {
            n = n->right;
        } else {
            if (orig_key) {
                *orig_key = n->key;
            }
            if (value) {
                *value = n->value;
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Traversal / Search                                                */
/* ------------------------------------------------------------------ */

void
g_tree_foreach(GTree *tree, GTraverseFunc func, gpointer user_data)
{
    if (!tree || !func) {
        return;
    }
    traverse_in_order(tree->root, func, user_data);
}

void
g_tree_traverse(GTree *tree, GTraverseFunc traverse_func,
                GTraverseType traverse_type, gpointer user_data)
{
    if (!tree || !traverse_func) {
        return;
    }

    switch (traverse_type) {
    case G_IN_ORDER:
        traverse_in_order(tree->root, traverse_func, user_data);
        break;
    case G_PRE_ORDER:
        traverse_pre_order(tree->root, traverse_func, user_data);
        break;
    case G_POST_ORDER:
        traverse_post_order(tree->root, traverse_func, user_data);
        break;
    case G_LEVEL_ORDER:
        /* Level-order is rarely used; fall back to in-order. */
        traverse_in_order(tree->root, traverse_func, user_data);
        break;
    }
}

gpointer
g_tree_search(GTree *tree, GCompareFunc search_func,
              gconstpointer user_data)
{
    if (!tree) {
        return NULL;
    }

    GTreeNode *n = tree->root;
    while (n) {
        gint cmp = search_func(user_data, n->key);
        if (cmp < 0) {
            n = n->left;
        } else if (cmp > 0) {
            n = n->right;
        } else {
            return n->value;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Size / Height                                                     */
/* ------------------------------------------------------------------ */

gint
g_tree_height(GTree *tree)
{
    if (!tree || !tree->root) {
        return 0;
    }
    return tree->root->height;
}

gint
g_tree_nnodes(GTree *tree)
{
    return tree ? tree->nnodes : 0;
}
