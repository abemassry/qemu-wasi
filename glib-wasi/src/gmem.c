/*
 * Minimal GLib memory allocation shim for wasm32-wasi.
 *
 * g_malloc aborts on failure (matching real GLib behaviour).
 * g_slice_* forwards to g_malloc / g_free — no slab allocator.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../glib/glib.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * Overflow-safe multiplication.  Returns 0 on overflow so the caller
 * can detect it (glib treats 0-byte allocations as valid).
 */
static inline int
g_mem_overflow(gsize a, gsize b, gsize *out)
{
    /* If either operand is zero the product is zero — no overflow. */
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    *out = a * b;
    if (*out / a != b) {
        return 1; /* overflow */
    }
    return 0;
}

static void G_GNUC_UNUSED
g_mem_abort(const char *msg)
{
    fprintf(stderr, "GLib-WASI: %s\n", msg);
    abort();
}

/* ------------------------------------------------------------------ */
/*  Core allocators                                                   */
/* ------------------------------------------------------------------ */

gpointer
g_malloc(gsize n_bytes)
{
    if (G_LIKELY(n_bytes > 0)) {
        gpointer p = malloc(n_bytes);
        if (G_UNLIKELY(p == NULL)) {
            g_mem_abort("g_malloc: out of memory");
        }
        return p;
    }
    return NULL;
}

gpointer
g_malloc0(gsize n_bytes)
{
    if (G_LIKELY(n_bytes > 0)) {
        gpointer p = calloc(1, n_bytes);
        if (G_UNLIKELY(p == NULL)) {
            g_mem_abort("g_malloc0: out of memory");
        }
        return p;
    }
    return NULL;
}

gpointer
g_realloc(gpointer mem, gsize n_bytes)
{
    if (G_LIKELY(n_bytes > 0)) {
        gpointer p = realloc(mem, n_bytes);
        if (G_UNLIKELY(p == NULL)) {
            g_mem_abort("g_realloc: out of memory");
        }
        return p;
    }
    /* n_bytes == 0 → free and return NULL (glib semantics) */
    free(mem);
    return NULL;
}

void
g_free(gpointer mem)
{
    free(mem);
}

/* ------------------------------------------------------------------ */
/*  "try" variants — return NULL instead of aborting                  */
/* ------------------------------------------------------------------ */

gpointer
g_try_malloc(gsize n_bytes)
{
    if (G_LIKELY(n_bytes > 0)) {
        return malloc(n_bytes);
    }
    return NULL;
}

gpointer
g_try_malloc0(gsize n_bytes)
{
    if (G_LIKELY(n_bytes > 0)) {
        return calloc(1, n_bytes);
    }
    return NULL;
}

gpointer
g_try_realloc(gpointer mem, gsize n_bytes)
{
    if (G_LIKELY(n_bytes > 0)) {
        return realloc(mem, n_bytes);
    }
    free(mem);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  memdup                                                            */
/* ------------------------------------------------------------------ */

gpointer
g_memdup(gconstpointer mem, guint byte_size)
{
    if (mem && byte_size > 0) {
        gpointer p = g_malloc(byte_size);
        memcpy(p, mem, byte_size);
        return p;
    }
    return NULL;
}

gpointer
g_memdup2(gconstpointer mem, gsize byte_size)
{
    if (mem && byte_size > 0) {
        gpointer p = g_malloc(byte_size);
        memcpy(p, mem, byte_size);
        return p;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Overflow-checked array allocators                                 */
/* ------------------------------------------------------------------ */

gpointer
g_malloc_n(gsize n_blocks, gsize n_block_bytes)
{
    gsize total;
    if (G_UNLIKELY(g_mem_overflow(n_blocks, n_block_bytes, &total))) {
        g_mem_abort("g_malloc_n: overflow in allocation size");
    }
    return g_malloc(total);
}

gpointer
g_malloc0_n(gsize n_blocks, gsize n_block_bytes)
{
    gsize total;
    if (G_UNLIKELY(g_mem_overflow(n_blocks, n_block_bytes, &total))) {
        g_mem_abort("g_malloc0_n: overflow in allocation size");
    }
    return g_malloc0(total);
}

gpointer
g_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes)
{
    gsize total;
    if (G_UNLIKELY(g_mem_overflow(n_blocks, n_block_bytes, &total))) {
        g_mem_abort("g_realloc_n: overflow in allocation size");
    }
    return g_realloc(mem, total);
}

/* ------------------------------------------------------------------ */
/*  GSlice — thin wrappers (no slab allocator on wasi)                */
/* ------------------------------------------------------------------ */

gpointer
g_slice_alloc(gsize block_size)
{
    return g_malloc(block_size);
}

gpointer
g_slice_alloc0(gsize block_size)
{
    return g_malloc0(block_size);
}

void
g_slice_free1(gsize block_size, gpointer mem_block)
{
    (void)block_size;
    g_free(mem_block);
}

/* ------------------------------------------------------------------ */
/*  "try" overflow-checked array allocators                           */
/* ------------------------------------------------------------------ */

gpointer
g_try_malloc_n(gsize n_blocks, gsize n_block_bytes)
{
    gsize total;
    if (G_UNLIKELY(g_mem_overflow(n_blocks, n_block_bytes, &total))) {
        return NULL;
    }
    return g_try_malloc(total);
}

gpointer
g_try_malloc0_n(gsize n_blocks, gsize n_block_bytes)
{
    gsize total;
    if (G_UNLIKELY(g_mem_overflow(n_blocks, n_block_bytes, &total))) {
        return NULL;
    }
    return g_try_malloc0(total);
}

gpointer
g_try_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes)
{
    gsize total;
    if (G_UNLIKELY(g_mem_overflow(n_blocks, n_block_bytes, &total))) {
        return NULL;
    }
    return g_try_realloc(mem, total);
}

/* ------------------------------------------------------------------ */
/*  g_slice_copy — used by some QEMU code paths                       */
/* ------------------------------------------------------------------ */

gpointer
g_slice_copy(gsize block_size, gconstpointer mem_block)
{
    gpointer copy = g_slice_alloc(block_size);
    if (copy && mem_block) {
        memcpy(copy, mem_block, block_size);
    }
    return copy;
}
