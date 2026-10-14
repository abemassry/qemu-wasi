/*
 * x86-64 instruction differential test for the QEMU wasm (WASI) port.
 *
 * Runs a battery of integer, bit-manipulation, string and SSE instructions
 * over many operand values and prints one hash per instruction form.  Run
 * it under native QEMU and under the wasm build (see run.sh): both are the
 * same QEMU source, so every line must match.  A mismatch points at a bug
 * in the wasm TCG backend, the TB interpreter, or a helper miscompiled for
 * wasm.
 *
 * Boots with QEMU's tests/tcg/x86_64/system/boot.S (PVH entry, long mode)
 * and prints through tests/tcg/minilib.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include "minilib.h"

typedef uint64_t u64;
typedef int64_t s64;

#define ITERS 1024
#define FLAGS 0x8d5ull          /* OF SF ZF AF PF CF */

static u64 rng = 0x9e3779b97f4a7c15ull;

static u64 next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static const u64 special[] = {
    0, 1, 2, 3, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000,
    0x7fffffff, 0x80000000, 0xffffffff, 0x100000000ull, 0x17fffffffull,
    0x7fffffffffffffffull, 0x8000000000000000ull, 0xffffffffffffffffull,
    0xfffffffffffffffeull, 0xffffffff00000000ull, 0x8000000080000000ull,
    0x0123456789abcdefull, 0xfedcba9876543210ull, 0x00000000deadbeefull,
};
#define NSPECIAL (sizeof(special) / sizeof(special[0]))

/* Operand pair for iteration it: all special pairs first, then random. */
static void operands(unsigned it, u64 *a, u64 *b, u64 *c, u64 *d)
{
    if (it < NSPECIAL * NSPECIAL) {
        *a = special[it % NSPECIAL];
        *b = special[it / NSPECIAL];
    } else {
        *a = next_rand();
        *b = next_rand();
        /* Small values matter for shifts, bit offsets and divisors. */
        if (it & 1) {
            *b &= 0xff;
        }
    }
    *c = next_rand();
    *d = next_rand();
}

/* Incoming flags: none, CF only, or every arithmetic flag. */
static u64 flags_in(unsigned it)
{
    static const u64 f[3] = { 0x2, 0x3, 0x2 | FLAGS };
    return f[it % 3];
}

#define MIX(v) (h = (h ^ (u64)(v)) * 0x100000001b3ull)

/*
 * TEST(name) { body using a, b, c, d, fi; MIX(results) } END
 * runs the body ITERS times and prints "name hash".
 */
#define TEST(name)                                                      \
    {                                                                   \
        const char *test_name = name;                                   \
        u64 h = 0xcbf29ce484222325ull;                                  \
        rng = 0x9e3779b97f4a7c15ull;                                    \
        for (unsigned it = 0; it < ITERS; it++) {                       \
            u64 a, b, c, d, fi = flags_in(it);                          \
            operands(it, &a, &b, &c, &d);                               \
            (void)a; (void)b; (void)c; (void)d; (void)fi;
#define END                                                             \
        }                                                               \
        ml_printf("%s %llx\n", test_name, (unsigned long long)h);       \
    }

#define PRE  "push %[fi]\n\tpopfq\n\t"
#define POST "\n\tpushfq\n\tpop %[f]"

/* r = r OP b */
#define ALU2(name, insn, m)                                             \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " %" m "[b], %" m "[r]" POST              \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : [b] "r" (b), [fi] "r" (fi) : "cc");              \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* r = r OP imm */
#define ALUI(name, insn, imm, m)                                        \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " $" #imm ", %" m "[r]" POST              \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : [fi] "r" (fi) : "cc");                           \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* r = OP r */
#define ALU1(name, insn, m)                                             \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " %" m "[r]" POST                         \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : [fi] "r" (fi) : "cc");                           \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* r = r OP cl */
#define SHCL(name, insn, m)                                             \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " %%cl, %" m "[r]" POST                   \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : "c" (b), [fi] "r" (fi) : "cc");                  \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* shld/shrd r, b by cl */
#define SHDCL(name, insn, m)                                            \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " %%cl, %" m "[b], %" m "[r]" POST        \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : [b] "r" (c), "c" (b), [fi] "r" (fi) : "cc");     \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* r = b OP-src (dest-only forms: movzx, bsf, popcnt, ...) */
#define MOV2(name, insn, ms, md)                                        \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " %" ms "[b], %" md "[r]" POST            \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : [b] "r" (b), [fi] "r" (fi) : "cc");              \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* BMI three-operand: r = OP(b, c) */
#define VEX3(name, insn, m)                                             \
    TEST(name) {                                                        \
        u64 r = a, f;                                                   \
        asm volatile(PRE insn " %" m "[c], %" m "[b], %" m "[r]" POST   \
                     : [r] "+r" (r), [f] "=&r" (f)                      \
                     : [b] "r" (b), [c] "r" (c), [fi] "r" (fi) : "cc"); \
        MIX(r); MIX(f & FLAGS);                                         \
    } END

/* rdx:rax = rax * b  (also rdx:rax / b with suitable inputs) */
#define MULDIV(name, insn, m, ax, dx)                                   \
    TEST(name) {                                                        \
        u64 lo = (ax), hi = (dx), f;                                    \
        asm volatile(PRE insn " %" m "[b]" POST                         \
                     : "+a" (lo), "+d" (hi), [f] "=&r" (f)              \
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");             \
        MIX(lo); MIX(hi); MIX(f & FLAGS);                               \
    } END

#define CONDS(X) X(o) X(no) X(b) X(ae) X(e) X(ne) X(be) X(a) \
                 X(s) X(ns) X(p) X(np) X(l) X(ge) X(le) X(g)

/* SSE: x = x OP y on two 128-bit values built from a,b and c,d */
#define SSE2(name, insn)                                                \
    TEST(name) {                                                        \
        u64 x[2] = { a, c }, y[2] = { b, d }, o[2];                     \
        asm volatile("movdqu %[x], %%xmm0\n\tmovdqu %[y], %%xmm1\n\t"   \
                     insn " %%xmm1, %%xmm0\n\tmovdqu %%xmm0, %[o]"      \
                     : [o] "=m" (o) : [x] "m" (x), [y] "m" (y)          \
                     : "xmm0", "xmm1");                                 \
        MIX(o[0]); MIX(o[1]);                                           \
    } END

/* SSE with immediate: x = OP(x, imm) or OP(y -> x, imm) */
#define SSEI(name, insn, imm)                                           \
    TEST(name) {                                                        \
        u64 x[2] = { a, c }, y[2] = { b, d }, o[2];                     \
        asm volatile("movdqu %[x], %%xmm0\n\tmovdqu %[y], %%xmm1\n\t"   \
                     insn " $" #imm ", %%xmm1, %%xmm0\n\t"              \
                     "movdqu %%xmm0, %[o]"                              \
                     : [o] "=m" (o) : [x] "m" (x), [y] "m" (y)          \
                     : "xmm0", "xmm1");                                 \
        MIX(o[0]); MIX(o[1]);                                           \
    } END

#define SSESH(name, insn, imm)                                          \
    TEST(name) {                                                        \
        u64 x[2] = { a, c }, o[2];                                      \
        asm volatile("movdqu %[x], %%xmm0\n\t"                          \
                     insn " $" #imm ", %%xmm0\n\tmovdqu %%xmm0, %[o]"   \
                     : [o] "=m" (o) : [x] "m" (x) : "xmm0");            \
        MIX(o[0]); MIX(o[1]);                                           \
    } END

static void cpuid(unsigned leaf, unsigned sub, unsigned r[4])
{
    asm volatile("cpuid" : "=a" (r[0]), "=b" (r[1]), "=c" (r[2]), "=d" (r[3])
                 : "a" (leaf), "c" (sub));
}

static void enable_sse(void)
{
    u64 cr;

    asm volatile("mov %%cr0, %0" : "=r" (cr));
    cr = (cr & ~4ull) | 2;              /* clear EM, set MP */
    asm volatile("mov %0, %%cr0" : : "r" (cr));
    asm volatile("mov %%cr4, %0" : "=r" (cr));
    cr |= 0x600;                        /* OSFXSR, OSXMMEXCPT */
    asm volatile("mov %0, %%cr4" : : "r" (cr));
}

static void test_alu(void)
{
#define SIZES(N, I) ALU2(N "q", I "q", "q") ALU2(N "l", I "l", "k") \
                    ALU2(N "w", I "w", "w") ALU2(N "b", I "b", "b")
    SIZES("add", "add") SIZES("adc", "adc") SIZES("sub", "sub")
    SIZES("sbb", "sbb") SIZES("and", "and") SIZES("or", "or")
    SIZES("xor", "xor") SIZES("cmp", "cmp") SIZES("test", "test")
#undef SIZES
    ALUI("addq$-1", "addq", -1, "q") ALUI("addl$0x7fffffff", "addl", 0x7fffffff, "k")
    ALUI("andq$0x80000000", "andq", -0x80000000, "q") ALUI("orl$0xff", "orl", 0xff, "k")
    ALUI("xorq$-0x10", "xorq", -0x10, "q") ALUI("cmpq$1", "cmpq", 1, "q")
    ALUI("subw$0x8000", "subw", 0x8000, "w") ALUI("adcb$0x80", "adcb", 0x80, "b")
#define SIZES(N, I) ALU1(N "q", I "q", "q") ALU1(N "l", I "l", "k") \
                    ALU1(N "w", I "w", "w") ALU1(N "b", I "b", "b")
    SIZES("neg", "neg") SIZES("not", "not") SIZES("inc", "inc") SIZES("dec", "dec")
#undef SIZES
}

static void test_shifts(void)
{
#define SIZES(N, I) SHCL(N "q", I "q", "q") SHCL(N "l", I "l", "k") \
                    SHCL(N "w", I "w", "w") SHCL(N "b", I "b", "b")
    SIZES("shl", "shl") SIZES("shr", "shr") SIZES("sar", "sar")
    SIZES("rol", "rol") SIZES("ror", "ror") SIZES("rcl", "rcl")
    SIZES("rcr", "rcr")
#undef SIZES
#define IMMS(N, I, M) ALUI(N "$1", I, 1, M) ALUI(N "$7", I, 7, M) \
                      ALUI(N "$15", I, 15, M) ALUI(N "$31", I, 31, M)
    IMMS("shlq", "shlq", "q") IMMS("shrq", "shrq", "q") IMMS("sarq", "sarq", "q")
    IMMS("rolq", "rolq", "q") IMMS("rorq", "rorq", "q") IMMS("shll", "shll", "k")
    IMMS("shrl", "shrl", "k") IMMS("sarl", "sarl", "k") IMMS("roll", "roll", "k")
    IMMS("rorl", "rorl", "k") IMMS("rcll", "rcll", "k") IMMS("rcrq", "rcrq", "q")
    ALUI("shlq$63", "shlq", 63, "q") ALUI("sarq$63", "sarq", 63, "q")
    ALUI("shrq$32", "shrq", 32, "q") ALUI("rorq$32", "rorq", 32, "q")
    ALUI("rolw$8", "rolw", 8, "w") ALUI("shrw$3", "shrw", 3, "w")
    ALUI("sarb$2", "sarb", 2, "b")
#undef IMMS
    SHDCL("shldq", "shldq", "q") SHDCL("shrdq", "shrdq", "q")
    SHDCL("shldl", "shldl", "k") SHDCL("shrdl", "shrdl", "k")
    SHDCL("shldw", "shldw", "w") SHDCL("shrdw", "shrdw", "w")
}

static void test_muldiv(void)
{
    /* MULDIV uses dv as the divisor/multiplier. */
#define DV_ANY u64 dv = b; (void)dv;
    TEST("mulq") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "mulq %[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    TEST("imulq1") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "imulq %[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    TEST("mull") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "mull %k[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    TEST("imull1") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "imull %k[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    TEST("mulw") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "mulw %w[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    TEST("mulb") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "mulb %b[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    TEST("imulb1") { u64 lo = a, hi = c, f, dv = b;
        asm volatile(PRE "imulb %b[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); MIX(f & 0x801); } END
    ALU2("imulq2", "imulq", "q") ALU2("imull2", "imull", "k") ALU2("imulw2", "imulw", "w")
    TEST("imulq3") { u64 r, f;
        asm volatile(PRE "imulq $-12345, %[b], %[r]" POST : [r] "=&r" (r), [f] "=&r" (f)
                     : [b] "r" (b), [fi] "r" (fi) : "cc");
        MIX(r); MIX(f & 0x801); } END
    TEST("imull3") { u64 r = a, f;
        asm volatile(PRE "imull $0x7fff1234, %k[b], %k[r]" POST : [r] "+r" (r), [f] "=&r" (f)
                     : [b] "r" (b), [fi] "r" (fi) : "cc");
        MIX(r); MIX(f & 0x801); } END
    /* Unsigned division: quotient fits because the high part < divisor. */
    TEST("divq") { u64 dv = b | 1, lo = a, hi = c % dv, f;
        asm volatile(PRE "divq %[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); } END
    TEST("divl") { u64 dv = (b & 0xffffffff) | 1, lo = a, hi = (c & 0xffffffff) % dv, f;
        asm volatile(PRE "divl %k[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); } END
    TEST("divw") { u64 dv = (b & 0xffff) | 1, lo = a, hi = (c & 0xffff) % dv, f;
        asm volatile(PRE "divw %w[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); } END
    TEST("divb") { u64 dv = (b & 0xff) | 1, lo = (a & ~0xff00ull) | (((c & 0xff) % dv) << 8), hi = 0, f;
        asm volatile(PRE "divb %b[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); } END
    /* Signed division of a sign-extended dividend: never overflows. */
    TEST("idivq") { s64 dv = (s64)b; if (dv == 0 || dv == -1) dv = 7;
        u64 lo = a, hi = (u64)((s64)a >> 63), f;
        asm volatile(PRE "idivq %[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" (dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); } END
    TEST("idivl") { int32_t dv = (int32_t)b; if (dv == 0 || dv == -1) dv = 7;
        u64 lo = (uint32_t)a, hi = (uint32_t)((int32_t)a >> 31), f;
        asm volatile(PRE "idivl %k[b]" POST : "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : [b] "r" ((u64)(uint32_t)dv), [fi] "r" (fi) : "cc");
        MIX(lo); MIX(hi); } END
}

static void test_bits(unsigned c1_ecx, unsigned c7_ebx, unsigned c81_ecx)
{
    MOV2("bsfq", "bsfq", "q", "q") MOV2("bsrq", "bsrq", "q", "q")
    MOV2("bsfl", "bsfl", "k", "k") MOV2("bsrl", "bsrl", "k", "k")
    MOV2("bsfw", "bsfw", "w", "w") MOV2("bsrw", "bsrw", "w", "w")
    ALU2("btq", "btq", "q") ALU2("btsq", "btsq", "q") ALU2("btrq", "btrq", "q")
    ALU2("btcq", "btcq", "q") ALU2("btl", "btl", "k") ALU2("btsl", "btsl", "k")
    ALUI("btq$37", "btq", 37, "q") ALUI("btsq$63", "btsq", 63, "q")
    ALUI("btrl$5", "btrl", 5, "k") ALUI("btcw$15", "btcw", 15, "w")
    ALU1("bswapq", "bswapq", "q") ALU1("bswapl", "bswapl", "k")
    MOV2("movzbq", "movzbq", "b", "q") MOV2("movzwq", "movzwq", "w", "q")
    MOV2("movsbq", "movsbq", "b", "q") MOV2("movswq", "movswq", "w", "q")
    MOV2("movslq", "movslq", "k", "q") MOV2("movzbl", "movzbl", "b", "k")
    MOV2("movsbw", "movsbw", "b", "w") MOV2("movl", "movl", "k", "k")
    MOV2("movw", "movw", "w", "w") MOV2("movb", "movb", "b", "b")
    TEST("cbw/cwde/cdqe/cwd/cdq/cqo") {
        u64 x = a, y = c;
        asm volatile("cbw" : "+a" (x)); MIX(x); x = a;
        asm volatile("cwde" : "+a" (x)); MIX(x); x = a;
        asm volatile("cdqe" : "+a" (x)); MIX(x); x = a;
        asm volatile("cwd" : "+a" (x), "+d" (y)); MIX(y); y = c;
        asm volatile("cdq" : "+a" (x), "+d" (y)); MIX(y); y = c;
        asm volatile("cqo" : "+a" (x), "+d" (y)); MIX(y);
    } END
    TEST("lea") {
        u64 r1, r2, r3;
        asm volatile("leaq 0x12345678(%[a],%[b],8), %[r1]\n\t"
                     "leal -0x80(%[a],%[b],2), %k[r2]\n\t"
                     "leaw 7(%[a],%[b],4), %w[r3]"
                     : [r1] "=&r" (r1), [r2] "=&r" (r2), [r3] "=&r" (r3)
                     : [a] "r" (a), [b] "r" (b) : "memory");
        MIX(r1); MIX((uint32_t)r2); MIX((uint16_t)r3);
    } END
    TEST("xadd/xchg/cmpxchg") {
        u64 r = a, s = b, f, ax = c;
        asm volatile(PRE "xaddq %[s], %[r]" POST : [r] "+r" (r), [s] "+r" (s), [f] "=&r" (f)
                     : [fi] "r" (fi) : "cc");
        MIX(r); MIX(s); MIX(f & FLAGS);
        r = a; s = b;
        asm volatile("xchgl %k[s], %k[r]" : [r] "+r" (r), [s] "+r" (s));
        MIX(r); MIX(s);
        r = a; ax = (it & 2) ? a : c;
        asm volatile(PRE "cmpxchgq %[s], %[r]" POST : [r] "+r" (r), "+a" (ax), [f] "=&r" (f)
                     : [s] "r" (b), [fi] "r" (fi) : "cc");
        MIX(r); MIX(ax); MIX(f & FLAGS);
        r = a; ax = (it & 2) ? a : c;
        asm volatile(PRE "cmpxchgl %k[s], %k[r]" POST : [r] "+r" (r), "+a" (ax), [f] "=&r" (f)
                     : [s] "r" (b), [fi] "r" (fi) : "cc");
        MIX(r); MIX(ax); MIX(f & FLAGS);
    } END
    TEST("cmpxchg16b") {
        u64 mem[2] __attribute__((aligned(16))) = { a, b };
        u64 lo = (it & 1) ? a : c, hi = (it & 2) ? b : d, f;
        asm volatile(PRE "cmpxchg16b %[m]" POST
                     : [m] "+m" (mem), "+a" (lo), "+d" (hi), [f] "=&r" (f)
                     : "b" (c), "c" (d), [fi] "r" (fi) : "cc");
        MIX(mem[0]); MIX(mem[1]); MIX(lo); MIX(hi); MIX(f & 0x40);
    } END

#define SETCC(cc) TEST("set" #cc) { u64 r = 0;                         \
        asm volatile("cmpq %[b], %[a]\n\tset" #cc " %b[r]"              \
                     : [r] "+r" (r) : [a] "r" (a), [b] "r" (b) : "cc"); \
        MIX(r); } END
    CONDS(SETCC)
#undef SETCC
#define CMOV(cc) TEST("cmov" #cc) { u64 r = c;                          \
        asm volatile("cmpl %k[b], %k[a]\n\tcmov" #cc "q %[s], %[r]"     \
                     : [r] "+r" (r) : [a] "r" (a), [b] "r" (b), [s] "r" (d) : "cc"); \
        MIX(r); } END
    CONDS(CMOV)
#undef CMOV

    if (c1_ecx & (1u << 23)) {
        MOV2("popcntq", "popcntq", "q", "q") MOV2("popcntl", "popcntl", "k", "k")
        MOV2("popcntw", "popcntw", "w", "w")
    }
    if (c81_ecx & (1u << 5)) {
        MOV2("lzcntq", "lzcntq", "q", "q") MOV2("lzcntl", "lzcntl", "k", "k")
    }
    if (c7_ebx & (1u << 3)) {
        MOV2("tzcntq", "tzcntq", "q", "q") MOV2("tzcntl", "tzcntl", "k", "k")
        VEX3("andnq", "andnq", "q") VEX3("andnl", "andnl", "k")
        VEX3("bextrq", "bextrq", "q") VEX3("bextrl", "bextrl", "k")
        MOV2("blsiq", "blsiq", "q", "q") MOV2("blsmskq", "blsmskq", "q", "q")
        MOV2("blsrq", "blsrq", "q", "q") MOV2("blsrl", "blsrl", "k", "k")
    }
    if (c7_ebx & (1u << 8)) {
        VEX3("bzhiq", "bzhiq", "q") VEX3("bzhil", "bzhil", "k")
        VEX3("pdepq", "pdepq", "q") VEX3("pextq", "pextq", "q")
        VEX3("pdepl", "pdepl", "k") VEX3("pextl", "pextl", "k")
        VEX3("sarxq", "sarxq", "q") VEX3("shlxq", "shlxq", "q")
        VEX3("shrxq", "shrxq", "q") VEX3("shrxl", "shrxl", "k")
        TEST("rorxq$13") { u64 r;
            asm volatile("rorxq $13, %[b], %[r]" : [r] "=r" (r) : [b] "r" (b));
            MIX(r); } END
        TEST("rorxl$7") { u64 r = a;
            asm volatile("rorxl $7, %k[b], %k[r]" : [r] "+r" (r) : [b] "r" (b));
            MIX(r); } END
        TEST("mulxq") { u64 lo, hi;
            asm volatile("mulxq %[b], %[lo], %[hi]" : [lo] "=&r" (lo), [hi] "=&r" (hi)
                         : "d" (a), [b] "r" (b));
            MIX(lo); MIX(hi); } END
    }
    if (c7_ebx & (1u << 19)) {
        ALU2("adcxq", "adcxq", "q") ALU2("adoxq", "adoxq", "q")
        ALU2("adcxl", "adcxl", "k")
    }
    if (c1_ecx & (1u << 20)) {
        MOV2("crc32q", "crc32q", "q", "q") MOV2("crc32l", "crc32l", "k", "k")
        MOV2("crc32b", "crc32b", "b", "k")
    }
    if (c1_ecx & (1u << 22)) {
        TEST("movbe") { u64 m = b, r1, r2 = a; uint32_t m32 = (uint32_t)c;
            asm volatile("movbeq %[m], %[r1]\n\tmovbel %[m32], %k[r2]\n\tmovbeq %[r1], %[m]"
                         : [r1] "=&r" (r1), [r2] "+r" (r2), [m] "+m" (m) : [m32] "m" (m32));
            MIX(r1); MIX(r2); MIX(m); } END
    }
}

static void test_strings(void)
{
    TEST("rep movsb/stosq/cmpsb/scasb") {
        uint8_t src[96], dst[96];
        u64 n = b % 80, si, di, cnt, f;

        for (int i = 0; i < 96; i++) {
            src[i] = (uint8_t)(a >> ((i & 7) * 8)) ^ (uint8_t)i;
            dst[i] = (uint8_t)c ^ (uint8_t)(i * 3);
        }
        si = (u64)src; di = (u64)dst; cnt = n;
        asm volatile("rep movsb" : "+S" (si), "+D" (di), "+c" (cnt) : : "memory");
        MIX(si - (u64)src); MIX(di - (u64)dst); MIX(cnt);
        for (int i = 0; i < 96; i++) {
            MIX(dst[i]);
        }
        di = (u64)dst + 8; cnt = n / 8;
        asm volatile("rep stosq" : "+D" (di), "+c" (cnt) : "a" (d) : "memory");
        for (int i = 0; i < 96; i++) {
            MIX(dst[i]);
        }
        dst[n / 2] ^= (uint8_t)(it | 1);
        si = (u64)src; di = (u64)dst; cnt = n;
        asm volatile("cld\n\trepe cmpsb\n\tpushfq\n\tpop %[f]"
                     : "+S" (si), "+D" (di), "+c" (cnt), [f] "=r" (f) : : "memory", "cc");
        MIX(si - (u64)src); MIX(cnt); MIX(f & FLAGS);
        di = (u64)src; cnt = 96;
        asm volatile("repne scasb\n\tpushfq\n\tpop %[f]"
                     : "+D" (di), "+c" (cnt), [f] "=r" (f) : "a" (c >> 8) : "memory", "cc");
        MIX(di - (u64)src); MIX(cnt); MIX(f & FLAGS);
    } END
    TEST("push/pop/movs") {
        u64 r1, r2;
        uint16_t w;
        asm volatile("pushq %[a]\n\tpushw %w[b]\n\tpopw %w[w]\n\tpopq %[r1]"
                     : [r1] "=&r" (r1), [w] "=&r" (w) : [a] "r" (a), [b] "r" (b));
        MIX(r1); MIX(w);
        asm volatile("movq %[a], %%rax\n\tpushq %%rax\n\tpopq %[r2]"
                     : [r2] "=r" (r2) : [a] "r" (c) : "rax");
        MIX(r2);
    } END
}

static void test_sse(unsigned c1_ecx)
{
    SSE2("paddb", "paddb") SSE2("paddw", "paddw") SSE2("paddd", "paddd")
    SSE2("paddq", "paddq") SSE2("psubb", "psubb") SSE2("psubw", "psubw")
    SSE2("psubd", "psubd") SSE2("psubq", "psubq") SSE2("paddsb", "paddsb")
    SSE2("paddusb", "paddusb") SSE2("paddsw", "paddsw") SSE2("psubusw", "psubusw")
    SSE2("pmullw", "pmullw") SSE2("pmulhw", "pmulhw") SSE2("pmulhuw", "pmulhuw")
    SSE2("pmuludq", "pmuludq") SSE2("pmaddwd", "pmaddwd")
    SSE2("pand", "pand") SSE2("pandn", "pandn") SSE2("por", "por") SSE2("pxor", "pxor")
    SSE2("pcmpeqb", "pcmpeqb") SSE2("pcmpeqw", "pcmpeqw") SSE2("pcmpeqd", "pcmpeqd")
    SSE2("pcmpgtb", "pcmpgtb") SSE2("pcmpgtw", "pcmpgtw") SSE2("pcmpgtd", "pcmpgtd")
    SSE2("pminub", "pminub") SSE2("pmaxub", "pmaxub") SSE2("pminsw", "pminsw")
    SSE2("pmaxsw", "pmaxsw") SSE2("pavgb", "pavgb") SSE2("pavgw", "pavgw")
    SSE2("psadbw", "psadbw") SSE2("packsswb", "packsswb") SSE2("packssdw", "packssdw")
    SSE2("packuswb", "packuswb") SSE2("punpcklbw", "punpcklbw")
    SSE2("punpckhbw", "punpckhbw") SSE2("punpcklwd", "punpcklwd")
    SSE2("punpckhdq", "punpckhdq") SSE2("punpcklqdq", "punpcklqdq")
    SSE2("psllw", "psllw") SSE2("pslld", "pslld") SSE2("psllq", "psllq")
    SSE2("psrlw", "psrlw") SSE2("psrld", "psrld") SSE2("psrlq", "psrlq")
    SSE2("psraw", "psraw") SSE2("psrad", "psrad")
    SSESH("psllw$3", "psllw", 3) SSESH("pslld$31", "pslld", 31)
    SSESH("psllq$33", "psllq", 33) SSESH("psrlw$15", "psrlw", 15)
    SSESH("psrld$1", "psrld", 1) SSESH("psrlq$63", "psrlq", 63)
    SSESH("psraw$4", "psraw", 4) SSESH("psrad$17", "psrad", 17)
    SSESH("pslldq$5", "pslldq", 5) SSESH("psrldq$11", "psrldq", 11)
    TEST("pshufd/lw/hw") { u64 x[2] = { a, c }, o[6];
        asm volatile("movdqu %[x], %%xmm1\n\tpshufd $0x1b, %%xmm1, %%xmm0\n\tmovdqu %%xmm0, %[o0]\n\t"
                     "pshuflw $0x72, %%xmm1, %%xmm0\n\tmovdqu %%xmm0, %[o1]\n\t"
                     "pshufhw $0xd8, %%xmm1, %%xmm0\n\tmovdqu %%xmm0, %[o2]"
                     : [o0] "=m" (o[0]), [o1] "=m" (o[2]), [o2] "=m" (o[4]) : [x] "m" (x)
                     : "xmm0", "xmm1");
        for (int i = 0; i < 6; i++) MIX(o[i]); } END
    TEST("movd/movq/pmovmskb/pextrw/pinsrw") { u64 x[2] = { a, c }, r1, r2, r3, o[2];
        asm volatile("movq %[a], %%xmm0\n\tmovq %%xmm0, %[r1]\n\tmovdqu %[x], %%xmm1\n\t"
                     "pmovmskb %%xmm1, %k[r2]\n\tpextrw $5, %%xmm1, %k[r3]\n\t"
                     "pinsrw $3, %k[b], %%xmm1\n\tmovdqu %%xmm1, %[o]"
                     : [r1] "=&r" (r1), [r2] "=&r" (r2), [r3] "=&r" (r3), [o] "=m" (o)
                     : [a] "r" (a), [b] "r" (b), [x] "m" (x) : "xmm0", "xmm1");
        MIX(r1); MIX(r2); MIX(r3); MIX(o[0]); MIX(o[1]); } END
    if (c1_ecx & (1u << 9)) {   /* SSSE3 */
        SSE2("pshufb", "pshufb") SSE2("phaddw", "phaddw") SSE2("phsubd", "phsubd")
        SSE2("pmaddubsw", "pmaddubsw") SSE2("pmulhrsw", "pmulhrsw")
        SSE2("psignb", "psignb") SSE2("pabsd", "pabsd")
        SSEI("palignr$5", "palignr", 5) SSEI("palignr$13", "palignr", 13)
    }
    if (c1_ecx & (1u << 19)) {  /* SSE4.1 */
        SSE2("pminsd", "pminsd") SSE2("pmaxud", "pmaxud") SSE2("pmulld", "pmulld")
        SSE2("pmuldq", "pmuldq") SSE2("pcmpeqq", "pcmpeqq") SSE2("packusdw", "packusdw")
        SSE2("pmovzxbw", "pmovzxbw") SSE2("pmovsxwd", "pmovsxwd")
        SSEI("pblendw$0xa5", "pblendw", 0xa5) SSEI("mpsadbw$5", "mpsadbw", 5)
        TEST("ptest/pextrq/pinsrb") { u64 x[2] = { a, c }, y[2] = { b, d }, r, f, o[2];
            asm volatile("movdqu %[x], %%xmm0\n\tmovdqu %[y], %%xmm1\n\tptest %%xmm1, %%xmm0\n\t"
                         "pushfq\n\tpop %[f]\n\tpextrq $1, %%xmm0, %[r]\n\t"
                         "pinsrb $9, %k[b], %%xmm0\n\tmovdqu %%xmm0, %[o]"
                         : [r] "=&r" (r), [f] "=&r" (f), [o] "=m" (o)
                         : [x] "m" (x), [y] "m" (y), [b] "r" (b) : "xmm0", "xmm1", "cc");
            MIX(r); MIX(f & 0x41); MIX(o[0]); MIX(o[1]); } END
    }
    if (c1_ecx & (1u << 20)) {  /* SSE4.2 */
        SSE2("pcmpgtq", "pcmpgtq")
    }
}

int main(void)
{
    unsigned r1[4], r7[4], r81[4];

    enable_sse();
    cpuid(1, 0, r1);
    cpuid(7, 0, r7);
    cpuid(0x80000001, 0, r81);
    ml_printf("x86-insn-diff: cpuid.1.ecx=%x cpuid.7.ebx=%x cpuid.80000001.ecx=%x\n",
              r1[2], r7[1], r81[2]);

    test_alu();
    test_shifts();
    test_muldiv();
    test_bits(r1[2], r7[1], r81[2]);
    test_strings();
    test_sse(r1[2]);

    ml_printf("x86-insn-diff: done\n");
    return 0;
}
