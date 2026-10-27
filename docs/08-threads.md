# Threads: the multi-threaded build

**What this covers.** How the multi-threaded flavor (`make THREADS=1`, C
macro `WASI_THREADS`, patch 0008) runs each virtual CPU of the guest on a
host thread of its own and why each of its mechanisms is built the way it
is; then measured results, limitations, debugging, and changing the number
of virtual CPUs. Read it before touching code under `#ifdef WASI_THREADS`,
when the threaded build hangs or misbehaves where the single-threaded one
does not, or before raising the 8-vCPU limit. This is QEMU's side; the
host's side is the m-browser repository's
[docs/08-threads.md](../../m-browser/docs/08-threads.md) (the link works
when both repositories sit side by side, as the Makefile assumes).

Paths are in `qemu-src/` unless they start with `scripts/`, `glib-wasi/`,
`wasi-stubs/`, `tools/` or `tests/`. Terms used below (more in
[reference/glossary.md](reference/glossary.md)):

| Term | Meaning |
|---|---|
| vCPU | A virtual CPU of the guest; `-smp N` gives it N |
| TCG, TB | QEMU's translator of guest code, and a translation block: guest instructions translated as a unit, here into a small wasm module the host compiles ([07-tcg-backend.md](07-tcg-backend.md)) |
| MTTCG | Multi-threaded TCG: one host thread per vCPU, each translating and running guest code |
| BQL | The "big QEMU lock", a global mutex around device state and the main loop |
| Atomic operation | A memory access, or read-modify-write, that other threads see as indivisible |
| Memory ordering, fence | The order in which one CPU's memory accesses become visible to other CPUs ([below](#shared-memory-ordering-and-atomics)); a fence (barrier) is an instruction that makes earlier accesses complete before later ones |
| TLS | Thread-local storage: C `__thread` variables; in wasm, a per-thread block in linear memory whose address is the global `__tls_base` |
| Shadow stack | The stack in linear memory for C locals whose address is taken; the global `__stack_pointer` points into it |
| Instance, Store | A running copy of a wasm module, and wasmtime's container of instances, which frees them only all at once |
| Exclusive section | QEMU stopping every other vCPU to do something they must not see half done (`start_exclusive()`) |
| IPI | Inter-processor interrupt: one CPU interrupting another, to make it reschedule or flush its TLB |

## Overview

```text
m-browser process
+--------------------------------------------------------------------------+
| QEMU's memory: ONE shared wasm memory (env.memory, up to 4 GiB), used by |
| every QEMU instance and every TB module: guest RAM, heap, globals, the   |
| TB code buffer, and per QEMU thread a 16 MiB shadow stack and TLS block  |
+--------------------------------------------------------------------------+
 main loop thread        vCPU thread, one per vCPU     call_rcu thread
 (m-browser's runner)    (qemu-2 = vCPU 0, ...)        (qemu-1)
 wasi_qemu_step()        mttcg_cpu_thread_fn()         call_rcu_thread()
  main_loop_wait():       host_vcpu_run()               runs call_rcu()
   timers, devices,        m-browser's run_vcpu()       callbacks (QEMU's
   display, input,          wasi_vcpu_enter(), again    deferred frees)
   network; sleeps           and again, in a vCPU
   <= 30 ms, BQL free        instance replaced now and then:
                              wasi_vcpu_step(), one
                              iteration of the vCPU loop
 host-only threads: qemu-jit-0..3 (compile TB modules), qemu-net (network)
 shared host state: WASI context (behind a lock), JIT cache, display
```

Each QEMU thread is an instance of the same module on an OS thread of its
own (the wasi-threads convention); a vCPU thread has a second instance
([below](#how-a-vcpu-thread-runs)). The main loop holds the BQL except
while it sleeps; a vCPU thread takes it to access a device or to handle an
interrupt.

## The two flavors compared

| | Single-threaded (`make`) | Multi-threaded (`make THREADS=1`) |
|---|---|---|
| Target, directory | `wasm32-wasip1`, `qemu-build/` | `wasm32-wasip1-threads`, `qemu-build-threads/`, `-DWASI_THREADS -pthread` |
| Threads | `pthread_create()` succeeds but never runs the thread (`wasi-stubs/pthread_stubs.c`) | Real, through the `wasi.thread-spawn` import |
| Locks | No-ops (wasi-stubs, glib-wasi) | wasi-libc's pthreads; `libglib-wasi-threads.a` |
| Memory | Defined and exported by QEMU | Imported, shared: `env.memory`, 256 to 65536 pages, created by the host |
| vCPUs | One, run by the main loop in slices (`wasi_rr_cpu_step()`, `QEMU_TB_SLICE_MS`, 30 ms) | `-smp N`, at most 8, MTTCG; no slices |
| Guest timer tick | Late by up to a slice | On a `QEMU_TICK_SLACK_MS` grid (30 ms) |
| RCU callbacks | Run by the main loop (`rcu_run_callbacks_inline()`) | Run by the `call_rcu` thread |
| Idle main loop | Sleeps up to 10 ms | Sleeps up to 30 ms, BQL released; woken by `host_main_wake` |
| JIT handles per TB | 1 | 8 (`WASI_JIT_SLOTS`), one per vCPU |
| Freeing TB modules | Host moves all of QEMU to a new Store, copying memory; `wasi_qemu_jit_reset()` | Host replaces one vCPU's Store; flag `WASI_VCPU_JIT_RESET` |
| `TCG_TARGET_DEFAULT_MO`; `mb` op | 0; emits nothing | `TCG_MO_ALL & ~TCG_MO_ST_LD`; `atomic.fence` |
| `CONFIG_ATOMIC64` | No | Yes (`meson.build`) |
| Host must support | Wasm exceptions | Also threads and shared memory; x86-64 for correct ordering |

The single-threaded shortcuts of patches 0003, 0004 and 0007 are off
under `!defined(WASI_THREADS)`, so that QEMU's own multi-threaded code
runs ([the list](06-patch-series.md#what-0008-switches-off-under-wasi_threads)).
The threaded flavor is the one installed in m-browser.

## Build plumbing

`THREADS=1` selects every variant at once (details in
[05-repository-tour.md](05-repository-tour.md#scripts)):

| Where | What changes | Why |
|---|---|---|
| `Makefile` | `VARIANT=-threads`: `qemu-build-threads/`, `zlib-wasi-threads/`, `libglib-wasi-threads.a`, `libwasi-stubs-threads.a`; `THREADS` passed on to the scripts and library Makefiles | Both flavors build from the same `qemu-src/`; all outputs are separate |
| `scripts/build-zlib.sh` | `wasm32-wasip1-threads-clang -pthread` | wasm-ld refuses objects compiled without the atomics feature in a module with shared memory |
| `glib-wasi/Makefile`, `glib-wasi/src/gthread.c` | `-DWASI_THREADS -pthread`: real GMutex, GRecMutex, GRWLock, GCond, GOnce, GPrivate and GThread on pthreads | QEMU's GLib-based code needs working locks |
| `wasi-stubs/Makefile` | `pthread_threads_stubs.c` instead of `pthread_stubs.c` | wasi-libc's threads build has real pthreads; only `pthread_sigmask`, `pthread_setname_np` (succeed) and the affinity calls (`ENOSYS`) are missing |
| `scripts/configure.sh` | Target `wasm32-wasip1-threads`, `-DWASI_THREADS -pthread`; `glib-2.0.pc` names `-lglib-wasi-threads` | Never link the single-threaded library's no-op locks |
| `scripts/fix_build_ninja.py` | `THREADS_LINK_FLAGS` (`--import-memory`, `--export-memory`, `--max-memory=4294967296`) when `build.ninja` names `wasm32-wasip1-threads-clang` | Each thread is a new instance, so the memory must come from outside to be the same for all; the host's WASI functions use the export; a shared memory needs a maximum, and 4 GiB is wasm32's limit |

```sh
# from the qemu-wasi root
make THREADS=1            # qemu-build-threads/qemu-system-x86_64
make THREADS=1 install    # copy it to ../m-browser/qemu-system-x86_64.wasm
make THREADS=1 test       # instruction test (one vCPU only, see below)
# must list the shared env.memory, env.host_vcpu_run, env.host_main_wake,
# wasi.thread-spawn and the exports wasi_vcpu_enter, wasi_thread_start:
wasm-tools print --skeleton qemu-build-threads/qemu-system-x86_64 \
    | grep -E '\((import|export)'
```

Full list: [reference/host-interface.md](reference/host-interface.md).

## Creating QEMU's threads

`qemu_thread_create()` (`util/qemu-thread-posix.c`) calls wasi-libc's
`pthread_create()`, which allocates the thread's shadow stack and TLS
block in QEMU's memory and calls `wasi.thread-spawn`; m-browser starts an
OS thread `qemu-<n>` with a new instance and calls its
`wasi_thread_start`. `qemu-1` is `call_rcu`, created by RCU's C
constructor `rcu_init()` during `_initialize`, before QEMU has parsed its
arguments. The vCPU threads follow, from `qemu_init_vcpu()` ->
`mttcg_start_vcpu_thread()` in `wasi_qemu_init()`: vCPU k is usually
`qemu-<k+2>`. QEMU's own names (`CPU 0/TCG`) go nowhere:
`pthread_setname_np()` is a stub.

Under `WASI_THREADS`, `qemu_thread_create()` asks for 16 MiB stacks:
wasi-libc's default is small, and a vCPU thread's shadow stack also holds
the frames of its vCPU instance. Each comes out of QEMU's 4 GiB, which
guest RAM shares.

## How a vCPU thread runs

**Why it is unusual.** wasmtime frees the instances of compiled TB modules
only when their Store is dropped, and a Store can only be dropped while
none of its code is running. Upstream, a vCPU thread spends its whole life
in `mttcg_cpu_thread_fn()`'s loop; run like that, its Store could never be
dropped and would collect TB instances until the process ran out of
memory mappings. So under `WASI_THREADS` the thread hands its loop to the
host, which runs it one iteration at a time in a second instance that it
can throw away between iterations (`accel/tcg/tcg-accel-ops-mttcg.c`,
`tcg/wasm32.c`):

```text
the thread's instance          m-browser                vCPU instance
(lives as long as the thread)  (same OS thread)         (replaced at times)
mttcg_cpu_thread_fn()
  RCU and TCG registration,
  current_cpu = cpu, BQL off
  wasi_vcpu_thread_run()
    host_vcpu_run(cpu_index, --> run_vcpu():
      stack_top, tls_base)       new Store and instance
                                 loop:
                                   wasi_vcpu_enter(  --> __stack_pointer = top
                                     stack_top,          __tls_base = tls base
                                     tls_base, flags)    wasi_vcpu_step(flags)
                                   <-- 0: go on, 1: unplugged
                                   sometimes: drop the Store (frees its
                                   TB instances), new one, flags = 1
                             <-- returns once unplugged
  BQL on, tcg_cpu_destroy()
```

`wasi_vcpu_step(flags)` is one iteration of upstream's MTTCG loop: take
the BQL; if the vCPU may run, release it, run guest code until something
needs attention (`tcg_cpu_exec()`) and retake it; handle the result
(`EXCP_ATOMIC` through `cpu_exec_step_atomic()`, without the BQL); clear
`exit_request`; `qemu_wait_io_event()`, which sleeps while the vCPU is
halted or stopped and runs work queued for it; release the BQL; return 1
if the vCPU is unplugged and cannot run, else 0. The host passes
`WASI_VCPU_JIT_RESET` (1) on the first call into each new Store, and the
step then first forgets this vCPU's TB handles
(`wasi_tb_cache_forget_all()`), which belonged to the old Store.

**Why the two addresses.** A fresh instance's globals hold the module's
initial values: the main thread's stack pointer and a TLS base of 0. QEMU
keeps per-thread state in thread-local variables (`current_cpu`,
`tcg_ctx`, the RCU reader, `bql_locked`, the TB handle ring, `ctx`), so
with the thread's TLS base the vCPU instance sees exactly the thread's
values. Its stack starts 64 bytes below a local of
`wasi_vcpu_thread_run()`, 16-byte aligned: the free part of the thread's
shadow stack. wasm-ld lets only assembly write `__stack_pointer` and
`__tls_base`, so `wasi_vcpu_enter` is inline assembly at the top of
`tcg/wasm32.c`, like wasi-libc's own `wasi_thread_start`. Consequences:

- **Guest code runs only inside `wasi_vcpu_step()`.** TB handles are only
  valid in the instance that loaded them (next section). And QEMU's
  `longjmp()` (`cpu_loop_exit()`) is a wasm exception whose tag the module
  defines, so each instance has its own (the `(tag ...)` line of
  `wasm-tools print --skeleton`): a `sigsetjmp()` in one instance never
  catches a `longjmp()` from another.
- **C function pointers work in every instance**: they are table indices
  fixed at link time, and every instance's table starts with the same
  entries. Only the TB entries the host appends differ.
- **No QEMU lock is held between two steps**, so the host can replace the
  Store without blocking anyone. But a halted vCPU waits inside a step
  (`qemu_wait_io_event()`), so its Store is replaced only once it wakes.

## Compiled TBs: one handle per vCPU

How TBs are interpreted, compiled and called is in
[07-tcg-backend.md](07-tcg-backend.md#running-tbs-the-tiers). A *handle*
is the index of a TB's compiled function in the function table of the
instance that asked the host for it. Each vCPU runs in an instance of its
own, so each TB keeps one handle per vCPU (`tcg/wasm32.h`):

```text
tb_ptr + export_vec_offset:  u32 4 * WASI_SLOTS (40)
                             u32 handle[WASI_JIT_SLOTS]  one per vCPU, 8
                             u32 run count       [WASI_SLOT_COUNT], shared
                             u32 decoded program [WASI_SLOT_PROG], shared
```

| State | Per thread (TLS) or shared |
|---|---|
| `jit_slot`, this thread's slot: its `cpu_index`, set on its first `tcg_qemu_tb_exec()`, which aborts (`at most 8 vCPUs are supported`) if it is `WASI_JIT_SLOTS` or more | Per thread |
| `ctx` (`struct wasmContext`) with its two 2 KiB stacks | Per thread |
| `tbc`: ring of the handles this thread loaded, so it can forget them (512 KiB, allocated on first use) | Per thread |
| The interpreter decoder's scratch buffers (`interp_decode()`) | Per thread |
| Run counts, decoded programs and their arena (`INTERP_LOCK()`, a pthread mutex), `tb_flush_gen`, statistics | Shared |

- **Flushes.** When the code buffer is full, `tcg_region_reset_all()`
  calls `wasi_tb_cache_flush()`: it retires the decoded programs and
  increments `tb_flush_gen`. A thread whose ring is from an older
  generation empties it without touching the slots, which now belong to
  new TBs. With several vCPUs QEMU flushes in an exclusive section, with
  one on its only vCPU thread; so no other thread is running a TB, which
  the retired-program list and the plain slots rely on.
- **Warm reload.** Retranslated code produces the same bytes, which the
  host may still have compiled
  ([07-tcg-backend.md](07-tcg-backend.md#position-independence)). The
  single-threaded build asks once, when the run count equals
  `QEMU_JIT_WARM`. The count is shared, so with that rule only one vCPU
  asked and the others interpreted the TB up to `QEMU_JIT_HOT` (5000)
  runs; now every vCPU asks every `JIT_ASYNC_RETRY` (16) runs in between.
- **Tolerated races.** Run counts are incremented without atomics: a lost
  increment only shifts when a TB is offered to the JIT. Slots are 4-byte
  aligned (`wasm_finalize_module()`), so no thread reads a half-written
  value. Two vCPUs decoding one TB at once waste one program.

**Pitfall:** never keep a TB handle where another thread, or the same
thread after a Store replacement, could call it: it would call whatever
has that index in the other table, or trap.

## Shared memory, ordering and atomics

**Shared memory.** Every TB module imports QEMU's memory as shared too
(`env.buffer`, limits flag 0x03: shared, 1 to 65536 pages;
`wasm_finalize_module()`): an import must match the memory it receives.

**Memory ordering, briefly.** A CPU may make its memory accesses visible to
other CPUs in another order than the program performed them. x86 promises
a lot ("total store order", TSO: only a later load may complete before an
earlier store); ARM much less. Guest code written for x86, such as the
Linux kernel's lock-free code, relies on that. TCG compares the guest's
needs (`TCG_GUEST_DEFAULT_MO`, for x86 `TCG_MO_ALL & ~TCG_MO_ST_LD`) with
the backend's promise (`TCG_TARGET_DEFAULT_MO`) and puts an `mb` op before
every guest load and store for the difference (`tcg_gen_req_mo()`,
`tcg/tcg-op-ldst.c`).

The threaded build promises x86 order (`tcg/wasm32/tcg-target.h`), so
ordinary accesses get no barrier. That holds because TB loads and stores
are plain wasm accesses that Cranelift (wasmtime's compiler) emits in
program order (m-browser compiles TB modules without optimization), the
interpreter performs them one opcode at a time, and an x86-64 host keeps
them in x86 order. With the single-threaded value, 0, every guest access
would get a fence (there the `mb` ops exist too, but emit nothing).
Explicit guest barriers (`mfence`, `lfence`, `sfence`) still become `mb`:
`atomic.fence` (bytes `fe 03 00`, `tcg-target.c.inc`), which the
interpreter runs as `I_FENCE`, a sequentially consistent
`__atomic_thread_fence()`.

**Pitfall:** the argument fails on hosts other than x86-64 (m-browser warns
that "the vCPU threads may see each other's memory accesses out of
order"). Whether it survives m-browser's `QEMU_WASM_OPT=speed`, which
optimizes TB modules too, has not been checked.

**Locked instructions.** With more than one vCPU, TBs are translated with
`CF_PARALLEL`, and x86's `lock`-prefixed instructions (and `xchg` with
memory) become helper calls doing real atomic operations in C
(`qatomic_*()`, wasm's atomic instructions); with one vCPU, plain inline
code. What the host cannot do atomically goes through
`cpu_exec_step_atomic()`: the vCPU leaves guest code with `EXCP_ATOMIC`
and runs one instruction in an exclusive section, stopping all others.

**`CONFIG_ATOMIC64`** tells QEMU the host has 8-byte atomics. meson finds
out by linking a test program, which fails on wasm-ld for an unrelated
reason (meson adds `--start-group`, which wasm-ld rejects), so
`meson.build` forces it when the compiler defines `__wasm_atomics__`.
Without it, with several vCPUs, every 8-byte guest access that missed the
TLB (QEMU's cache of guest address translations; misses go through C
helpers in `accel/tcg/cputlb.c` and `ldst_atomicity.c.inc`) took the
exclusive path, and boots with 2 or 4 vCPUs crawled. From the code, 8-byte
locked instructions would too: their helpers exist only `WITH_ATOMIC64`
(`tcg/tcg-op-ldst.c`).

**16-byte atomics** do not exist in wasm (`HAVE_CMPXCHG128` is 0), so a
guest `lock cmpxchg16b` with several vCPUs always takes the exclusive path.
How often the guest kernel does that has not been measured.

## The main loop: sleeping and waking

`main_loop_wait()` (`util/main-loop.c`) sleeps until the next timer
deadline, at most 30 ms, **with the BQL released**, in `wasi_idle_wait`
(`host_net_wait()` when there is a `user` network, `net/wasi-user.c.inc`)
or `nanosleep()`; then it polls QEMU's event loops and runs timers. With
the BQL held, every vCPU that touches a device or takes an interrupt would
wait. The cap is for host input, which the display refresh polls.

Upstream wakes a sleeping main loop through an event notifier, a file
descriptor its `poll()` watches. Here the main loop sleeps in the host,
out of reach of QEMU's fake event notifiers (and glib-wasi never sets
`notify_me`). So `aio_notify()` (`util/async.c`), run whenever a thread
schedules work for the main loop or moves a timer earlier, calls
`wasi_main_wake()`, which calls the import `host_main_wake` (m-browser's
`VirtualNet::wake_main()`: it ends the current or next `host_net_wait()`)
unless it runs on the main thread itself (the thread-local
`wasi_main_thread`, set by `qemu_init_main_loop()`). The event notifiers'
flag and fake-fd counter are atomic too (`util/event_notifier-posix.c`).

**Pitfall:** `nanosleep()` cannot be interrupted. Without a `user` network
(`-nic none`), or while the guest's network card has no room for frames,
the main loop notices other threads' work up to 30 ms late.

## The guest's timer tick

Linux's periodic timer interrupt, the tick, comes from each CPU's local
APIC timer (a timer in the CPU's interrupt controller, `hw/intc/apic.c`).
Under emulation a tick costs a busy guest thousands of instructions; at
1000 ticks a second that was about half of its time. The single-threaded
build gets fewer, late ticks for free, because the main loop only runs
timers between 30 ms slices. The threaded build has no slices, so
`apic_timer_slack()` rounds each deadline up to the next multiple of
`QEMU_TICK_SLACK_MS` (default 30, read once; 0 keeps deadlines exact) on
QEMU's virtual clock: about 33 ticks a second, and guest timers fire up to
30 ms late.

**Note:** the rounding must be `QEMU_ALIGN_UP()`. An earlier version used
`ROUND_UP()`, which only works for powers of two, and gave about 90 ticks
a second instead of 33.

## RCU, other threads, locks

RCU (read-copy-update) lets threads read shared structures without locks:
a writer publishes a new copy, and `call_rcu()` frees the old one once no
reader thread can still be using it. Here the `call_rcu` thread runs those
frees as upstream does; `rcu_run_callbacks_inline()` (patch 0004) is
compiled but not called. vCPU threads register as readers in
`mttcg_cpu_thread_fn()`, and `mttcg_force_rcu()` kicks one out of guest
code when the wait for readers is held up by it.

`thread_pool_submit_co()` runs its work synchronously in both flavors
(patch 0004), so disk I/O happens on the thread that issued it, often a
vCPU thread emulating the disk controller with the BQL held. A normal run
creates no other QEMU threads (QEMU has more for unused features such as
`-object iothread`, migration or memory preallocation).

QEMU's `QemuMutex`, `QemuCond` and `QemuEvent` are wasi-libc's pthreads,
which wait with wasm's `memory.atomic.wait32` and `notify`; wasmtime parks
the OS thread itself, and m-browser never sees these calls. GLib locks
come from glib-wasi's threads variant.

**Pitfall:** glib-wasi's `g_atomic_*` macros (`glib-wasi/glib/gatomic.h`)
are plain reads and writes in both variants, and the reference counts of
GArray, GPtrArray, GByteArray, GHashTable, GTree, GBytes, GMainContext,
GSource and GMappedFile are plain `++` and `--`. Nothing in the current
build calls `g_atomic_*`, and `tcg/region.c` uses its trees (QTree, here
glib-wasi's GTree) under its own locks; code that runs on several threads
must use QEMU's `qatomic_*()` and `QemuMutex`.

## Quitting

When QEMU exits normally, `wasi_qemu_step()` (`system/main.c`) runs
`qemu_cleanup()`, then sets `stop` and `unplug` on every vCPU and kicks it,
so that its thread ends and the host can drop its Store.

**Note:** from reading the code (not tested), this probably does not
work: the main thread still holds the BQL then (QEMU takes it in
`qemu_init_subsystems()`, and only `main_loop_wait()` lets go of it), and a
kicked vCPU needs the BQL to leave `qemu_cond_wait()` or to begin its next
step. The vCPU threads would stay blocked, using no CPU, until m-browser
exits; a `bql_unlock()` after the unplug loop is the likely fix.

## Results

Single-threaded build against the threaded build with 4 vCPUs on the
development machine, booting the Alpine desktop image with Chromium tweaks
([16-guest-image.md](16-guest-image.md),
`tools/perf/guest/chromium-fast.cmds`), each run alone:

| Milestone | Single-threaded | Threads, 4 vCPUs |
|---|---|---|
| Desktop (Fluxbox) appears | 2:03 | 2:35 |
| Chromium's window is drawn | 3:40 | 3:55 |
| First click, made while Chromium is still loading | lost | menu after 9 s |
| Clicks once Chromium has settled | 13 s | 6 s |

`tools/perf/boot-timing.sh` (`CLICKS=1`; `QEMU_WASM=` picks the build,
`SMP=` the vCPUs) and `click-latency.sh` measure such numbers
([15-performance.md](15-performance.md)).

Booting is slower because it is mostly serial: one vCPU does most of the
work at any moment. Meanwhile coordination costs: IPIs (rescheduling,
function-call and TLB-shootdown interrupts) cross host threads, and each
makes the target vCPU leave guest code and take the BQL; locked
instructions become helper calls; each vCPU loads every hot TB into its
own Store (causes read from the code, not measured). The gain is
responsiveness: X and the window manager get a vCPU while Chromium keeps
the others busy.

## Limitations and pitfalls

- **x86-64 hosts only** for correct memory ordering
  ([above](#shared-memory-ordering-and-atomics)); **at most 8 vCPUs**
  ([below](#changing-the-number-of-vcpus)); glib-wasi's atomics and
  reference counts are not thread-safe ([above](#rcu-other-threads-locks)).
- **Profiling.** m-browser's `QEMU_PROFILE` does not follow vCPU threads.
  `QEMU_PROF_EVERY` measures (and pays for it) but never prints: the
  threaded main loop does not call `wasi_prof_report_maybe()`. Statistics
  counters (`interp_stats`, `wasi_prof`, `wasi_tbs_executed`) are not
  atomic, so the reports at TB buffer flushes and of `QEMU_STATUS_EVERY`
  are approximate. Profile with the single-threaded build.
- **A failing thread ends the process.** By wasi-threads' rules a trap or
  `exit()` on any QEMU thread ends the whole m-browser process, GUI
  included, without closing the disk image cleanly.
- **Options that do not apply.** `QEMU_TB_SLICE_MS` and `QEMU_TB_SLICE`
  are ignored. `-accel tcg,thread=single` is refused ("This build runs
  each vCPU on a thread of its own"). `-icount` turns MTTCG off, and
  nothing in this build drives round-robin TCG, so from the code the vCPU
  would never run (not tried).
- **The instruction test uses one vCPU**: `tests/x86-insn-diff/run.sh`
  passes `-smp 1` (otherwise m-browser adds its own `-smp`, and the CPUID
  header line differs from the native run's). It never exercises the
  atomic helpers of `CF_PARALLEL`; boot the desktop with several vCPUs too.
- **`#ifdef WASI_PLATFORM` means both flavors.** A shortcut that is only
  safe with one thread needs `!defined(WASI_THREADS)` as well.
- **Host imports run on any thread**, several at once. Do not block in
  one while holding the BQL: every thread that needs it waits too.
- **I/O is serialized twice:** device emulation, synchronous disk I/O
  included, runs under the BQL, and m-browser runs the WASI calls of all
  threads one at a time behind its WASI lock.

## Debugging threads

| Thread name (`ps -L`) | What it is |
|---|---|
| `m-browser` | The GUI thread, and QEMU's main loop thread (unnamed, so it shows the process name) |
| `qemu-1` | `call_rcu` |
| `qemu-2`, `qemu-3`, ... | vCPU 0, vCPU 1, ... (creation order; a vCPU thread has `run_vcpu` in its backtrace) |
| `qemu-jit-<n>`, `qemu-net` | m-browser's background compilers, its network thread |

```sh
# any directory; the newest m-browser process
ps -L -o tid,comm,pcpu,time -p "$(pgrep -n m-browser)"   # CPU per thread
top -H -p "$(pgrep -n m-browser)"                        # live
gdb -p "$(pgrep -n m-browser)" -batch -ex 'thread apply all bt'  # sudo?
```

A busy guest keeps its vCPU threads near 100% each, an idle one near 0%
(a halted vCPU sleeps on a condition variable). The main loop thread uses
little unless display refreshes are expensive (`QEMU_DISPLAY_STATS`),
`call_rcu` next to nothing, `qemu-jit-*` work in bursts. In gdb, wasm
frames are bare addresses; a thread waiting for a QEMU lock sits in
wasmtime's atomic-wait code. The monitor's `info cpus` shows `thread_id=1`
for every vCPU: there is no `gettid()`, and the `getpid()` stub returns 1.

| Symptom | Likely cause | Next step |
|---|---|---|
| Screen and monitor frozen, all `qemu-*` threads near 0% | Deadlock: a thread waits for a lock, often the BQL, whose holder waits for it in turn or is blocked in a host call | gdb backtraces; does `QEMU_STATUS_EVERY` still print? (the main loop prints it) |
| Screen frozen, the monitor (Ctrl+Alt+2) answers, vCPU threads at 100% | The guest spins, say on a lock whose holder's vCPU does not get to run, or vCPUs keep taking the exclusive path | `info cpus`, `info registers -a`, `QEMU_LOOP_DIAG` |
| Slow, vCPU threads well below 100% while the guest is busy | Lock contention, usually on the BQL | Monitor: `sync-profile on`, wait, `info sync-profile` (QEMU's lock profiler; not yet tried in these builds) |
| Several threads use CPU while the guest is idle | m-browser's WASI lock: waiters spin | m-browser's docs/08-threads.md |
| Quitting takes 10 s, then "QEMU did not quit" | The main loop is blocked | gdb backtraces |

Narrow a problem down: the threaded build with `QEMU_SMP=1` (real threads,
but no `CF_PARALLEL`: no atomic helpers, no exclusive sections), then the
single-threaded build (`make install`; `make THREADS=1 install` goes
back). A bug that survives both is not about threads; see
[14-debugging.md](14-debugging.md).

## Changing the number of vCPUs

m-browser adds `-smp N` when QEMU's arguments have none: `QEMU_SMP` (1 to
8), else 4 on hosts with at least 8 CPUs and 2 on smaller ones; an
explicit `-smp` wins (`SMP=` for `tools/perf`). More vCPUs than free host
cores only take turns: the main loop, JIT compilers and network need some.

To raise the limit of 8:

1. `WASI_JIT_SLOTS` in `tcg/wasm32.h` (its `WASI_THREADS` branch); the
   `WASI_SLOT_*` macros, `wasm_finalize_module()`'s `export_vec` and the
   check in `tcg_qemu_tb_exec()` follow. Each slot costs 4 bytes per TB.
2. m-browser's `.clamp(1, 8)` of `QEMU_SMP` in `run_wasm_module()`
   (`src/wasm.rs`).
3. QEMU's memory: per vCPU a 16 MiB shadow stack, a TLS block and a
   512 KiB handle ring, out of the 4 GiB that guest RAM also uses. The TB
   code buffer is handed out to the vCPUs in regions (`tcg_n_regions()`,
   `tcg/region.c`).
4. The host: per vCPU an OS thread with a 16 MiB stack and a Store. Each
   vCPU instantiates hot TBs in its own Store, and memory mappings count
   for the whole process, so Stores are replaced more often.
5. Contention on the BQL, the WASI lock and exclusive sections grows with
   the count. Measure with `tools/perf` before and after.

Then build both flavors, run `make THREADS=1 test`, and boot the desktop
with `QEMU_SMP=<n>`; in the guest, `nproc` must print `<n>`.
