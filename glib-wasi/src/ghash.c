/*
 * GLib GHashTable shim – separate-chaining implementation.
 *
 * This is a minimal but fully functional hash table written for QEMU
 * compiled to wasm32-wasi.  It implements every GHashTable entry point
 * that QEMU uses.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Pull in our own header for the public API declarations and types. */
#include "../glib/ghash.h"

/* ------------------------------------------------------------------ */
/* GList minimal definition -- only what we need for get_keys/values. */
/* The real GList is in another translation unit; we reproduce just   */
/* enough of the struct here so we can build a singly-used list.      */
/* ------------------------------------------------------------------ */
struct _GList {
    gpointer  data;
    GList    *next;
    GList    *prev;
};

static GList *g_list_prepend_internal(GList *list, gpointer data)
{
    GList *node = (GList *)malloc(sizeof(GList));
    if (!node) {
        return list;
    }
    node->data = data;
    node->next = list;
    node->prev = NULL;
    if (list) {
        list->prev = node;
    }
    return node;
}

/* ------------------------------------------------------------------ */
/* Internal bucket node for separate chaining.                        */
/* ------------------------------------------------------------------ */
typedef struct _GHashNode GHashNode;

struct _GHashNode {
    gpointer   key;
    gpointer   value;
    guint      hash;       /* cached hash value                       */
    GHashNode *next;       /* next node in the same bucket            */
};

/* ------------------------------------------------------------------ */
/* Hash table structure.                                              */
/* ------------------------------------------------------------------ */
#define GHASH_INITIAL_NBUCKETS  64
#define GHASH_LOAD_FACTOR_NUM   3   /* grow when size/nbuckets > 3/4  */
#define GHASH_LOAD_FACTOR_DEN   4
#define GHASH_GROWTH_FACTOR     2

struct _GHashTable {
    GHashNode    **buckets;
    guint          nbuckets;
    guint          size;           /* number of stored entries         */
    gint           ref_count;

    GHashFunc      hash_func;
    GEqualFunc     key_equal_func;
    GDestroyNotify key_destroy_func;
    GDestroyNotify value_destroy_func;
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/* Map a full hash value to a bucket index. */
static inline guint bucket_index(GHashTable *ht, guint hash)
{
    return hash % ht->nbuckets;
}

/* Look up the node for a given key.  Optionally return the previous
 * node pointer (useful for unlinking during removal).                */
static GHashNode *find_node(GHashTable *ht,
                            gconstpointer key,
                            guint hash,
                            GHashNode **prev_out)
{
    guint idx = bucket_index(ht, hash);
    GHashNode *prev = NULL;
    GHashNode *node = ht->buckets[idx];

    while (node) {
        if (node->hash == hash && ht->key_equal_func(node->key, key)) {
            if (prev_out) {
                *prev_out = prev;
            }
            return node;
        }
        prev = node;
        node = node->next;
    }

    if (prev_out) {
        *prev_out = NULL;
    }
    return NULL;
}

/* Grow the bucket array and rehash every entry. */
static void resize(GHashTable *ht)
{
    guint new_nbuckets = ht->nbuckets * GHASH_GROWTH_FACTOR;
    GHashNode **new_buckets = (GHashNode **)calloc(new_nbuckets,
                                                   sizeof(GHashNode *));
    if (!new_buckets) {
        return;   /* allocation failure – keep existing table */
    }

    for (guint i = 0; i < ht->nbuckets; i++) {
        GHashNode *node = ht->buckets[i];
        while (node) {
            GHashNode *next = node->next;
            guint idx = node->hash % new_nbuckets;
            node->next = new_buckets[idx];
            new_buckets[idx] = node;
            node = next;
        }
    }

    free(ht->buckets);
    ht->buckets  = new_buckets;
    ht->nbuckets = new_nbuckets;
}

static inline void maybe_resize(GHashTable *ht)
{
    if (ht->size * GHASH_LOAD_FACTOR_DEN >
        ht->nbuckets * GHASH_LOAD_FACTOR_NUM) {
        resize(ht);
    }
}

/* Free all nodes, optionally calling the destroy notifiers. */
static void remove_all_nodes(GHashTable *ht, gboolean notify)
{
    for (guint i = 0; i < ht->nbuckets; i++) {
        GHashNode *node = ht->buckets[i];
        while (node) {
            GHashNode *next = node->next;
            if (notify) {
                if (ht->key_destroy_func) {
                    ht->key_destroy_func(node->key);
                }
                if (ht->value_destroy_func) {
                    ht->value_destroy_func(node->value);
                }
            }
            free(node);
            node = next;
        }
        ht->buckets[i] = NULL;
    }
    ht->size = 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

GHashTable *g_hash_table_new_full(GHashFunc      hash_func,
                                  GEqualFunc     key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func)
{
    GHashTable *ht = (GHashTable *)malloc(sizeof(GHashTable));
    if (!ht) {
        return NULL;
    }

    ht->nbuckets = GHASH_INITIAL_NBUCKETS;
    ht->buckets  = (GHashNode **)calloc(ht->nbuckets, sizeof(GHashNode *));
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }

    ht->size               = 0;
    ht->ref_count          = 1;
    ht->hash_func          = hash_func  ? hash_func  : g_direct_hash;
    ht->key_equal_func     = key_equal_func ? key_equal_func : g_direct_equal;
    ht->key_destroy_func   = key_destroy_func;
    ht->value_destroy_func = value_destroy_func;

    return ht;
}

GHashTable *g_hash_table_new(GHashFunc hash_func,
                             GEqualFunc key_equal_func)
{
    return g_hash_table_new_full(hash_func, key_equal_func, NULL, NULL);
}

GHashTable *g_hash_table_ref(GHashTable *hash_table)
{
    if (hash_table) {
        hash_table->ref_count++;
    }
    return hash_table;
}

void g_hash_table_unref(GHashTable *hash_table)
{
    if (!hash_table) {
        return;
    }
    if (--hash_table->ref_count > 0) {
        return;
    }
    remove_all_nodes(hash_table, TRUE);
    free(hash_table->buckets);
    free(hash_table);
}

void g_hash_table_destroy(GHashTable *hash_table)
{
    if (!hash_table) {
        return;
    }
    /* destroy removes all entries (with notifications) then drops one
     * reference.  If ref_count was already 1 the table is freed. */
    remove_all_nodes(hash_table, TRUE);
    g_hash_table_unref(hash_table);
}

/* ------------------------------------------------------------------ */
/* Insert / Replace                                                   */
/* ------------------------------------------------------------------ */

/*
 * g_hash_table_insert: if the key already exists, the *old* key is
 * kept and the new key is freed (if a key_destroy is set).  The value
 * is always replaced.
 *
 * Returns TRUE if the key did not exist (i.e. a new entry was added).
 */
gboolean g_hash_table_insert(GHashTable *hash_table,
                             gpointer key, gpointer value)
{
    if (!hash_table) {
        return FALSE;
    }

    guint hash = hash_table->hash_func(key);
    GHashNode *node = find_node(hash_table, key, hash, NULL);

    if (node) {
        /* Key already present – keep old key, replace value. */
        if (hash_table->key_destroy_func) {
            hash_table->key_destroy_func(key);
        }
        if (hash_table->value_destroy_func) {
            hash_table->value_destroy_func(node->value);
        }
        node->value = value;
        return FALSE;
    }

    /* New entry. */
    GHashNode *new_node = (GHashNode *)malloc(sizeof(GHashNode));
    if (!new_node) {
        return FALSE;
    }

    new_node->key   = key;
    new_node->value = value;
    new_node->hash  = hash;

    guint idx = bucket_index(hash_table, hash);
    new_node->next = hash_table->buckets[idx];
    hash_table->buckets[idx] = new_node;

    hash_table->size++;
    maybe_resize(hash_table);
    return TRUE;
}

/*
 * g_hash_table_replace: if the key already exists, *both* the old key
 * and old value are destroyed and the new key/value pair is stored.
 *
 * Returns TRUE if the key did not exist (i.e. a new entry was added).
 */
gboolean g_hash_table_replace(GHashTable *hash_table,
                              gpointer key, gpointer value)
{
    if (!hash_table) {
        return FALSE;
    }

    guint hash = hash_table->hash_func(key);
    GHashNode *node = find_node(hash_table, key, hash, NULL);

    if (node) {
        /* Replace both key and value. */
        if (hash_table->key_destroy_func) {
            hash_table->key_destroy_func(node->key);
        }
        if (hash_table->value_destroy_func) {
            hash_table->value_destroy_func(node->value);
        }
        node->key   = key;
        node->value = value;
        node->hash  = hash;
        return FALSE;
    }

    /* New entry. */
    GHashNode *new_node = (GHashNode *)malloc(sizeof(GHashNode));
    if (!new_node) {
        return FALSE;
    }

    new_node->key   = key;
    new_node->value = value;
    new_node->hash  = hash;

    guint idx = bucket_index(hash_table, hash);
    new_node->next = hash_table->buckets[idx];
    hash_table->buckets[idx] = new_node;

    hash_table->size++;
    maybe_resize(hash_table);
    return TRUE;
}

/*
 * g_hash_table_add: inserts key as both key and value.
 * Used for set-like semantics.
 */
gboolean g_hash_table_add(GHashTable *hash_table, gpointer key)
{
    return g_hash_table_replace(hash_table, key, key);
}

/* ------------------------------------------------------------------ */
/* Lookup                                                             */
/* ------------------------------------------------------------------ */

gpointer g_hash_table_lookup(GHashTable *hash_table,
                             gconstpointer key)
{
    if (!hash_table) {
        return NULL;
    }

    guint hash = hash_table->hash_func(key);
    GHashNode *node = find_node(hash_table, key, hash, NULL);
    return node ? node->value : NULL;
}

gboolean g_hash_table_lookup_extended(GHashTable *hash_table,
                                      gconstpointer lookup_key,
                                      gpointer *orig_key,
                                      gpointer *value)
{
    if (!hash_table) {
        return FALSE;
    }

    guint hash = hash_table->hash_func(lookup_key);
    GHashNode *node = find_node(hash_table, lookup_key, hash, NULL);

    if (node) {
        if (orig_key) {
            *orig_key = node->key;
        }
        if (value) {
            *value = node->value;
        }
        return TRUE;
    }
    return FALSE;
}

gboolean g_hash_table_contains(GHashTable *hash_table,
                               gconstpointer key)
{
    if (!hash_table) {
        return FALSE;
    }

    guint hash = hash_table->hash_func(key);
    return find_node(hash_table, key, hash, NULL) != NULL;
}

/* ------------------------------------------------------------------ */
/* Remove / Steal                                                     */
/* ------------------------------------------------------------------ */

/* Internal: remove a node, optionally calling destroy notifiers. */
static gboolean remove_internal(GHashTable *hash_table,
                                gconstpointer key,
                                gboolean notify)
{
    if (!hash_table) {
        return FALSE;
    }

    guint hash = hash_table->hash_func(key);
    GHashNode *prev = NULL;
    GHashNode *node = find_node(hash_table, key, hash, &prev);

    if (!node) {
        return FALSE;
    }

    /* Unlink from the chain. */
    guint idx = bucket_index(hash_table, hash);
    if (prev) {
        prev->next = node->next;
    } else {
        hash_table->buckets[idx] = node->next;
    }

    if (notify) {
        if (hash_table->key_destroy_func) {
            hash_table->key_destroy_func(node->key);
        }
        if (hash_table->value_destroy_func) {
            hash_table->value_destroy_func(node->value);
        }
    }

    free(node);
    hash_table->size--;
    return TRUE;
}

gboolean g_hash_table_remove(GHashTable *hash_table, gconstpointer key)
{
    return remove_internal(hash_table, key, TRUE);
}

gboolean g_hash_table_steal(GHashTable *hash_table, gconstpointer key)
{
    return remove_internal(hash_table, key, FALSE);
}

void g_hash_table_remove_all(GHashTable *hash_table)
{
    if (hash_table) {
        remove_all_nodes(hash_table, TRUE);
    }
}

void g_hash_table_steal_all(GHashTable *hash_table)
{
    if (hash_table) {
        remove_all_nodes(hash_table, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* Iteration                                                          */
/* ------------------------------------------------------------------ */

void g_hash_table_foreach(GHashTable *hash_table,
                          GHFunc func, gpointer user_data)
{
    if (!hash_table || !func) {
        return;
    }

    for (guint i = 0; i < hash_table->nbuckets; i++) {
        GHashNode *node = hash_table->buckets[i];
        while (node) {
            /* Grab next before calling func, in case func modifies the
             * value (though not the key). */
            GHashNode *next = node->next;
            func(node->key, node->value, user_data);
            node = next;
        }
    }
}

/* Internal helper for foreach_remove / foreach_steal. */
static guint foreach_remove_or_steal(GHashTable *hash_table,
                                     GHRFunc func,
                                     gpointer user_data,
                                     gboolean notify)
{
    if (!hash_table || !func) {
        return 0;
    }

    guint removed = 0;

    for (guint i = 0; i < hash_table->nbuckets; i++) {
        GHashNode *prev = NULL;
        GHashNode *node = hash_table->buckets[i];

        while (node) {
            GHashNode *next = node->next;

            if (func(node->key, node->value, user_data)) {
                /* Remove this node. */
                if (prev) {
                    prev->next = next;
                } else {
                    hash_table->buckets[i] = next;
                }

                if (notify) {
                    if (hash_table->key_destroy_func) {
                        hash_table->key_destroy_func(node->key);
                    }
                    if (hash_table->value_destroy_func) {
                        hash_table->value_destroy_func(node->value);
                    }
                }

                free(node);
                hash_table->size--;
                removed++;
                /* prev stays the same */
            } else {
                prev = node;
            }
            node = next;
        }
    }

    return removed;
}

guint g_hash_table_foreach_remove(GHashTable *hash_table,
                                  GHRFunc func, gpointer user_data)
{
    return foreach_remove_or_steal(hash_table, func, user_data, TRUE);
}

guint g_hash_table_foreach_steal(GHashTable *hash_table,
                                 GHRFunc func, gpointer user_data)
{
    return foreach_remove_or_steal(hash_table, func, user_data, FALSE);
}

GList *g_hash_table_get_keys(GHashTable *hash_table)
{
    GList *list = NULL;

    if (!hash_table) {
        return NULL;
    }

    for (guint i = 0; i < hash_table->nbuckets; i++) {
        GHashNode *node = hash_table->buckets[i];
        while (node) {
            list = g_list_prepend_internal(list, node->key);
            node = node->next;
        }
    }
    return list;
}

GList *g_hash_table_get_values(GHashTable *hash_table)
{
    GList *list = NULL;

    if (!hash_table) {
        return NULL;
    }

    for (guint i = 0; i < hash_table->nbuckets; i++) {
        GHashNode *node = hash_table->buckets[i];
        while (node) {
            list = g_list_prepend_internal(list, node->value);
            node = node->next;
        }
    }
    return list;
}

/* ------------------------------------------------------------------ */
/* Size                                                               */
/* ------------------------------------------------------------------ */

guint g_hash_table_size(GHashTable *hash_table)
{
    return hash_table ? hash_table->size : 0;
}

/* ------------------------------------------------------------------ */
/* Built-in hash and equality functions                               */
/* ------------------------------------------------------------------ */

/*
 * g_str_hash -- djb2 hash by Dan Bernstein.
 */
guint g_str_hash(gconstpointer v)
{
    const char *p = (const char *)v;
    guint hash = 5381;

    while (*p) {
        hash = (hash << 5) + hash + (unsigned char)*p;
        p++;
    }
    return hash;
}

gboolean g_str_equal(gconstpointer v1, gconstpointer v2)
{
    return strcmp((const char *)v1, (const char *)v2) == 0;
}

guint g_int_hash(gconstpointer v)
{
    /* Thomas Wang's 32-bit integer hash. */
    guint key = *(const guint *)v;
    key = ~key + (key << 15);
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057;
    key = key ^ (key >> 16);
    return key;
}

gboolean g_int_equal(gconstpointer v1, gconstpointer v2)
{
    return *(const int *)v1 == *(const int *)v2;
}

guint g_int64_hash(gconstpointer v)
{
    const uint64_t *p = (const uint64_t *)v;
    uint64_t val = *p;

    /* Mix the 64-bit value down to 32 bits.  FNV-1a style. */
    return (guint)(val ^ (val >> 32));
}

gboolean g_int64_equal(gconstpointer v1, gconstpointer v2)
{
    return *(const int64_t *)v1 == *(const int64_t *)v2;
}

guint g_direct_hash(gconstpointer v)
{
    /* GLib uses GPOINTER_TO_UINT which just casts the pointer. */
    return (guint)(uintptr_t)v;
}

gboolean g_direct_equal(gconstpointer v1, gconstpointer v2)
{
    return v1 == v2;
}

/* ------------------------------------------------------------------ */
/* GHashTableIter implementation                                      */
/* ------------------------------------------------------------------ */

/*
 * The GHashTableIter fields map to:
 *   dummy1 → GHashTable *
 *   dummy2 → current GHashNode *
 *   dummy3 → previous GHashNode * (for removal)
 *   dummy4 → current bucket index
 *   dummy5 → (unused)
 *   dummy6 → (unused)
 */

void g_hash_table_iter_init(GHashTableIter *iter, GHashTable *hash_table)
{
    iter->dummy1 = hash_table;
    iter->dummy2 = NULL;       /* current node */
    iter->dummy3 = NULL;       /* previous node */
    iter->dummy4 = -1;         /* bucket index, -1 = before start */
    iter->dummy5 = FALSE;
    iter->dummy6 = NULL;
}

gboolean g_hash_table_iter_next(GHashTableIter *iter,
                                gpointer *key,
                                gpointer *value)
{
    GHashTable *ht = (GHashTable *)iter->dummy1;
    GHashNode  *node = (GHashNode *)iter->dummy2;
    int         idx  = iter->dummy4;

    if (!ht) {
        return FALSE;
    }

    /* Advance: if we have a current node, go to next in chain */
    if (node) {
        iter->dummy3 = node;   /* previous = current before advancing */
        node = node->next;
    }

    /* If no more nodes in this chain, scan forward through buckets */
    while (!node) {
        idx++;
        if ((guint)idx >= ht->nbuckets) {
            /* No more buckets — iteration complete */
            iter->dummy2 = NULL;
            iter->dummy4 = idx;
            return FALSE;
        }
        node = ht->buckets[idx];
        iter->dummy3 = NULL;  /* at start of new bucket, no previous */
    }

    iter->dummy2 = node;
    iter->dummy4 = idx;

    if (key) {
        *key = node->key;
    }
    if (value) {
        *value = node->value;
    }
    return TRUE;
}

void g_hash_table_iter_remove(GHashTableIter *iter)
{
    GHashTable *ht   = (GHashTable *)iter->dummy1;
    GHashNode  *node = (GHashNode *)iter->dummy2;
    GHashNode  *prev = (GHashNode *)iter->dummy3;
    int         idx  = iter->dummy4;

    if (!ht || !node) {
        return;
    }

    /* Unlink node from chain */
    if (prev) {
        prev->next = node->next;
    } else {
        ht->buckets[idx] = node->next;
    }

    /* Call destroy notifiers */
    if (ht->key_destroy_func) {
        ht->key_destroy_func(node->key);
    }
    if (ht->value_destroy_func) {
        ht->value_destroy_func(node->value);
    }

    free(node);
    ht->size--;

    /* Update iterator: current becomes prev so _next() advances correctly */
    iter->dummy2 = prev;
}

void g_hash_table_iter_replace(GHashTableIter *iter, gpointer value)
{
    GHashTable *ht   = (GHashTable *)iter->dummy1;
    GHashNode  *node = (GHashNode *)iter->dummy2;

    if (!ht || !node) {
        return;
    }

    if (ht->value_destroy_func) {
        ht->value_destroy_func(node->value);
    }
    node->value = value;
}

void g_hash_table_iter_steal(GHashTableIter *iter)
{
    GHashTable *ht   = (GHashTable *)iter->dummy1;
    GHashNode  *node = (GHashNode *)iter->dummy2;
    GHashNode  *prev = (GHashNode *)iter->dummy3;
    int         idx  = iter->dummy4;

    if (!ht || !node) {
        return;
    }

    /* Unlink node from chain — no destroy notifiers */
    if (prev) {
        prev->next = node->next;
    } else {
        ht->buckets[idx] = node->next;
    }

    free(node);
    ht->size--;

    /* Update iterator: back up so _next() advances correctly */
    iter->dummy2 = prev;
}

GHashTable *g_hash_table_iter_get_hash_table(GHashTableIter *iter)
{
    return (GHashTable *)iter->dummy1;
}

/* ------------------------------------------------------------------ */
/* g_hash_table_find                                                  */
/* ------------------------------------------------------------------ */

gpointer g_hash_table_find(GHashTable *hash_table,
                           GHRFunc predicate,
                           gpointer user_data)
{
    if (!hash_table || !predicate) {
        return NULL;
    }

    for (guint i = 0; i < hash_table->nbuckets; i++) {
        GHashNode *node = hash_table->buckets[i];
        while (node) {
            if (predicate(node->key, node->value, user_data)) {
                return node->value;
            }
            node = node->next;
        }
    }
    return NULL;
}
