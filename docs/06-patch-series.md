# The patch series

**What this covers.** What the QEMU patch series in `patches/` is, why the
project keeps its QEMU changes that way, how the patch files are stored,
how the eight patches depend on each other, and then each patch: what it
does, which files it touches, why it was done that way, where it will
conflict with a newer QEMU and how to tell that it works. Read it before
changing QEMU (the workflow is in [10-changing-qemu.md](10-changing-qemu.md))
and before upgrading it ([11-upgrading-qemu.md](11-upgrading-qemu.md)).
Paths are relative to `qemu-src/` unless they start with `patches/` or
`scripts/`.

## What a patch series is

A *patch* is a text file describing a change to some files: lines to
remove, lines to add, and a few unchanged lines of context so that it
still applies when the file has shifted a little. A *patch series* is an
ordered list of patches applied one after the other. `git format-patch`
turns commits into patch files, and `git am` ("apply mailbox"; the files
are in the e-mail format QEMU's developers use) turns them back into
commits.

The project keeps no copy of QEMU's source (no *fork*), only
`patches/BASE`, the upstream release tag (`v9.1.0`), and one patch per
topic, `patches/0001-*.patch` to `patches/0008-*.patch`:

```text
qemu-wasi repository                      qemu-src/ (own git checkout,
patches/BASE        make (clone, git am)             ignored by qemu-wasi)
patches/0001..0008  ------------------->  v9.1.0 + one commit per patch
                    <-------------------  (branch qemu-wasi): you edit
                    make export-patches   and commit here
```

Both scripts are described in
[05-repository-tour.md](05-repository-tour.md#scripts). Why not a fork is
not written down; the benefits in practice: the series is exactly the
difference from upstream, sorted by topic and described, where a fork
spreads it over history and merges; a fix is folded into its topic's
patch, so the series always shows the final design; upgrading means
re-applying eight commits to a new tag
([11-upgrading-qemu.md](11-upgrading-qemu.md)); and about 600 KB of
patches replace QEMU's 10,000 files. The price: two git repositories, and
a change is only safe once exported and committed in qemu-wasi
(`make reset-qemu` throws away whatever in `qemu-src/` was not exported).

## How the patches are stored

`scripts/export-patches.sh` runs `git format-patch --zero-commit
--no-signature --base=v9.1.0`. What the parts of a patch file mean, from
the top (the example is 0006):

| Part | Meaning |
|---|---|
| `From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001` | Normally the commit's hash, which a rebase changes for every later commit; `--zero-commit` writes zeros so that untouched patches stay byte-identical. The date is a constant |
| `From:`, `Date:` | Author and author date; folding a fix into a patch keeps them |
| `Subject: [PATCH 6/8] net: user-mode networking ...` | `git am` strips the bracket. Adding a patch turns `/8` into `/9` everywhere: when 0008 was added, the other seven files changed in this line only |
| The text up to `---` | The commit message: the patch's description, which this document summarises; keep it true |
| From `---` to the first `diff --git` | A summary of the files changed, ignored by `git am` |
| `diff --git`, `index c8f6797..21f579f`, `@@ -181,6 +181,10 @@` | The diff. The `index` ids (file contents before and after) and the `@@` line numbers also change in later patches that touch the same file |
| The end | Nothing: `--no-signature` drops the git version that would end the file |

The first patch ends with `base-commit:
fd1952d814da738ed107e05583b3e02ac11e88ff` (`--base`), the commit tag
`v9.1.0` points to (in `qemu-src/`, `git rev-parse 'v9.1.0^{commit}'`;
the tag object has an id of its own). `git am` ignores it; it must agree
with `patches/BASE`, which `scripts/fetch-qemu.sh` reads. File names are
the number and the subject with every run of characters other than
letters, digits, `.` and `_` turned into `-`, cut at 57 characters (git's
limit of 64 with `.patch`), as in
`0003-accel-tcg-run-the-vCPU-inline-from-the-main-loop-on-.patch`.
Rewording a subject renames the file; the export deletes the old files
first.

**Pitfall:** never edit a patch file by hand: the `@@` line counts must
be exact, and a hand-edited patch typically fails in `git am` on the next
machine. Change `qemu-src/` and export instead.

## How the patches depend on each other

```text
v9.1.0  (patches/BASE)
  |
0001 build, portability --- WASI_LOG(), coroutine-wasi, osdep.h
  |
0002 TCG backend ---------- defines wasi_slice_*(), wasi_prof_*(),
  |                         wasi_tb_cache_*(), wasi_tb_exec_report()
0003 vCPU inline ---------- uses 0002's time slice and profile;
  |                         defines wasi_rr_cpu_step(), tb_history_dump()
0004 main loop, RCU ------- uses 0002's profile; defines wasi_idle_wait,
  |                         rcu_run_callbacks_inline()
0005 text consoles -------- stands alone
  |
0006 user network --------- sets 0004's wasi_idle_wait; defines
  |                         wasi_net_poll()
0007 reactor -------------- qemu_main_loop_step() calls 0002, 0003, 0004
  |                         and 0006
0008 vCPU threads --------- extends 0002; switches off parts of 0003,
                            0004 and 0007 under WASI_THREADS
```

Later patches use earlier ones, never the reverse. Only the complete
series is built and tested (the top-level README: they "only build as a
whole series"), so do not build or bisect a commit in the middle.

**Pitfall:** functions shared between patches are mostly declared with a
local `extern` at the call site (`extern void wasi_rr_cpu_step(void);` in
`system/runstate.c`), so the compiler never compares them with the
definition. With the link flag `--allow-undefined`, a renamed function
becomes a host import that the host refuses ("unknown import"), and a
changed signature is only a wasm-ld warning; the call traps at run time.
Change such a function everywhere (in `qemu-src/`:
`git grep -n 'extern.*<name>'`).

### What 0008 switches off under WASI_THREADS

0003, 0004 and 0007 make QEMU work on one thread. The threaded flavor
(`make THREADS=1`, which defines `WASI_THREADS`) has real threads, so 0008
turns those shortcuts off, mostly by changing `#ifdef WASI_PLATFORM` into
`#if defined(WASI_PLATFORM) && !defined(WASI_THREADS)`:

| From | Where | Single-threaded build | Threaded build |
|---|---|---|---|
| 0003 | `default_mttcg_enabled()`, `tcg_set_thread()` (`accel/tcg/tcg-all.c`) | Multi-threaded TCG off, `thread=multi` refused | On, `thread=single` refused |
| 0003 | `cpu_handle_interrupt()` (`accel/tcg/cpu-exec.c`) | Returns to the main loop after each time slice | No time slices |
| 0003 | `do_run_on_cpu()` (`cpu-common.c`) | Runs the work at once | As upstream: queued for the vCPU's thread |
| 0003 | `qemu_wait_io_event_common()`, `qemu_init_vcpu()` (`system/cpus.c`) | Stops the vCPU directly; no wait for its creation | As upstream |
| 0004 | `main_loop_wait()` (`util/main-loop.c`) | Idle sleep of at most 10 ms holding the BQL (QEMU's global lock) | At most 30 ms, BQL released |
| 0007 | `qemu_main_loop_step()` (`system/runstate.c`) | Main loop, network, a vCPU slice, RCU callbacks, profile | Main loop and network; the `call_rcu` thread runs RCU callbacks |

Kept in both flavors: coroutines without stack switching (0001) and what
makes them work (`util/async.c`, `block/graph-lock.c`,
`util/thread-pool.c`), the flag event notifier (atomic since 0008), the
direct `aio_poll()` calls (0004), `io_prepare()` (0003) and the missing
QMP (0007). `wasi_rr_cpu_step()` is compiled but unused with threads.

## The patches

### 0001: wasi: build QEMU for wasm32-wasi

**Purpose.** Makes `configure` and meson (QEMU's build-file generator)
accept wasi-sdk's clang for `wasm32-wasip1`, and makes QEMU's portability
layer work without signals, processes, a real `mmap()`, sockets, pipes and
stack switching. 14 files, +697 -58.

| File | What changes |
|---|---|
| `configure` | `__wasi__` gives `host_os=wasi`; no `CPU_CFLAGS` for wasm32; "Unrecognized host OS" disabled with `if false &&` |
| `meson.build` | `wasm32` in `supported_cpus`; compiler-version and SafeStack probes skipped for wasm32; coroutine backends: `wasi` only on WASI, `fiber` first elsewhere |
| `meson_options.txt` | `fiber` and `wasi` added to `coroutine_backend` |
| `include/qemu/osdep.h` | `sigsetjmp()`/`siglongjmp()` become `setjmp()`/`longjmp()`; includes wasi-stubs' headers and `qemu/wasi-debug.h`; minimal `struct sigaction` and wait-status macros; `NDEBUG` check skipped |
| `include/qemu/wasi-debug.h` (new) | `WASI_LOG()`, `WASI_LOG_V()`, printed at `WASI_DEBUG=1`, `2`; empty on other hosts |
| `include/sysemu/os-posix.h` | Socket and netdb declarations the sysroot lacks; wasi-stubs implements them |
| `include/qemu/atomic.h` | `ATOMIC_REG_SIZE` build assertions commented out |
| `os-posix.c`, `util/osdep.c` | No signal handlers, rlimits, `initgroups()`, `mprotect()` |
| `util/memalign.c`, `util/mmap-alloc.c` | Aligned buffers and guest RAM from `aligned_alloc()` |
| `util/event_notifier-posix.c` | An event notifier is a flag with a fake fd number (1000 up) |
| `crypto/random-platform.c` | `getentropy()` |
| `util/coroutine-wasi.c` (new) | Coroutine backend without stack switching; `qemu_coroutine_wasi_run_outside()`, declared in `include/qemu/coroutine_int.h`, calls a function outside any coroutine |
| `block/file-posix.c` | New image files are created without lock bytes (WASI has no file locks; opened images already skip them, as `locking=auto` finds no OFD locks) |

**Design decisions.** *Macros, not functions, for `sigsetjmp()`:* with
`-mllvm -wasm-enable-sjlj` each `setjmp()` call becomes wasm exception
handling at the call site; a wrapper breaks the pairing, and
`cpu_loop_exit()`'s `siglongjmp()` is never caught (comment in
`osdep.h`). *No stack switching:* a coroutine (a function that pauses and
resumes later) needs a stack of its own, which wasm cannot switch to, so
entering one calls its function directly and a yield returns at once.
This works because I/O completes synchronously and 0004 runs the awaited
work inline; it is why QMP is disabled (0007). *`aligned_alloc()`:*
wasi-libc's emulated `mmap()` lacks `MAP_FIXED` and partial `munmap()`,
which `qemu_ram_mmap()` needs. *Atomic assertions dropped,* as in the
Emscripten patch: `qatomic_*()` is used on 64-bit fields, wider than
wasm32's registers. Why the meson probes are skipped and the unknown-OS
error is disabled is not recorded (the `if false &&` is probably unneeded
now; untested).

**Likely conflicts.** `meson.build`: both probes were re-indented into
`if cpu != 'wasm32'` blocks, so any upstream change inside them (a new
minimum compiler version) conflicts with the whole block; redo the change
instead. `include/qemu/osdep.h` changes in most releases, and any
upstream edit to the `qatomic_*()` macros meets the eight commented-out
lines of `include/qemu/atomic.h`. `include/sysemu/` has been renamed
upstream (to `include/system/`, as far as we know), so the `os-posix.h`
hunk moves. `util/coroutine-wasi.c` must still match
`include/qemu/coroutine_int.h`.

**How to tell it works.** It configures, compiles and starts. Without the
`sigsetjmp` macros QEMU traps at the first `cpu_loop_exit()`; without the
`mmap-alloc.c` change it cannot allocate guest RAM.

### 0002: tcg/wasm32: add a WebAssembly backend for WASI hosts

**Purpose.** TCG, QEMU's code generator, needs a backend for the host's
instruction set. This one turns each translation block (TB: guest
instructions translated as a unit) into a small wasm module that the host
compiles, with an interpreter for TBs not yet worth compiling. Based on
Kohei Tokunaga's Emscripten backend. 22 files, +8108 -18. Details:
[07-tcg-backend.md](07-tcg-backend.md).

| File | What changes |
|---|---|
| `tcg/wasm32/` (9 new files) | Code generator (`tcg-target.c.inc`), module assembly (`tcg-wasm32-helpers.c.inc`), interpreter (`wasi-interp.c.inc`) and its generated dispatcher (`wasi-interp-calls.c.inc`), headers |
| `tcg/wasm32.c`, `tcg/wasm32.h`, `tcg/wasi_jit.h` (new) | `tcg_qemu_tb_exec()`: tiers, JIT handles, time slices, profile; `struct wasmContext`; the host's `wasi_jit_*()` |
| `tcg/tcg.c` | Calls the backend at labels, TB start and end (`tcg_out_label_cb()`, `wasm_reset_state()`, `wasm_finalize_module()`); no prologue |
| `tcg/region.c` | Code buffer from `aligned_alloc()`, never executable; a flush calls `wasi_tb_cache_flush()` |
| `tcg/meson.build`, `util/cacheflush.c` | Builds `tcg/wasm32.c`; empty `flush_idcache_range()` |
| `include/tcg/tcg.h`, `include/exec/exec-all.h`, `disas/disas-host.c` | As for TCI (QEMU's interpreter backend): C `tcg_qemu_tb_exec()`, `GETPC()` from `tci_tb_ptr`, `print_insn_tci()` |
| `tcg/tcg-common.c` | `ARRAY_SIZE` replaced by a `sizeof` division (reason not recorded) |
| `accel/tcg/ldst_common.c.inc`, `util/coroutine-fiber.c` (new) | Emscripten only: `set_done_flag()` after each load/store helper, the `fiber` coroutine backend; neither is compiled in these builds |

**Design decisions** (commit message). WASI cannot compile wasm at run
time, so the host does (`wasi_jit_compile()`). A TB module imports QEMU's
memory and function table, the host adds the TB's function to that table,
and QEMU calls it like a function pointer; helpers are called with
`call_indirect`, so a helper's `longjmp()` unwinds straight through the
TB. Compiling costs about 1 ms per TB, so TBs are interpreted from their
own wasm and compiled in the background after `QEMU_JIT_HOT` runs (5000).
TCG registers are locals of the TB function, without resume points,
because on WASI a TB always runs from its start to an exit. Code is
position independent, so the host can reuse a module for identical bytes.

**Likely conflicts.** `tcg/tcg.c` and `tcg/region.c` change in most
releases, and upstream has reorganised the backend interface since 9.1
(as far as we know): expect to port `tcg/wasm32/` itself (checklist in
[07-tcg-backend.md](07-tcg-backend.md)). `accel/tcg/ldst_common.c.inc`
has a hunk in every helper, all Emscripten-only.

**How to tell it works.** The instruction test in its three modes
(`make test`, then with `QEMU_JIT_HOT=0` for compiled code only and
`QEMU_JIT_HOT=4000000000` for the interpreter only; see
[13-testing.md](13-testing.md)). Without the patch there is no TCG
backend for wasm32 and the build fails.

### 0003: accel/tcg: run the vCPU inline from the main loop on WASI

**Purpose.** QEMU runs each vCPU (virtual CPU) on its own thread, but in
the single-threaded build `pthread_create()` never runs the thread. So the
main loop runs the vCPU itself, one time slice per iteration, with
`wasi_rr_cpu_step()`. 7 files, +522 -2.

| File | What changes |
|---|---|
| `accel/tcg/tcg-accel-ops-rr.c` | `rr_start_vcpu_thread()` sets up inline and records the one thread as the vCPU thread; `wasi_rr_cpu_step()`, a copy of `rr_cpu_thread_fn()`'s loop body, which leaves `current_cpu` NULL for the main loop (else `vm_stop()` took the main loop for the vCPU and `stop` never stopped) |
| `accel/tcg/tcg-all.c` | Multi-threaded TCG (MTTCG) off, `thread=multi` refused |
| `accel/tcg/cpu-exec.c` | Time-slice exit in `cpu_handle_interrupt()`; translation time for the profile; `QEMU_DUMP_AT`, `QEMU_LOOP_DIAG`, TB history (all hosts) |
| `accel/tcg/cputlb.c` | `io_prepare()` lets I/O proceed instead of `cpu_io_recompile()`; unaligned accesses logged |
| `system/cpus.c` | vCPU stopped directly, not kicked, not waited for; `WASI_LOG_V()` |
| `cpu-common.c` | `do_run_on_cpu()` runs the work at once |
| `include/hw/core/tcg-cpu-ops.h` | No `G_NORETURN` on a function pointer (C11 forbids it) |

**Design decisions.** Recording the thread makes `qemu_cpu_is_self()`
true; otherwise `apic_update_irq()` kicks the vCPU forever. With nothing
to interrupt it, the vCPU leaves when its slice is over so that timers and
their interrupts get serviced. Slices are timed (`QEMU_TB_SLICE_MS`,
30 ms), not counted: each return costs a busy guest a timer tick of a
couple of thousand TBs, and with 4000-TB slices ticks took half its time.
`cpu_io_recompile()` (retranslate so that I/O happens at an exact
instruction boundary) never converged here, and without icount
(instruction counting) the boundary is not needed. Exit requests are
kept: dropping them lost the TLB flush queued after a memory-map change,
and SeaBIOS copied itself into read-only memory (comment in
`cpu_handle_interrupt()`).

**Likely conflicts.** `accel/tcg/cpu-exec.c` is busy upstream: hunks in
`cpu_tb_exec()`, `HELPER(lookup_tb_ptr)`, `cpu_handle_interrupt()` (with a
whitespace-only change to its signature) and `cpu_exec_loop()`; also
`io_prepare()` and `mmu_lookup()` in `cputlb.c`. Worse, because silent:
`wasi_rr_cpu_step()` and the WASI branch of `rr_start_vcpu_thread()` copy
upstream code; carry changes to `rr_cpu_thread_fn()` over by hand.

**How to tell it works.** SeaBIOS and the kernel boot and the guest clock
advances. Without the patch the vCPU never runs (black screen); without
the slice exit the guest waits forever for its first timer tick.

### 0004: util: main loop, coroutines, RCU and thread pool without threads

**Purpose.** Adapts what relies on other threads or on blocking: the main
loop, which must poll and sleep by itself (glib-wasi's main loop never
dispatches); bottom halves (BHs, callbacks the main loop runs later)
scheduled from coroutines; the block layer's graph lock; RCU callbacks
(deferred frees, run by a `call_rcu` thread); the thread pool. 7 files,
+167.

| File | What changes |
|---|---|
| `util/main-loop.c` | `main_loop_wait()` sleeps until the next timer (at most 10 ms, through `wasi_idle_wait` if set) when every vCPU is idle, then polls the main and iohandler AioContexts; no signal setup |
| `include/qemu/main-loop.h` | Declares `wasi_idle_wait` |
| `util/async.c` | A one-shot BH scheduled from a coroutine runs at once, outside the coroutine (as BHs run elsewhere; `bdrv_open()` asserts it); waking the running coroutine, or the one whose BH runs, does nothing |
| `block/graph-lock.c` | `bdrv_graph_wrlock()` in a coroutine takes the lock without draining |
| `util/rcu.c`, `include/qemu/rcu.h` | `rcu_run_callbacks_inline()` |
| `util/thread-pool.c` | `thread_pool_submit_co()` runs the work synchronously |
| `migration/file.c` | An incoming `file:` migration starts from a BH, not from a watch for "readable" that `g_poll()` never reports |
| `migration/ram.c` | No periodic schedule-and-yield while loading RAM: the yield returns at once, and the next schedule aborted (`Co-routine was already scheduled`) |

**Design decisions.** AioContexts (QEMU's event loops) normally run as
GLib sources; without the direct `aio_poll()` no main-context BH, timer or
fd handler runs, and disk probing and every guest disk read hang. A
coroutine's BH that runs at once has its result ready when the coroutine
"yields": the other half of 0001's coroutines. The graph-lock shortcut is
safe with one thread; a damaged qcow2 image now fails with an error, not
an assertion. Nothing queued with `call_rcu()` was ever freed (old TB hash
tables, FlatViews); 0007's `qemu_main_loop_step()` runs the callbacks
between iterations, when the only RCU reader is outside its critical
sections.

**Likely conflicts.** `main_loop_wait()` and `qemu_init_main_loop()`;
`aio_bh_schedule_oneshot_full()` and `aio_co_wake()`.
`rcu_run_callbacks_inline()` uses `rcu.c` internals (`try_dequeue()`,
`rcu_call_count`) and stops compiling if they change.

**How to tell it works.** The guest boots from disk, an idle guest costs
little host CPU, and with `QEMU_PROF_EVERY=10` the memory figure does not
keep growing across `[qemu-wasi] TB buffer flush` messages.

### 0005: ui: draw text consoles without pixman

**Purpose.** The monitor and the serial port are text consoles
(`-monitor vc -serial vc`; Ctrl+Alt+2, Ctrl+Alt+3), drawn with pixman, a
pixel library not available for WASI. This implements the part of its
API they use. 4 files, +217 -4.

| File | What changes |
|---|---|
| `include/ui/pixman-minimal.h` | Upstream's pixman stand-in gains the a8 format, solid fills, glyph blending, rectangle copies (32-bit destinations only) |
| `ui/meson.build` | Always builds `ui/console-vc.c`, never the stubs |
| `ui/qemu-pixman.c`, `include/ui/qemu-pixman.h` | Glyph and line-buffer functions also on WASI |

**Design decisions.** Rather than port pixman, the patch extends the
stand-in header upstream already uses when pixman is missing, with only
what `ui/console-vc.c` and `ui/qemu-pixman.c` call (rectangle copies are
for scrolling); the real console then replaces the stubs.

**Likely conflicts.** Few: the `console-vc.c` rule in `ui/meson.build`,
upstream edits to `pixman-minimal.h`.

**How to tell it works.** Ctrl+Alt+2 shows `(qemu)`, Ctrl+Alt+3 the serial
console. Without it the `vc` character device is never registered and
QEMU cannot create the consoles m-browser asks for.

### 0006: net: user-mode networking provided by the WASI host

**Purpose.** QEMU's usual `-nic user` network is libslirp, a TCP/IP stack
not ported to WASI. This `user` backend passes Ethernet frames to the
host, which emulates the same network: guest 10.0.2.15 by DHCP, gateway
10.0.2.2, DNS 10.0.2.3
([09-display-input-network.md](09-display-input-network.md)). 3 files,
+185.

| File | What changes |
|---|---|
| `net/wasi-user.c.inc` (new) | The backend: `host_net_*` imports, `wasi_net_poll()`, idle wait, option checks, `hostfwd` |
| `net/net.c` | Includes it; `net_init_wasi_user()` is the `user` driver without slirp |
| `include/net/net.h` | Declares `wasi_net_poll()` |

**Design decisions.** Included from `net.c` so that no meson file changes
("this cross build cannot be reconfigured unattended", says the file).
With no fds to watch, the main loop calls `wasi_net_poll()` every
iteration and sleeps in `host_net_wait()` (0004's `wasi_idle_wait`), so a
reply wakes it at once. `hostfwd` rules go to the host; options that
would change the network layout are rejected.

**Likely conflicts.** `net_client_init_fun[]` in `net/net.c`. Silent:
`wasi_user_unsupported_option()` lists the fields of `NetdevUserOptions`
(from `qapi/net.json`); an option upstream adds would be silently
ignored. Compare them when upgrading.

**How to tell it works.** In the guest, `ip addr` shows 10.0.2.15 and
`ping 10.0.2.2` answers; the monitor's `info network` says "provided by
the WASI host". Without it, `-nic user` fails: "network backend 'user' is
not compiled into this binary".

### 0007: system: run QEMU as a WASI reactor

**Purpose.** QEMU's `main()` runs the main loop until QEMU exits. This
makes QEMU a WASI *reactor*, a module whose functions the host calls again
and again: `wasi_qemu_init()`, `wasi_qemu_step()` (one main loop
iteration; -1 to go on, else the exit status), `wasi_qemu_jit_reset()`,
`wasi_qemu_quit()`; and `wasi_qemu_monitor()` with its buffer
(`wasi_qemu_buffer()`, `wasi_qemu_buffer_size()`), which runs a human
monitor command for the host. 5 files, +344 -3.

| File | What changes |
|---|---|
| `system/main.c` | The seven exports; `WASI_LOG()` in `main()` |
| `system/runstate.c` | `qemu_main_loop_step()`: main loop, network frames, vCPU slice, RCU callbacks, profile; `qemu_main_loop()` repeats it; `QEMU_STATUS_EVERY`; `WASI_LOG()` |
| `include/sysemu/sysemu.h` | Declares `qemu_main_loop_step()` |
| `system/vl.c` | Unreadable default config ignored; wasi-gfx display registered (`wasi_gfx_display_register_dcl()`, qemu-wasi's `display/`); `user` default network; 77 `WASI_LOG()` calls |
| `monitor/monitor.c` | No QMP dispatcher coroutine |

**Design decisions.** Between calls no wasm frames are live, so the host
can move QEMU into a fresh wasmtime Store (the container that owns
instances) by copying its memory, the only way to free compiled TB
modules; it then calls `wasi_qemu_jit_reset()`. The reactor link flag
comes from `scripts/fix_build_ninja.py`. The QMP dispatcher waits by
yielding, which these coroutines cannot: it would spin and assert; the HMP
(human) monitor works. wasi-libc leaves `errno` at 0 for paths outside the
preopened directories, so the config check tests the error, not the
return value.

**Likely conflicts.** `system/vl.c`: 19 of its 23 hunks only add
`WASI_LOG()` lines, and one only re-indents a `warn_report()`; they are
tracing, so drop them on conflict. `qemu_main_loop_step()` repeats
`qemu_main_loop()`'s body: compare by hand. `include/sysemu/` has moved
upstream (see 0001).

**How to tell it works.** The module exports the seven functions
([reference/host-interface.md](reference/host-interface.md) shows how to
list them), m-browser starts it, and closing its window ends QEMU with
exit status 0. Without the patch QEMU could only run as a command
(m-browser still calls `_start` if the step export is missing), whose
`main()` never returns, so compiled TB modules would never be freed.

### 0008: wasi: run each vCPU on a thread of its own (WASI_THREADS)

**Purpose.** The threaded flavor (`make THREADS=1`: `wasm32-wasip1-threads`,
macro `WASI_THREADS`) runs QEMU's multi-threaded TCG, one thread per vCPU
beside the main loop's. It switches off the single-threaded shortcuts
([above](#what-0008-switches-off-under-wasi_threads)) and adds what
threads on a wasm host need. 21 files, +451 -79. Details:
[08-threads.md](08-threads.md).

| File | What changes |
|---|---|
| `accel/tcg/tcg-accel-ops-mttcg.c` | The vCPU thread calls the `host_vcpu_run` import; the host calls back `wasi_vcpu_step()`, a copy of `mttcg_cpu_thread_fn()`'s loop body |
| `tcg/wasm32.c` | `wasi_vcpu_enter` (inline assembly: stack pointer, TLS base); a handle slot per vCPU, per-thread handle rings; `wasi_jit_drop()` no longer called (both flavors) |
| `tcg/wasm32.h`; in `tcg/wasm32/`: `tcg-target.h`, `tcg-target.c.inc`, `tcg-wasm32-helpers.c.inc`, `wasi-interp.c.inc`, `wasi_platform.h` | `WASI_SLOTS`; shared memory import; `TCG_TARGET_DEFAULT_MO`; `atomic.fence`; interpreter lock and per-thread decoder; a blank line in `wasi_platform.h` |
| `util/main-loop.c`, `util/async.c`, `include/qemu/main-loop.h` | Main loop sleeps with the BQL released; `aio_notify()` wakes it (`host_main_wake`) |
| `util/event_notifier-posix.c` | Atomic flag |
| `hw/intc/apic.c` | APIC timer on a `QEMU_TICK_SLACK_MS` grid (30 ms) |
| `meson.build`, `util/qemu-thread-posix.c` | `CONFIG_ATOMIC64` for wasm32 with atomics; 16 MiB thread stacks |
| `system/main.c` | On exit, unplug the vCPUs so their threads end (they do not yet: [known-issues.md](known-issues.md)) |
| `accel/tcg/cpu-exec.c`, `tcg-all.c`, `cpu-common.c`, `system/cpus.c`, `system/runstate.c` | The switch-offs |

**Design decisions** (commit message). The host runs each vCPU's TBs in a
Store of its own and replaces it now and then, which needs the thread to
have no frames in it; so the thread calls `host_vcpu_run`, and the host
calls `wasi_vcpu_step()` repeatedly through `wasi_vcpu_enter()`. Each
vCPU's instance has its own function table, hence one JIT handle per vCPU
per TB. `TCG_TARGET_DEFAULT_MO`: TB code keeps accesses in program order
and x86-64 hosts keep x86's order, so x86 guests need barriers only where
they ask (not true on other hosts). The main loop sleeps in the host, out
of reach of event notifiers, so `host_main_wake` wakes it. At 1000 timer
ticks a second, ticks took half of a busy guest's time, hence the grid
(the single-threaded build gets late ticks from its slices). meson's
`CONFIG_ATOMIC64` test fails on wasm-ld (`--start-group`), and without it
every 8-byte access missing the TLB stopped all vCPUs.

**Note:** the commit message says "The single-threaded build is
unchanged", but the handle bookkeeping in `tcg/wasm32.c` changed for both.

**Likely conflicts.** Where 0003, 0004 or 0007 conflict, 0008 conflicts
again, as it edits their `#ifdef` lines. `wasi_vcpu_step()` copies
upstream code (compare by hand); also `apic_timer_update()`, the
`CONFIG_ATOMIC64` test and `qemu_thread_create()`. The inline assembly
depends on wasi-sdk's LLVM (its wasm assembler syntax and the
`__stack_pointer` and `__tls_base` globals) rather than on QEMU.

**How to tell it works.** `make THREADS=1` and `make THREADS=1 test`;
m-browser logs `Multi-threaded QEMU: one thread per vCPU`, the monitor's
`info cpus` lists every vCPU, and the desktop responds while Chromium
starts. The single-threaded flavor behaves as before.

## Which patches change a file

From `git show --stat` of each commit: 65 files, +10612 -85. "(new)"
marks a file the series creates (in the first patch listed).

| File | Patches |
|---|---|
| `accel/tcg/cpu-exec.c`, `accel/tcg/tcg-all.c` | 0003, 0008 |
| `accel/tcg/cputlb.c`, `accel/tcg/tcg-accel-ops-rr.c` | 0003 |
| `accel/tcg/ldst_common.c.inc` | 0002 |
| `accel/tcg/tcg-accel-ops-mttcg.c` | 0008 |
| `block/graph-lock.c` | 0004 |
| `block/file-posix.c` | 0001 |
| `configure`, `meson_options.txt`, `os-posix.c` | 0001 |
| `cpu-common.c` | 0003, 0008 |
| `crypto/random-platform.c` | 0001 |
| `disas/disas-host.c` | 0002 |
| `hw/intc/apic.c` | 0008 |
| `include/exec/exec-all.h`, `include/tcg/tcg.h` | 0002 |
| `include/hw/core/tcg-cpu-ops.h` | 0003 |
| `include/net/net.h`, `net/net.c`, `net/wasi-user.c.inc` (new) | 0006 |
| `include/qemu/atomic.h`, `include/qemu/osdep.h`, `include/qemu/wasi-debug.h` (new) | 0001 |
| `include/qemu/coroutine_int.h` | 0001 |
| `include/qemu/main-loop.h` | 0004, 0008 |
| `include/qemu/rcu.h` | 0004 |
| `include/sysemu/os-posix.h` | 0001 |
| `include/sysemu/sysemu.h` | 0007 |
| `include/ui/pixman-minimal.h`, `include/ui/qemu-pixman.h` | 0005 |
| `meson.build` | 0001, 0008 |
| `migration/file.c`, `migration/ram.c` | 0004 |
| `monitor/monitor.c` | 0007 |
| `system/cpus.c` | 0003, 0008 |
| `system/main.c`, `system/runstate.c` | 0007, 0008 |
| `system/vl.c` | 0007 |
| `tcg/meson.build`, `tcg/region.c`, `tcg/tcg-common.c`, `tcg/tcg.c` | 0002 |
| `tcg/wasi_jit.h`; in `tcg/wasm32/`: `tcg-target-con-set.h`, `tcg-target-con-str.h`, `tcg-target-reg-bits.h`, `wasi-interp-calls.c.inc` (all new; the last is generated by `scripts/gen_interp_calls.py`) | 0002 |
| `tcg/wasm32.c`, `tcg/wasm32.h`; in `tcg/wasm32/`: `tcg-target.c.inc`, `tcg-target.h`, `tcg-wasm32-helpers.c.inc`, `wasi-interp.c.inc`, `wasi_platform.h` (all new in 0002) | 0002, 0008 |
| `ui/meson.build`, `ui/qemu-pixman.c` | 0005 |
| `util/async.c`, `util/main-loop.c` | 0004, 0008 |
| `util/cacheflush.c`, `util/coroutine-fiber.c` (new) | 0002 |
| `util/coroutine-wasi.c` (new), `util/memalign.c`, `util/mmap-alloc.c`, `util/osdep.c` | 0001 |
| `util/event_notifier-posix.c` | 0001, 0008 |
| `util/qemu-thread-posix.c` | 0008 |
| `util/rcu.c`, `util/thread-pool.c` | 0004 |

## Changes that also affect non-WASI builds

Most changes sit inside `#ifdef WASI_PLATFORM`, `__wasi__`,
`WASI_THREADS` or `EMSCRIPTEN`. These do not, which matters for the
native QEMU that `make native` builds from the same tree for the tests:

- **`include/qemu/atomic.h`** (0001): the `ATOMIC_REG_SIZE` build
  assertions are commented out on every host.
- **Coroutine backend** (0001): `meson.build` lists `fiber`, the
  Emscripten backend, first for every non-WASI host, and `auto` takes the
  first; it needs `<emscripten/fiber.h>`. Native builds must be
  configured with `--with-coroutine=ucontext` (or `sigaltstack`), as the
  Makefile's `native` target is.
- **`configure`** (0001): `if false && test "$host_os" = "bogus"` turns
  off the "Unrecognized host OS" error on every host.
- **Debugging hooks** (0003): `QEMU_DUMP_AT`, `QEMU_LOOP_DIAG` and the TB
  history in `accel/tcg/cpu-exec.c` exist on every host, on purpose, to
  compare native and wasm runs. Unset, they cost a test of a static
  variable per TB.
- **`WASI_LOG()`, `WASI_LOG_V()`** calls in `system/vl.c`,
  `system/runstate.c`, `system/main.c`, `system/cpus.c` and
  `util/main-loop.c` are compiled everywhere and expand to nothing.
- **`ui/meson.build`** (0005): `ui/console-vc.c` is built even without
  pixman, but what it calls is declared only with pixman or on WASI, so a
  native build without pixman probably fails (from the code; not tried).
- **Harmless:** `ARRAY_SIZE`'s array check lost in `tcg/tcg-common.c`
  (0002); whitespace changes in `cpu-exec.c` (0003) and `system/vl.c`
  (0007); the `CONFIG_ATOMIC64` override (0008) needs `__wasm_atomics__`.
