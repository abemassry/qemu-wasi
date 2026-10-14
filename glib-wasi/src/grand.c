/*
 * GLib random number generation – wasm32-wasi implementation.
 *
 * Uses xorshift128+ as the PRNG algorithm.  The global instance is
 * lazily initialised with a time-based seed.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../glib/grand.h"

/* ------------------------------------------------------------------ */
/*  GRand structure (xorshift128+ state)                              */
/* ------------------------------------------------------------------ */

struct _GRand {
    guint64 s[2];
};

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * splitmix64 – used to expand a single 32-bit seed into a full
 * 128-bit xorshift128+ state.
 */
static guint64
splitmix64(guint64 *state)
{
    guint64 z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static void
grand_seed_from_u32(GRand *rand_, guint32 seed)
{
    guint64 sm = (guint64)seed;
    rand_->s[0] = splitmix64(&sm);
    rand_->s[1] = splitmix64(&sm);

    /* Ensure we never have an all-zero state. */
    if (rand_->s[0] == 0 && rand_->s[1] == 0) {
        rand_->s[0] = UINT64_C(0x12345678deadbeef);
    }
}

/*
 * xorshift128+ core.  Returns a 64-bit pseudo-random number.
 */
static guint64
xorshift128plus(GRand *rand_)
{
    guint64 s1 = rand_->s[0];
    guint64 s0 = rand_->s[1];
    guint64 result = s0 + s1;

    rand_->s[0] = s0;
    s1 ^= s1 << 23;
    rand_->s[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);

    return result;
}

/* ------------------------------------------------------------------ */
/*  GRand lifecycle                                                   */
/* ------------------------------------------------------------------ */

GRand *
g_rand_new_with_seed(guint32 seed)
{
    GRand *rand_ = (GRand *)g_malloc(sizeof(GRand));
    grand_seed_from_u32(rand_, seed);
    return rand_;
}

GRand *
g_rand_new_with_seed_array(const guint32 *seed, guint seed_length)
{
    GRand *rand_ = (GRand *)g_malloc(sizeof(GRand));

    if (!seed || seed_length == 0) {
        grand_seed_from_u32(rand_, 0);
        return rand_;
    }

    /*
     * Mix all seed words together using splitmix64 starting from
     * the first word, then XOR-folding subsequent words in.
     */
    guint64 sm = (guint64)seed[0];
    rand_->s[0] = splitmix64(&sm);
    rand_->s[1] = splitmix64(&sm);

    for (guint i = 1; i < seed_length; i++) {
        guint64 extra = (guint64)seed[i];
        rand_->s[0] ^= splitmix64(&extra);
        rand_->s[1] ^= splitmix64(&extra);
    }

    if (rand_->s[0] == 0 && rand_->s[1] == 0) {
        rand_->s[0] = UINT64_C(0x12345678deadbeef);
    }

    return rand_;
}

GRand *
g_rand_new(void)
{
    /*
     * Seed from current time.  On WASI clock_gettime is usually
     * available; fall back to time() otherwise.
     */
    guint32 seed;
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        seed = (guint32)((guint64)ts.tv_sec ^ (guint64)ts.tv_nsec);
    } else {
        seed = (guint32)time(NULL);
    }
    return g_rand_new_with_seed(seed);
}

void
g_rand_free(GRand *rand_)
{
    g_free(rand_);
}

void
g_rand_set_seed(GRand *rand_, guint32 seed)
{
    if (rand_) {
        grand_seed_from_u32(rand_, seed);
    }
}

/* ------------------------------------------------------------------ */
/*  GRand number generators                                           */
/* ------------------------------------------------------------------ */

guint32
g_rand_int(GRand *rand_)
{
    return (guint32)(xorshift128plus(rand_) >> 32);
}

gint32
g_rand_int_range(GRand *rand_, gint32 begin, gint32 end)
{
    if (begin >= end) {
        return begin;
    }

    guint32 range = (guint32)(end - begin);
    guint32 r = g_rand_int(rand_);

    /* Simple modulo reduction. */
    return begin + (gint32)(r % range);
}

gdouble
g_rand_double(GRand *rand_)
{
    /*
     * Use the upper 53 bits of a 64-bit value to produce a double
     * uniformly distributed in [0, 1).
     */
    guint64 val = xorshift128plus(rand_);
    return (gdouble)(val >> 11) * (1.0 / 9007199254740992.0);   /* 2^53 */
}

gdouble
g_rand_double_range(GRand *rand_, gdouble begin, gdouble end)
{
    return begin + g_rand_double(rand_) * (end - begin);
}

/* ------------------------------------------------------------------ */
/*  Global convenience functions                                      */
/* ------------------------------------------------------------------ */

static GRand *global_rand = NULL;

static GRand *
get_global_rand(void)
{
    if (!global_rand) {
        global_rand = g_rand_new();
    }
    return global_rand;
}

guint32
g_random_int(void)
{
    return g_rand_int(get_global_rand());
}

gint32
g_random_int_range(gint32 begin, gint32 end)
{
    return g_rand_int_range(get_global_rand(), begin, end);
}

gdouble
g_random_double(void)
{
    return g_rand_double(get_global_rand());
}

gdouble
g_random_double_range(gdouble begin, gdouble end)
{
    return g_rand_double_range(get_global_rand(), begin, end);
}

void
g_random_set_seed(guint32 seed)
{
    if (!global_rand) {
        global_rand = g_rand_new_with_seed(seed);
    } else {
        g_rand_set_seed(global_rand, seed);
    }
}
