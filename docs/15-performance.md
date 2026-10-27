# Performance: measuring, knobs, and what is left to gain

**What this covers.** How fast the emulated desktop is, how to measure it,
where the time goes, every setting that affects speed (with what it was
measured to do), and the most promising ideas for making it faster.  Read
it before any performance work, and to judge whether a change made things
better or worse.  [17-history.md](17-history.md) tells the story of each
optimization; this chapter is the reference.

## Where things stand

Measured on a laptop with a 12th-generation Intel Core i7-12800H (20
hardware threads) on mains power, the Alpine image with its Chromium
settings ([16-guest-image.md](16-guest-image.md)), each run alone:

| | Single-threaded build | Multi-threaded build, 4 vCPUs |
|---|---|---|
| Desktop (Fluxbox) appears | 2:03 | 2:35 |
| Chromium's window appears | 3:40 | 3:55 |
| Click on Chromium's menu while Chromium is still starting | lost | answered in about 9 s |
| Click once Chromium has settled | 8-13 s | 6-9 s |

Times are since QEMU started.  On battery power the same machine is
30-50% slower (a run on battery with the current image: Chromium's
toolbar painted after 6:22, clicks answered in 11-15 s).  Starting point before the work in
[17-history.md](17-history.md): desktop after 7 minutes, Chromium after
11-12, clicks over 2 minutes.

For scale: the emulated CPU runs roughly a few hundred times slower than
the host CPU running the same code natively.

## How to measure

| Tool | Measures | Works with | Chapter |
|---|---|---|---|
| `tools/perf/boot-timing.sh` | When X, Fluxbox, Chromium start; guest CPU use; swapping; timer interrupts | Both builds | [tools/perf/README.md](../tools/perf/README.md) |
| `tools/perf/click-latency.sh` | Seconds from a click to Chromium's menu | Both builds | same |
| `tools/perf/bench-run.sh` | Fixed guest workloads (processes, files, compression, Python, Chromium) | Both builds | same |
| m-browser's `QEMU_PROFILE=1` | Where QEMU's host time goes, by function (sampling profiler) | Single-threaded build only | the m-browser repository's docs/12-profiling.md |
| `QEMU_PROF_EVERY=<s>` | Coarse split: translating, compiling, interpreted vs compiled execution, main loop, idle | Single-threaded build only (prints nothing in the threads build) | [07-tcg-backend.md](07-tcg-backend.md) |
| m-browser's `QEMU_JIT_STATS=1` | Translated blocks compiled, reused, shared; memory mappings | Both | the m-browser repository's docs/07-jit-host.md |
| The guest logger | Per-process CPU in the guest, interrupts, swapping | Images from `tools/perf/prepare-base.sh` | [tools/perf/README.md](../tools/perf/README.md) |

Method:

1. Measure a baseline with the current build, then the change, in the
   same session, on mains power, one VM at a time.
2. Repeat each run at least twice; differences under about 10% between
   two single runs are noise.
3. Compare like with like: the guest logger samples every 10 s, and
   "window drawn" in `click-latency.sh` means the toolbar is painted
   (earlier experiments used a cruder signal, so their numbers are not
   directly comparable with today's).
4. For two QEMU builds: `QEMU_WASM=...` with `boot-timing.sh`; for a
   QEMU setting: the variable on the command line
   (`QEMU_TB_SLICE_MS=10 tools/perf/boot-timing.sh s10`).

## Where the time goes

Profiles taken with m-browser's sampling profiler on the single-threaded
build, before the "registers as locals" and time-slice changes (so guest
code was slower then; the proportions of the rest still hold).  "Self" is
time in the function itself, "total" includes what it calls.

| Phase | Guest code (translated blocks) | Interpreter tier | Dispatch (`tcg_qemu_tb_exec` self) | Block lookup (`helper_lookup_tb_ptr` total) | Compiling blocks | Other notable |
|---|---|---|---|---|---|---|
| Kernel boot (0-120 s) | 11-29% | 31-44% | about 12% | | 7-9% | |
| Services (120-240 s) | 34% self, 51% total | 20% | 15% | 12% (hash table lookups 3%) | 3% | translating 2%, TLB fills 2.4%, clock reads 1.8% |
| X and Fluxbox | 33% self, 53% total | 17% | 16% | 13% | 2% | TLB fills 3.7% |
| Chromium | 26% self, 43% total | 25% | 14% | 11% | 2% | code generation 3.7% |

What it means:

- **Guest code** is the useful work.  Everything else is overhead.
- **The interpreter tier** runs blocks until they have run 5000 times
  (`QEMU_JIT_HOT`).  It is cheap per block (no compile), but each run
  costs about 0.9 µs, against about 0.35 µs compiled.
- **Dispatch and lookup**: every translated block returns to the dispatch
  loop in `tcg_qemu_tb_exec()` (tcg/wasm32.c), which calls the next one;
  blocks that end with an indirect jump (returns, indirect calls) look up
  their successor in a hash table (`helper_lookup_tb_ptr`).  Together
  about a quarter of the time.
- **TLB fills** (`x86_cpu_tlb_fill`): the guest switches processes often,
  which flushes QEMU's software TLB, and every first access to a page
  afterwards walks the guest's page tables.
- **Clock reads** were partly the coarse profiler's own; they are now
  only made when `QEMU_PROF_EVERY` is set.

At that time the guest itself spent 18-50% of its time handling
interrupts (mostly the timer tick, about 500-775 interrupts a second);
the time-slice change cut the timer interrupts by more than 10x.

Which blocks the interpreter runs (QEMU_PROF_EVERY's histogram, desktop
phase): 46-65% of interpreted runs were of blocks that had already run
1000-5000 times.  That suggested compiling earlier, but measurements
showed it loses: see `QEMU_JIT_HOT` below.

## The knobs

| Knob | Where | Default | Measured effect |
|---|---|---|---|
| Guest RAM `-m` | m-browser `src/app.rs` | 1024 MiB | 512: Chromium's start swaps ~100 MiB, kernel busy reclaiming; 128: constant swapping, lost keys |
| `-accel tcg,tb-size=N` | m-browser `src/app.rs` | 256 MiB | 64: translation buffer flushed every ~7 s, desktop ~1 minute later.  Costs host memory |
| `QEMU_JIT_HOT` | QEMU, tcg/wasm32/wasi-interp.c.inc | 5000 | 1000 and 500 slower (500 lost the whole gain of a codegen improvement): compiling blocks that soon stop running is wasted |
| `QEMU_JIT_WARM` | QEMU | 50 | Reloads blocks compiled before a flush quickly; not tuned further |
| `QEMU_JIT_ASYNC` | QEMU | on | Compiling in the background: about 7% faster, no compile pauses |
| `QEMU_JIT_THREADS` | m-browser `src/qemu_jit.rs` | a quarter of the host's CPUs, 1-4 | Not measured separately (2 and 4 both used in tests) |
| `QEMU_TB_SLICE_MS` | QEMU (single-threaded build) | 30 | 4000-block slices: ~600 guest ticks/s, desktop ~2x later; 10 ms: Chromium ~20 s later than 30 ms |
| `QEMU_TICK_SLACK_MS` | QEMU (threads build), hw/intc/apic.c | 30 | The same effect for the multi-threaded build |
| `QEMU_SMP` / `-smp` | m-browser `src/wasm.rs` | 4 (2 on hosts with fewer than 8 CPUs) | 4 vCPUs vs the single-threaded build: desktop ~25% later, Chromium's window ~7% later, but clicks answered while busy |
| `QEMU_WASM_OPT` | m-browser `src/wasm.rs` | QEMU's module precompiled optimized, blocks unoptimized | Precompiling QEMU optimized: ~5%.  `speed` (blocks optimized too) is an experiment: compiling becomes much slower |
| Store migration threshold | m-browser `src/wasm.rs` | 100,000 instances or 45,000 mappings | Rarely reached since blocks share instances |
| Guest kernel options | `KBOOT=1 KAPPEND=...` with boot-timing.sh | | `mitigations=off`, `transparent_hugepage=never`: no measurable effect |
| Chromium settings | in the image | `zz-fast.conf`, `about:blank` | Guest settles within about a minute of the window appearing, instead of never; clicks 8-13 s instead of ~50 s |

## Reading a guest log

The guest logger's output (`WORK/serial-NAME.log`; format in
[tools/perf/README.md](../tools/perf/README.md)) answers "what is the
guest doing":

- **high `irq`** in top's CPU line: interrupt handling, mostly the timer
  tick.  Check the `LOC` rate (`analyze-serial.py` prints it): with the
  tick coalescing working it is at most about 33 per second per busy CPU.
- **high `sys`**: the guest kernel is working: page faults, memory
  reclaim (`kswapd`, `kcompactd` in the process list), process creation.
- **swap counters** (`pswpin`, `pswpout`) above zero: the guest needs
  more RAM.
- **`RES`, `CAL`, `TLB`** in `/proc/interrupts` (threads build):
  interrupts between vCPUs (rescheduling, function calls, TLB
  shootdowns).  They cost more than on real hardware, because the target
  vCPU must notice them between translated blocks.

## Ideas for further speed

Ordered by expected gain for the effort; none has been tried yet.

1. **Fewer block lookups.**  About a quarter of the time is dispatch and
   lookup.  Ideas: chain blocks inside wasm (a compiled block calling its
   successor directly, e.g. with wasm tail calls, instead of returning to
   the dispatch loop each time); keep QEMU's per-vCPU jump cache warm
   across guest process switches (it is flushed with the TLB on every
   `CR3` write).  Touches [07-tcg-backend.md](07-tcg-backend.md) territory:
   keep the instruction test passing in all modes.
2. **A faster interpreter tier.**  It runs a fifth to a quarter of the
   time.  The generated code has frequent patterns that could become
   single "superinstructions" in the decoded program, for example (counts
   from a sample of 87 blocks): `i64.const K; local.set R; local.get R`
   (567), `local.get CTX; i32.load; i32.const K` (509),
   `local.get T; local.get R; i64.store` (492),
   `local.get R; local.get R; i64.add` (416).  Also: avoid zeroing all 33
   locals on every run.  See `interp_decode()` and `interp_exec()` in
   tcg/wasm32/wasi-interp.c.inc.
3. **Better code for hot blocks.**  Compiled blocks are compiled without
   optimizations (fast to compile).  A third tier could recompile the
   hottest few hundred blocks with optimizations (m-browser compiles with
   wasmtime's Cranelift; `OptLevel::Speed` for a separate engine), or the
   backend could emit tighter wasm (fewer redundant `local.set` /
   `local.get` pairs).
4. **Cheaper vCPU coordination** (threads build): measure IPI rates with
   the guest logger; try `SMP=2`, which may boot faster than 4 while
   keeping most of the responsiveness.
5. **A guest kernel with `HZ=100`.**  The remaining tick cost would drop
   by up to 10x more; needs a custom-built guest kernel (Alpine's `virt`
   kernel uses 1000).
6. **Fewer TLB flushes.**  Every guest process switch flushes QEMU's
   software TLB for user addresses; QEMU's x86 emulation could keep
   entries tagged by process (PCID), which is a substantial change in
   QEMU's x86 code.

**Pitfall:** measure every idea with tools/perf before keeping it.
Several plausible ideas made things slower (compiling earlier; very short
slices), and the reasons only showed in measurements.
