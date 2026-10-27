# History: what was done, in what order, and why

**What this covers.** The project's history from the first graphical
display to the multi-threaded build: each problem met, how it was
diagnosed, what was changed, and what it gained, with the measurements.
Read it to understand why the code is the way it is, and before undoing
anything that looks odd: most odd-looking choices answer a problem
described here.  The commits named are in this repository and in
m-browser.

## Timeline

| Date | Commit (qemu-wasi / m-browser) | Milestone |
|---|---|---|
| 2026-10-06 to 10-12 | m-browser `6b0c311`, `d379403` | QEMU in m-browser shows a graphical display |
| 2026-10-14 | qemu-wasi `63ece02` (initial), m-browser `e5f882e` | Published patch series; networking with ping |
| 2026-10-15 | m-browser `957669d` | Stopping QEMU no longer crashes the browser |
| 2026-10-20 | `8aba8ad` / `b5ebd0b` "get tcp networking working" | TCP networking (smoltcp), port forwarding |
| 2026-10-21 | `fc2cf6c` / `3c091ad` "run qemu faster" | Faster code generation, background compiling, time slices, 1 GiB |
| 2026-10-21 | `0492e6c` / `b8e54f6` "qemu wasm threads" | One thread per vCPU (patch 0008) |
| later | (uncommitted at the time of writing) | This documentation, tools/perf, a configure fix |

## Before the published history: making it work

The port began from Kohei Tokunaga's Emscripten WebAssembly backend for
QEMU's code generator (TCG), whose Emscripten code paths are still in
`tcg/wasm32` (see [07-tcg-backend.md](07-tcg-backend.md)).  Porting it to
WASI meant replacing everything Emscripten provided through JavaScript:
the host compiles each translated block's module (the `wasi_jit_*`
imports), a minimal GLib and POSIX stubs replace missing libraries, and
QEMU runs as a reactor driven step by step by m-browser (patches
0001-0007; [06-patch-series.md](06-patch-series.md)).

Many code-generation bugs were found with the instruction test
(`tests/x86-insn-diff`, [13-testing.md](13-testing.md)) and fixed in patch
0002: wrong block numbering (backward branches re-ran a block's first
part: x86 `rep` string instructions pushed twice or looped forever),
Int128 helper results clobbering a second register (`cmpxchg16b` stored
to a wrong address), garbage in the upper half of 32-bit values (`clz`,
`ctz`, `extract`, `deposit`, multiplications, shifts, division, `popcnt`,
byte swaps, add/subtract carries).

## The desktop (October 2026)

- **X took about 30 minutes to start.**  The modesetting driver's glamor
  acceleration runs OpenGL, which inside the guest is emulated on the
  emulated CPU.  Fix in the guest: `AccelMethod "none"`
  ([16-guest-image.md](16-guest-image.md)).
- **Keystrokes were lost.**  Two causes.  The guest had QEMU's default
  128 MiB of RAM and swapped constantly, stalling X, so the kernel's
  small keyboard buffer overflowed; and when QEMU's main loop stalled
  (for example while m-browser moved QEMU to a fresh Store), all typed
  keys arrived in one burst.  Fixes: `-m 512` (later 1024), and pacing in
  the display backend, `KEYS_PER_POLL` = 6 key events per 30 ms refresh
  ([09-display-input-network.md](09-display-input-network.md)).
- **"The guest never idles."**  That turned out to be the swapping plus
  Chromium starting automatically.
- **Memory grew without bound.**
  - wasmtime frees compiled modules' instances only with their Store.
    m-browser moves QEMU into a fresh Store between main-loop steps
    (QEMU is a reactor, so no wasm code is running then), copying only
    the non-zero parts of its memory.  A first threshold of 50,000
    instances without checking the process's memory mappings crashed with
    ENOMEM (Linux limits a process to about 65,000 mappings): now 100,000
    instances or 45,000 mappings, whichever comes first.  See the
    m-browser repository's docs/06-qemu-runner.md.
  - QEMU's RCU callbacks (deferred frees) never ran: the RCU thread is a
    pthread that never runs in the single-threaded build.  Fix:
    `rcu_run_callbacks_inline()` from the main loop (patch 0004).
- **Translations were thrown away every few seconds.**  The translation
  buffer (`-accel tcg,tb-size=N`, in MiB) of 64 MiB filled up about every
  7 seconds, and each flush discards all translated code.  256 MiB
  reached the desktop about a minute sooner, for about 400 MiB more host
  memory.  A "warm" tier was added: a block whose identical module the
  host still has compiled goes back to compiled code after 50
  interpreted runs (`QEMU_JIT_WARM`).
- **Profiling.**  A sampling profiler was added to m-browser
  (`QEMU_PROFILE`; the m-browser repository's docs/12-profiling.md) and a
  logger inside the guest (top, interrupt counters; now
  `tools/perf/guest/qemu-profile.start`).  Findings: about a third of the
  time ran guest code, a fifth the interpreter tier, and much of the rest
  QEMU's dispatch and lookups between blocks.
- **QEMU's own module precompiled with optimizations.**  m-browser
  compiles its many small translated-block modules without optimizations
  (compile speed matters), and used to compile QEMU's own large module the
  same way.  It is now compiled once with optimizations and cached:
  about 5% faster.
- Tried and rejected: wasmtime's Winch compiler (no support for wasm
  exceptions, which QEMU's setjmp/longjmp needs); `-nic none` (no network
  is not an option).

## Networking (2026-10-14 to 10-20)

QEMU's usual user-mode network (libslirp) does not build for WASI, so the
`user` network passes Ethernet frames to the host (patch 0006), and
m-browser emulates the 10.0.2.0/24 network: ARP, DHCP, DNS and UDP relay,
ping through unprivileged ICMP sockets, IPv4 fragments (first commit),
then TCP through the smoltcp TCP/IP stack and TCP port forwarding
(`hostfwd`).  A relay loop that spun forever when the host reset a
connection while the guest kept sending was fixed with a failure flag and
a bounded number of rounds per pass, plus a regression test.  The guest's
connection request is only answered once the host's connection succeeds,
so refused and unreachable hosts look right to the guest.  Verified with
`wget`, `apk`, `ping` and DNS.  See the m-browser repository's
docs/10-networking.md.

## "Run qemu faster" (2026-10-21)

Starting point: X after about 5 minutes, the desktop after 7, Chromium's
window after 11-12, and clicks in Chromium taking more than 2 minutes.

### 1. Registers as locals

Reading the generated modules (m-browser's `QEMU_JIT_DUMP`) showed two
inheritances from the Emscripten backend:

- TCG's registers were wasm globals.  Translated modules are compiled
  without optimizations, so every register access was a memory access.
- Every helper call and guest memory access split the code into a
  "resumable" block with flag stores and checks, for Emscripten's
  coroutines, which can leave a block midway and re-enter it later.  On
  WASI a block always runs from its start to an exit.

On WASI the registers became function locals and the resume machinery
went away (patch 0002, `REG_LOCAL_BASE`).  Measured with the guest
benchmark (now `tools/perf/guest/bench.sh`), two VMs side by side:

| Step | Before | After |
|---|---|---|
| forkexec (300 processes) | 20.5 s | 13.2 s |
| find /usr | 17.0 s | 12.5 s |
| bzip2 3 MB | 21.4 s | 16.8 s |
| python loop | 9.3 s | 6.7 s |
| headless Chromium | 613 s | 481 s |
| total | 683 s | 532 s (-22%) |

In the GUI boot, Fluxbox appeared at 296 s instead of 396 s (guest
uptime), Chromium's renderer at 630 s instead of 924 s.

### 2. When to compile

`QEMU_JIT_HOT` (how many interpreted runs before a block is compiled)
was tried at 1000 and 500 against the default 5000: both were slower
(500 lost the whole gain above), because many blocks that reach 1000
runs stop running soon after, so compiling them is wasted work.  Kept at
5000.

### 3. The browser crashed when the screen blanked

"Failed to acquire next swap chain texture: Timeout" panicked m-browser
(and with it the VM) when the host's screen blanked.  Frames are now
skipped in that case (the m-browser repository's docs/05-browser-ui.md).

### 4. Compiling in the background

Compiling a block's module takes about a millisecond, during which the
emulator stopped.  Hot blocks are now compiled on other threads while
QEMU keeps interpreting them (`wasi_jit_load_async`, m-browser's
`QEMU_JIT_THREADS`): about 7% faster, and no compile pauses.  m-browser
also shares one instance per distinct module among all blocks with the
same code.

### 5. The guest's timer tick

The guest kernel ticks 1000 times a second (`CONFIG_HZ=1000`), and a tick
costs the emulated CPU thousands of instructions.  The single-threaded
build returned to the main loop every 4000 translated blocks, well under a
millisecond, and the guest's tick was due at nearly every return: about
600 ticks a second, about half of all guest time spent on timer
interrupts.  Slices are now timed: 30 ms (`QEMU_TB_SLICE_MS`; patch
0003), so at most about 33 ticks a second.  Measurements, single-threaded
build, 1 GiB, each table's runs under the same conditions:

| Slice | Fluxbox first seen (guest uptime) | Timer interrupts while busy |
|---|---|---|
| 4000 blocks (before) | 320 s | about 560 per second |
| 16000 blocks | 196 s | about 300 per second |
| 64000 blocks | 163 s | about 85 per second |

| Slice (kernel booted directly, Chromium tweaks) | Fluxbox first seen | Chromium's window drawn |
|---|---|---|
| 10 ms | 193 s | 336 s after QEMU's start |
| 30 ms (chosen) | 187 s | 316 s after QEMU's start |

The guest sees late ticks, which it handles fine.

### 6. Memory and Chromium

With 512 MiB, Chromium's start pushed about 100 MiB to swap and the
kernel spent a sixth of its time reclaiming memory; 1 GiB removed that
(m-browser now passes `-m 1024`).  Chromium's New Tab page kept the guest
busy; starting it with `about:blank`, with background services off and
`--hide-crash-restore-bubble` (whose bubble swallowed clicks), let the
guest settle soon after the window appeared
([16-guest-image.md](16-guest-image.md)).

### Result

Single-threaded build, each run alone: desktop after 2:03, Chromium's
window after 3:40, guest idle after about 4:40, clicks answered in 8-13 s
once Chromium had settled.  But a click made while Chromium was still
starting was lost.

## "qemu wasm threads" (2026-10-21)

To keep the guest responsive while it is busy, the multi-threaded build
runs each vCPU on its own thread (QEMU's MTTCG), on wasi-threads (patch
0008; [08-threads.md](08-threads.md); m-browser's `src/qemu_threads.rs`).
Bugs found on the way:

- With 2 or 4 vCPUs the boot crawled, stuck in the kernel's decompressor:
  QEMU believed wasm had no 64-bit atomic operations
  (`CONFIG_ATOMIC64` undefined, because meson's test program fails to
  link for an unrelated reason), so most 8-byte guest memory accesses
  missing the TLB stopped all other vCPUs.  Fixed in meson.build.
- About 90 timer interrupts a second instead of 33: the tick-coalescing
  code used a rounding macro that only works for powers of two.
- After a translation-buffer flush only one vCPU reloaded cached modules;
  the others kept interpreting.
- The instruction test failed on its CPUID header line, because m-browser
  added `-smp 4`: the test now passes `-smp 1`.

Result, 4 vCPUs, each run alone: desktop after 2:35, Chromium's window
after 3:55, and clicks answered in 6-9 s even while Chromium was still
starting.  The boot is slightly slower (it is mostly sequential, and the
vCPUs must coordinate), but the guest no longer drops input while busy.

## Documentation pass

This documentation, the `tools/perf` scripts (rebuilt from the ad-hoc
scripts used for the measurements above), and a fix: the threads build's
generated `glib-2.0.pc` also named the single-threaded GLib library,
which would have made a clean `make THREADS=1` fail when `libglib-wasi.a`
did not exist (`scripts/configure.sh`).

## Ideas not pursued yet

See [15-performance.md](15-performance.md) for profiling data and the
next candidates: fewer lookups between translated blocks, a faster
interpreter tier (fused instructions), fewer TLB flushes on guest context
switches, and cheaper cross-vCPU coordination in the threads build.
