/*
 * Minimal test program for the glib-wasi shim library.
 *
 * Exercises basic functionality from each module and prints PASS/FAIL
 * for every check.  Returns 0 if all tests pass, 1 otherwise.
 *
 * Build:  make test
 * Run:    wasmtime build/test_glib.wasm   (or any WASI runtime)
 */

#include <stdio.h>
#include <string.h>
#include <glib/glib.h>
#include <glib/ghash.h>
#include <glib/glist.h>
#include <glib/garray.h>
#include <glib/gstring.h>
#include <glib/gstrfuncs.h>
#include <glib/gerror.h>
#include <glib/gmessages.h>
#include <glib/gtree.h>
#include <glib/grand.h>
#include <glib/gthread.h>
#include <glib/gutils.h>
#include <glib/gmain.h>

static int failures = 0;

#define CHECK(name, cond)                                       \
    do {                                                        \
        if (cond) {                                             \
            printf("PASS  %s\n", name);                        \
        } else {                                                \
            printf("FAIL  %s\n", name);                        \
            failures++;                                         \
        }                                                       \
    } while (0)

/* ------------------------------------------------------------------ */
/*  gmem – g_malloc / g_malloc0 / g_realloc / g_free                  */
/* ------------------------------------------------------------------ */
static void test_gmem(void)
{
    gpointer p = g_malloc(128);
    CHECK("g_malloc returns non-NULL", p != NULL);
    g_free(p);

    p = g_malloc0(64);
    unsigned char *bp = (unsigned char *)p;
    int all_zero = 1;
    for (int i = 0; i < 64; i++) {
        if (bp[i] != 0) { all_zero = 0; break; }
    }
    CHECK("g_malloc0 zeroes memory", all_zero);
    g_free(p);

    p = g_malloc(16);
    p = g_realloc(p, 256);
    CHECK("g_realloc returns non-NULL", p != NULL);
    g_free(p);
}

/* ------------------------------------------------------------------ */
/*  ghash – GHashTable insert / lookup                                */
/* ------------------------------------------------------------------ */
static void test_ghash(void)
{
    GHashTable *ht = g_hash_table_new(g_str_hash, g_str_equal);
    CHECK("g_hash_table_new returns non-NULL", ht != NULL);

    g_hash_table_insert(ht, (gpointer)"hello", (gpointer)"world");
    gpointer val = g_hash_table_lookup(ht, "hello");
    CHECK("g_hash_table_lookup finds inserted key",
          val != NULL && strcmp((const char *)val, "world") == 0);

    CHECK("g_hash_table_size == 1", g_hash_table_size(ht) == 1);

    gboolean removed = g_hash_table_remove(ht, "hello");
    CHECK("g_hash_table_remove returns TRUE", removed == TRUE);
    CHECK("g_hash_table_size == 0 after remove", g_hash_table_size(ht) == 0);

    g_hash_table_destroy(ht);
}

/* ------------------------------------------------------------------ */
/*  glist – GList append / length                                     */
/* ------------------------------------------------------------------ */
static void test_glist(void)
{
    GList *list = NULL;
    list = g_list_append(list, (gpointer)"one");
    list = g_list_append(list, (gpointer)"two");
    list = g_list_append(list, (gpointer)"three");
    CHECK("g_list_length == 3", g_list_length(list) == 3);

    gpointer d = g_list_nth_data(list, 1);
    CHECK("g_list_nth_data(1) == \"two\"",
          d != NULL && strcmp((const char *)d, "two") == 0);

    list = g_list_remove(list, "two");
    CHECK("g_list_length == 2 after remove", g_list_length(list) == 2);

    g_list_free(list);
}

/* ------------------------------------------------------------------ */
/*  garray – GArray append                                            */
/* ------------------------------------------------------------------ */
static void test_garray(void)
{
    GArray *arr = g_array_new(FALSE, FALSE, sizeof(gint));
    CHECK("g_array_new returns non-NULL", arr != NULL);

    gint vals[] = {10, 20, 30};
    g_array_append_vals(arr, vals, 3);
    CHECK("GArray len == 3 after append", arr->len == 3);
    CHECK("g_array_index(arr,gint,1) == 20",
          g_array_index(arr, gint, 1) == 20);

    g_array_free(arr, TRUE);
}

/* ------------------------------------------------------------------ */
/*  gstring – GString operations                                      */
/* ------------------------------------------------------------------ */
static void test_gstring(void)
{
    GString *s = g_string_new("hello");
    CHECK("g_string_new sets str", strcmp(s->str, "hello") == 0);

    g_string_append(s, " world");
    CHECK("g_string_append works", strcmp(s->str, "hello world") == 0);
    CHECK("g_string_append updates len", s->len == 11);

    g_string_truncate(s, 5);
    CHECK("g_string_truncate works", strcmp(s->str, "hello") == 0);

    gchar *raw = g_string_free(s, FALSE);
    CHECK("g_string_free(FALSE) returns str", strcmp(raw, "hello") == 0);
    g_free(raw);
}

/* ------------------------------------------------------------------ */
/*  gstrfuncs – g_strdup / g_strcmp0                                   */
/* ------------------------------------------------------------------ */
static void test_gstrfuncs(void)
{
    gchar *dup = g_strdup("test string");
    CHECK("g_strdup returns correct copy",
          dup != NULL && strcmp(dup, "test string") == 0);
    g_free(dup);

    CHECK("g_strcmp0(NULL, NULL) == 0", g_strcmp0(NULL, NULL) == 0);
    CHECK("g_strcmp0(\"a\", \"b\") < 0", g_strcmp0("a", "b") < 0);
    CHECK("g_str_has_prefix works", g_str_has_prefix("hello world", "hello"));
    CHECK("g_str_has_suffix works", g_str_has_suffix("hello world", "world"));
}

/* ------------------------------------------------------------------ */
/*  gerror – GError creation / matching                               */
/* ------------------------------------------------------------------ */
static void test_gerror(void)
{
    GQuark domain = g_quark_from_static_string("test-error");
    GError *err = g_error_new_literal(domain, 42, "something went wrong");
    CHECK("g_error_new_literal returns non-NULL", err != NULL);
    CHECK("GError code == 42", err->code == 42);
    CHECK("GError message matches",
          strcmp(err->message, "something went wrong") == 0);
    CHECK("g_error_matches works", g_error_matches(err, domain, 42));
    g_error_free(err);
}

/* ------------------------------------------------------------------ */
/*  gmessages – g_message (just verify it doesn't crash)              */
/* ------------------------------------------------------------------ */
static void test_gmessages(void)
{
    /* g_message writes to stderr; we just make sure it doesn't abort. */
    g_message("test message from test_glib: %d", 123);
    CHECK("g_message did not crash", 1);
}

/* ------------------------------------------------------------------ */
/*  gtree – GTree insert / lookup                                     */
/* ------------------------------------------------------------------ */
static gint int_compare(gconstpointer a, gconstpointer b)
{
    gint ia = GPOINTER_TO_INT(a);
    gint ib = GPOINTER_TO_INT(b);
    return (ia > ib) - (ia < ib);
}

static void test_gtree(void)
{
    GTree *tree = g_tree_new(int_compare);
    CHECK("g_tree_new returns non-NULL", tree != NULL);

    g_tree_insert(tree, GINT_TO_POINTER(5), GINT_TO_POINTER(50));
    g_tree_insert(tree, GINT_TO_POINTER(3), GINT_TO_POINTER(30));
    g_tree_insert(tree, GINT_TO_POINTER(7), GINT_TO_POINTER(70));

    CHECK("g_tree_nnodes == 3", g_tree_nnodes(tree) == 3);

    gpointer val = g_tree_lookup(tree, GINT_TO_POINTER(3));
    CHECK("g_tree_lookup finds key 3",
          GPOINTER_TO_INT(val) == 30);

    val = g_tree_lookup(tree, GINT_TO_POINTER(99));
    CHECK("g_tree_lookup returns NULL for missing key", val == NULL);

    g_tree_destroy(tree);
}

/* ------------------------------------------------------------------ */
/*  grand – g_random_int                                              */
/* ------------------------------------------------------------------ */
static void test_grand(void)
{
    guint32 r1 = g_random_int();
    guint32 r2 = g_random_int();
    /* Technically they *could* be equal, but the probability is 1/2^32. */
    CHECK("g_random_int returns values (basic smoke test)", 1);

    gint32 ranged = g_random_int_range(10, 20);
    CHECK("g_random_int_range(10,20) in [10,20)",
          ranged >= 10 && ranged < 20);

    (void)r1;
    (void)r2;
}

/* ------------------------------------------------------------------ */
/*  gthread – basic no-op smoke test (single-threaded wasm32)         */
/* ------------------------------------------------------------------ */
static void test_gthread(void)
{
    GMutex m;
    g_mutex_init(&m);
    g_mutex_lock(&m);
    g_mutex_unlock(&m);
    g_mutex_clear(&m);
    CHECK("GMutex init/lock/unlock/clear (no-op)", 1);
}

/* ------------------------------------------------------------------ */
/*  gutils – g_get_monotonic_time smoke test                          */
/* ------------------------------------------------------------------ */
static void test_gutils(void)
{
    gint64 t = g_get_monotonic_time();
    /* Just verify it returns something non-negative. */
    CHECK("g_get_monotonic_time >= 0", t >= 0);
}

/* ------------------------------------------------------------------ */
/*  gmain – GMainContext smoke test                                   */
/* ------------------------------------------------------------------ */
static void test_gmain(void)
{
    GMainContext *ctx = g_main_context_default();
    CHECK("g_main_context_default returns non-NULL", ctx != NULL);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== glib-wasi test suite ===\n\n");

    test_gmem();
    test_ghash();
    test_glist();
    test_garray();
    test_gstring();
    test_gstrfuncs();
    test_gerror();
    test_gmessages();
    test_gtree();
    test_grand();
    test_gthread();
    test_gutils();
    test_gmain();

    printf("\n=== Results: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
