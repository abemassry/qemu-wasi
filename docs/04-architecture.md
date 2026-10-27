# Architecture: the QEMU side

**What this covers.** The map of the QEMU side: its layers, which patch or
directory implements each, how control passes between QEMU and the host,
where memory and threads go, who does what, the main design decisions and
the limits that follow, and which chapter to read for a given task. It
names files and functions and leaves the details to later chapters. It
uses the vocabulary of [02-concepts.md](02-concepts.md) (guest, host, TCG,
TB, vCPU, reactor, Store; single terms are in the
[glossary](reference/glossary.md)). The host's half is described in the
m-browser repository's docs/04-architecture.md. Paths are in `qemu-src/`
unless they start with a top-level directory of this repository.

## Summary

qemu-wasi is QEMU 9.1's `qemu-system-x86_64`, an emulator of an x86-64 PC,
compiled to a WebAssembly core module for WASI preview 1. Almost all of it
is upstream QEMU: the device models, the x86 front end of TCG (QEMU's
translator of guest code), the softmmu, the main loop, the block layer,
the monitor and the consoles. Eight patches and three directories outside
QEMU make it run on WASI: a TCG backend that turns each translation block
into a small wasm module, interpreted while cold and compiled by the host
once hot; a vCPU run in time slices by the main loop or, in the threaded
flavor, on a thread of its own; coroutines, RCU and a thread pool without
threads; a minimal GLib and POSIX stand-ins; and a display and a network
that go through the host. The module is a *reactor*: m-browser calls
`wasi_qemu_init()` once and then `wasi_qemu_step()` again and again, and
within those calls QEMU uses imports to have code compiled, show frames,
fetch input, move network frames and start threads.

## Layers

```text
+--------------------------------------------------------------------+
| Guest software: SeaBIOS, Linux kernel, Xorg, Fluxbox, Chromium     |
+--------------------------------------------------------------------+
          |  x86-64 instructions, device register accesses
          v
+--------------------------------------------------------------------+
| Emulated PC: machine "pc", device models in hw/ (upstream)         |
|   i440fx + PIIX chipset     VGA ("std")      e1000 network card    |
|   IDE disk on the PIIX      USB: qemu-xhci + usb-tablet            |
|   PS/2 keyboard and mouse   16550A serial port                     |
|   APIC, PIT, RTC: interrupts and timers                            |
+--------------------------------------------------------------------+
| QEMU core (upstream, with hooks from the patches)                  |
|   TCG front end target/i386/tcg/ -> TCG core tcg/                  |
|     -> wasm backend tcg/wasm32/ + tcg/wasm32.c (0002): modules,    |
|        interpreter tier, JIT handles, time slices                  |
|   vCPU loop and softmmu: accel/tcg/   main loop: util/ (timers,    |
|   bottom halves, AioContexts)   block layer + qcow2: block/        |
|   chardev/, HMP monitor: monitor/   consoles: ui/ (0005)           |
+--------------------------------------------------------------------+
| WASI port glue                                                     |
|   0001 build, portability, coroutines   0003 vCPU inline           |
|   0004 main loop, RCU, thread pool      0006 net/wasi-user.c.inc   |
|   0007 reactor exports                  0008 vCPU threads          |
|   glib-wasi/ (GLib)  wasi-stubs/ (POSIX)  display/ (screen, input) |
+--------------------------------------------------------------------+
          |  imports                            ^  exports
          |  env: wasi_jit_*, host_gfx_*,       |  _initialize,
          |    host_net_*, host_main_wake,      |  wasi_qemu_init, _step,
          |    host_vcpu_run                    |  _quit, _jit_reset,
          |  wasi: thread-spawn                 |  wasi_vcpu_enter,
          v  wasi_snapshot_preview1             |  wasi_thread_start
+--------------------------------------------------------------------+
| m-browser (Rust, wasmtime): TB compiler and cache, window, input,  |
| virtual network, threads and Stores, WASI files, clocks, random    |
+--------------------------------------------------------------------+
```

Everything above the imports is one wasm module, plus the small TB modules
it generates, which share its memory and function table. The only ways out
of the module are imports; `host_main_wake`, `host_vcpu_run`,
`wasi.thread-spawn`, `wasi_vcpu_enter` and `wasi_thread_start` exist only
in the threaded flavor. A guest instruction, a keystroke and a network
packet are followed through these layers in
[02-concepts.md](02-concepts.md#7-how-the-pieces-cooperate-at-run-time).

## Components and where they live

| Component | Where | Chapter |
|---|---|---|
| Device models | `hw/`, upstream; `hw/intc/apic.c` tick slack (0008) | [02](02-concepts.md), [12](12-adding-features.md) |
| TCG front end: x86 to TCG ops | `target/i386/tcg/`, upstream | [07](07-tcg-backend.md) |
| TCG core, vCPU loop, softmmu | `tcg/tcg.c`, `tcg/region.c`, `accel/tcg/` (`cpu-exec.c`, `cputlb.c`, `translate-all.c`); hooks from 0002, 0003, 0008 | [07](07-tcg-backend.md) |
| Wasm backend and interpreter tier | 0002: `tcg/wasm32/`, `tcg/wasm32.c`, `tcg/wasm32.h`, `tcg/wasi_jit.h` | [07](07-tcg-backend.md) |
| Build for wasm32, portability layer, coroutines without stack switching | 0001: `configure`, `meson.build`, `include/qemu/osdep.h`, `util/coroutine-wasi.c`, `util/mmap-alloc.c`; `Makefile`, `scripts/` | [05](05-repository-tour.md), [06](06-patch-series.md) |
| vCPU run inline from the main loop | 0003: `wasi_rr_cpu_step()` in `accel/tcg/tcg-accel-ops-rr.c`, slice exit in `cpu_handle_interrupt()` (`accel/tcg/cpu-exec.c`) | [06](06-patch-series.md) |
| Main loop, bottom halves, RCU, thread pool without threads | 0004: `util/main-loop.c`, `util/async.c`, `util/rcu.c`, `util/thread-pool.c`, `block/graph-lock.c` | [06](06-patch-series.md) |
| Text consoles (monitor, serial) | 0005: `include/ui/pixman-minimal.h`, `ui/qemu-pixman.c`, `ui/meson.build` (always builds upstream's `ui/console-vc.c`) | [09](09-display-input-network.md) |
| User network | 0006: `net/wasi-user.c.inc`, included by `net/net.c` | [09](09-display-input-network.md) |
| Reactor exports, one main-loop step | 0007: `system/main.c`, `qemu_main_loop_step()` in `system/runstate.c`; display registration in `system/vl.c` | [06](06-patch-series.md), [host interface](reference/host-interface.md) |
| One thread per vCPU (MTTCG) | 0008: `accel/tcg/tcg-accel-ops-mttcg.c`, `wasi_vcpu_enter` in `tcg/wasm32.c`, `#ifdef WASI_THREADS` | [08](08-threads.md) |
| Display and input backend | `display/` (`wasi_gfx_dcl.c`, `wasi_gfx_display.c`) | [09](09-display-input-network.md) |
| Minimal GLib | `glib-wasi/` | [05](05-repository-tour.md) |
| POSIX stand-ins | `wasi-stubs/` | [05](05-repository-tour.md) |
| Instruction test | `tests/x86-insn-diff/` | [13](13-testing.md) |
| Timing scripts | `tools/perf/` | [13](13-testing.md), [15](15-performance.md) |
| Diagnostics (`QEMU_STATUS_EVERY`, `QEMU_PROF_EVERY`, `WASI_DEBUG`, ...) | `system/runstate.c`, `tcg/wasm32.c`, `accel/tcg/cpu-exec.c`, `include/qemu/wasi-debug.h` | [14](14-debugging.md), [variables](reference/environment-variables.md) |

## Control flow

### Start-up and the reactor model

Native QEMU's `main()` calls `qemu_init()` and then `qemu_main_loop()`,
which returns only when QEMU exits. This build is linked as a WASI reactor
(`-mexec-model=reactor`), and the host drives it from m-browser's
`run_qemu_reactor()`:

```text
m-browser                                  QEMU module
instantiate the module, with imports
_initialize()                    ------->  C constructors
wasi_qemu_init()                 ------->  qemu_init(): options, machine,
                                           devices, disk, TB buffer,
                                           vCPUs, display (host_gfx_init)
loop:
  stop requested? wasi_qemu_quit() ----->  shutdown request
  wasi_qemu_step()               ------->  qemu_main_loop_step()
                                 <-------  -1: go on; >= 0: exit status
  single-threaded build, too many TB
  instances: copy the memory into a
  fresh Store, wasi_qemu_jit_reset() --->  forget all TB handles
```

Why a reactor: wasmtime keeps every TB module instance until its Store is
dropped, and a Store can only be dropped while none of its code runs.
Between two steps no QEMU frame is live and all of QEMU's state is in its
linear memory, so the host can instantiate QEMU in a new Store, copy the
memory over (about half a second), drop the old Store with all its TB
instances and call `wasi_qemu_jit_reset()`. m-browser does so when TB
instances pile up (100,000 by default) or memory mappings run short (the
m-browser repository's docs/06-qemu-runner.md). A step boundary is also a
clean place to stop: the host calls `wasi_qemu_quit()` and keeps stepping
until QEMU returns its exit status, with the disk image flushed and closed
(m-browser waits up to 10 s).

**Note:** frames, input, network frames and compile requests do not wait
for the step boundary: QEMU calls those imports during a step. Between
steps m-browser only checks its stop flag and decides on a Store move.

### One step in the single-threaded build

```text
wasi_qemu_step()                           system/main.c
  qemu_main_loop_step(&status)             system/runstate.c
    main_loop_should_exit()                shutdown, reset, ... requests
    main_loop_wait(!all_cpu_threads_idle())  util/main-loop.c
      every vCPU idle: sleep until the next timer, at most 10 ms,
        in host_net_wait() (a network frame ends it) or nanosleep()
      aio_poll() of the iohandler and main AioContexts: bottom
        halves, fd handlers, the AioContexts' timers
      qemu_clock_run_all_timers(): device timers, and the 30 ms
        display refresh, which polls input and sends frames
    wasi_net_poll()                        net/wasi-user.c.inc
      host_net_recv() until empty; each frame to the e1000
    wasi_rr_cpu_step()                     accel/tcg/tcg-accel-ops-rr.c
      wasi_slice_begin(), tcg_cpu_exec(): guest code runs until the
        30 ms slice is over, the guest halts, or QEMU stops the vCPU
    rcu_run_callbacks_inline()             util/rcu.c, deferred frees
  -1, or qemu_cleanup() and the exit status
```

The main loop and the vCPU take turns on one thread, and every lock is a
no-op. While guest code runs, timers, display and input wait. To end a
slice, every 1024 TBs `tcg_qemu_tb_exec()` makes the next TB exit to
`cpu_exec()`, and `cpu_handle_interrupt()` returns to the main loop once
`wasi_slice_over()` says the slice (`QEMU_TB_SLICE_MS`, 30 ms) is over.
An idle guest executes `hlt`, which ends the slice at once; the next
`main_loop_wait()` then sleeps.

### One step in the threaded build

```text
main thread                             vCPU thread, one per vCPU
(m-browser's QEMU thread)               (qemu-2, qemu-3, ...)
wasi_qemu_step()                        mttcg_cpu_thread_fn()
  qemu_main_loop_step()                   wasi_vcpu_thread_run()
    main_loop_wait(false)                   host_vcpu_run(index,
      sleep <= 30 ms, BQL released,             stack_top, tls_base)
      ended early by host_main_wake      m-browser, same OS thread:
      aio_poll(), timers, display        new Store + vCPU instance
    wasi_net_poll()                      loop:
  -1                                       wasi_vcpu_enter()
                                             wasi_vcpu_step(flags)
call_rcu thread (qemu-1):                      tcg_cpu_exec()
  RCU callbacks                                qemu_wait_io_event()
                                           now and then: new Store,
                                           flag WASI_VCPU_JIT_RESET
```

The vCPU threads are created in `wasi_qemu_init()`: wasi-libc's
`pthread_create()` calls `wasi.thread-spawn`, and the host starts an OS
thread with a new instance of the module and calls its
`wasi_thread_start`. Instead of looping in `mttcg_cpu_thread_fn()` as
upstream does, a vCPU thread hands its loop to the host (`host_vcpu_run`),
which runs one iteration at a time (`wasi_vcpu_step()` in
`accel/tcg/tcg-accel-ops-mttcg.c`) in an instance it can replace in
between, freeing that vCPU's TB instances. The main loop's Store runs no
guest code and is never replaced
([08-threads.md](08-threads.md#how-a-vcpu-thread-runs)).

### The life of a translation block

```text
cpu_exec() needs the TB for the guest pc     accel/tcg/cpu-exec.c
  tb_lookup() misses:
    tb_gen_code()                            accel/tcg/translate-all.c
      target/i386 front end: x86 -> TCG ops
      tcg_gen_code() with tcg/wasm32: ops -> wasm function body
      wasm_finalize_module(): module header, helper list, per-TB
        slots (one handle per vCPU, run count, decoded program)
      -> bytes in the TB code buffer, position independent
  cpu_tb_exec() -> tcg_qemu_tb_exec()        tcg/wasm32.c
    loop over the chain:
      handle in this vCPU's slot: call it (call_indirect)
      else interpret it (interp_decode() once, interp_exec())
        run 50 (QEMU_JIT_WARM): wasi_jit_load_cached(), in case the
          host still has this exact module compiled
        run 5000 (QEMU_JIT_HOT): wasi_jit_load_async(); the host
          compiles on a qemu-jit thread; asked again every 16 runs
          until it returns a handle
      next TB in ctx->tb_ptr (goto_tb, goto_ptr): loop again;
        0 (exit_tb, or no next TB): return to cpu_exec()
code buffer full: tb_flush() -> tcg_region_reset_all()
  -> wasi_tb_cache_flush(): all translations and handles go
```

A *handle* is the index where the host put the TB module's `start`
function in the calling instance's function table, so QEMU calls a
compiled TB as a C function pointer, wasm to wasm. TB modules import
QEMU's memory and function table, call helpers through the table and have
no state of their own. A wasm function cannot jump into another, so a TB
chains by returning the next TB's address to the loop in
`tcg_qemu_tb_exec()`, which calls it without going back to `cpu_exec()`.
A TB the interpreter cannot decode is compiled at once
(`wasi_jit_compile()`), and one the JIT rejects stays interpreted
([07-tcg-backend.md](07-tcg-backend.md#running-tbs-the-tiers)).

## Memory

A wasm32 module has one linear memory with 32-bit addresses, so at most
4 GiB, and all of QEMU lives in it:

```text
0                                                              4 GiB
+--------+--------+--------------------------------------+ - - - - +
| static | main   | heap: malloc(), aligned_alloc()      | not yet |
| data   | stack  |   guest RAM, 1 GiB (-m 1024)         | grown   |
| ~8 MB  | 8 MiB  |   TB code buffer, 256 MiB (tb-size)  |         |
|        |        |   VGA memory, device state, TB hash  |         |
|        |        |   table, decoded TB programs, GLib   |         |
|        |        |   threaded build: per thread a       |         |
|        |        |   16 MiB stack and a TLS block       |         |
+--------+--------+--------------------------------------+ - - - - +
```

- **Guest RAM and the code buffer are heap blocks** from `aligned_alloc()`
  (`qemu_ram_mmap()` in `util/mmap-alloc.c`, `alloc_code_gen_buffer()` in
  `tcg/region.c`): wasi-libc's emulated `mmap()` lacks what QEMU needs.
  Their order in the diagram is not significant. Guest memory accesses
  reach the RAM block through the softmmu TLB, or helpers on a miss.
- **The code buffer holds wasm bytes**, never executed in place: compiled
  TB code and instances live in wasmtime, outside linear memory, and that
  is what a Store move frees.
- **The host reads and writes this memory directly** (TB bytes, frames,
  network frames, WASI buffers): pointers in the host interface are
  offsets into it.
- **Single-threaded build:** QEMU defines and exports the memory: 256 pages
  at start (16 MiB: static data and the 8 MiB stack,
  `-z stack-size=8388608`), growing as `malloc()` needs more. **Threaded
  build:** the host creates it, shared, 256 to 65536 pages, and every
  instance (one per QEMU thread, plus one per vCPU) imports it as
  `env.memory`. TB modules import it as `env.buffer` in both flavors.

## Threads

| | Single-threaded (`make`) | Threaded (`make THREADS=1`) |
|---|---|---|
| Main loop | m-browser's QEMU thread, inside `wasi_qemu_step()` | The same; sleeps with the BQL released |
| vCPUs | One, run in 30 ms slices by `wasi_rr_cpu_step()` on the same thread | One thread per vCPU (`-smp N`, at most 8), MTTCG, each run by the host through `host_vcpu_run` |
| RCU callbacks | `rcu_run_callbacks_inline()`, every step | The `call_rcu` thread |
| Thread pool (disk I/O) | Runs the work at once, on the calling thread | The same |
| Coroutines | Entered by a plain call; a yield returns at once | The same |
| Other threads QEMU starts | `pthread_create()` succeeds but never runs them (`wasi-stubs/pthread_stubs.c`) | Real; a normal run creates no others |
| Locks | No-ops | wasi-libc's pthreads; glib-wasi's threads variant |
| Host-only threads | `qemu-jit-N` (TB compilation), `qemu-net` (network), m-browser's UI thread | The same |

**Pitfall:** `#ifdef WASI_PLATFORM` covers both flavors. A shortcut that
is only safe with one thread needs `!defined(WASI_THREADS)` too; see
[the list](06-patch-series.md#what-0008-switches-off-under-wasi_threads)
of single-threaded shortcuts that patch 0008 switches off.

## I/O paths at a glance

| Path | Route | Chapter |
|---|---|---|
| Disk | IDE controller model (`-drive` without `if=` means IDE on a `pc`) -> block layer -> qcow2 driver (`block/qcow2.c`) -> `file` driver (`block/file-posix.c`) -> `pread()`/`pwrite()` -> WASI `fd_pread`/`fd_pwrite` on `/data/m.qcow2`, a file in the directory m-browser preopens as `/data`. Synchronous, on the thread that emulates the controller. Firmware comes from `/bios` (`-L /bios`). m-browser locks the image, as QEMU cannot here | [02](02-concepts.md#coroutines), [06](06-patch-series.md) |
| Display | Guest draws into VGA memory -> refresh timer every 30 ms -> VGA reports changed areas -> `display/` copies and scales them -> `host_gfx_update_framebuffer()` (BGRA) -> m-browser's window. The monitor and the serial port are text consoles (`-monitor vc -serial vc`, Ctrl+Alt+2, Ctrl+Alt+3) | [09](09-display-input-network.md) |
| Input | m-browser queues window events -> `host_gfx_poll_events()` at each refresh -> QEMU's input layer -> PS/2 keyboard, USB tablet (absolute pointer) | [09](09-display-input-network.md) |
| Network | e1000 -> `net/wasi-user.c.inc` -> `host_net_send()`; back: `wasi_net_poll()` each iteration -> `host_net_recv()` -> e1000. m-browser emulates the 10.0.2.0/24 network and relays through host sockets | [09](09-display-input-network.md) |
| Clocks, timers | QEMU's clocks read WASI `clock_time_get`; the guest's PIT, RTC and APIC timers are QEMU timers run by the main loop: late by up to a slice (single-threaded) or rounded to a 30 ms grid (`QEMU_TICK_SLACK_MS`, threaded) | [08](08-threads.md#the-guests-timer-tick) |

## Who does what

| Concern | QEMU (this repository) | m-browser |
|---|---|---|
| Guest code | Translates x86 into wasm modules, interprets cold TBs, decides when a TB is hot | Compiles modules with Cranelift on `qemu-jit-N` threads, caches them by their bytes, instantiates them, puts their entry into QEMU's table |
| Freeing compiled code | Forgets its handles (`wasi_qemu_jit_reset()`, `WASI_VCPU_JIT_RESET`) | Decides when, and moves QEMU or a vCPU to a fresh Store |
| Scheduling | One main-loop iteration per step; vCPU slices; idle sleep | Calls the step in a loop; runs vCPU iterations on their threads |
| Screen | VGA model, consoles, change tracking, scaling, BGRA frames | Shows frames in a window; headless, writes them to PNG files |
| Input | Polls events, feeds QEMU's input layer | Captures window events, queues them |
| Network | e1000 model, Ethernet frames | DHCP, DNS, NAT of TCP, UDP and ping through host sockets, on its `qemu-net` thread |
| Disk | Block layer, qcow2 format | Preopened directories, lock on the image |
| Threads | pthreads, vCPU loop bodies | OS threads and instances for `thread-spawn`, a Store per vCPU |
| Configuration | Parses the command line | Builds it (`-m 1024`, `tb-size=256`, `-smp`, devices) and passes only `QEMU_*` and `WASI_*` variables |

## Key design decisions

**A wasm TCG backend with an interpreter tier, not TCI.** TCI, QEMU's
portable backend, interprets TCG ops on every run and never produces
machine code. Generated wasm, compiled by the host, runs as machine code,
but compiling a TB costs 1 to 1.5 ms, about a thousand runs of it, and
most TBs run only a few times. So TBs start in an interpreter of the same
wasm (`tcg/wasm32/wasi-interp.c.inc`) and are compiled in the background
after 5000 runs: one translation serves both tiers.

**TCG registers as wasm locals.** The host compiler can keep locals in
machine registers; globals live in memory. It works because on WASI a TB
always runs from its start to an exit. Together with dropping the resume
points of the Emscripten backend this one descends from, it made guest
code about 22% faster ([07](07-tcg-backend.md#how-it-came-to-be)).

**Position-independent TB code.** A TB finds itself through
`ctx->tb_ptr`, never through an absolute address, so the same guest code
always gives the same bytes. The host caches modules by their bytes: after
a flush or a Store move, hot code is reloaded (about 50 µs) instead of
recompiled, and identical TBs share one instance.

**A reactor with Store migration.** wasmtime frees TB instances only with
their Store, and each also costs memory mappings, which Linux limits per
process. A reactor has no frame live between steps, so its Store can be
replaced, and it can be stopped cleanly
([above](#start-up-and-the-reactor-model)).

**Time slices and tick slack.** The guest's Linux has a 1000 Hz timer
tick, and each tick costs a busy guest a couple of thousand TBs of
interrupt handling: about half of all its work at that rate. The
single-threaded build runs the vCPU in 30 ms slices and services timers
between them; the threaded build has no slices and rounds the APIC timer
to a 30 ms grid (`apic_timer_slack()`). Either way a busy guest gets
about 33 ticks a second instead of 1000, each up to 30 ms late.

**MTTCG on wasi-threads.** QEMU's multi-threaded TCG already runs one
vCPU per host thread, and wasi-threads is how a WASI preview 1 module gets
threads. The gain is responsiveness more than boot time: X and the window
manager keep a vCPU while Chromium is busy ([08](08-threads.md#results)).
The host runs each vCPU's loop an iteration at a time, to replace its Store.

**A minimal GLib.** QEMU requires GLib, which does not build for WASI.
`glib-wasi/` implements the subset QEMU uses and stubs GLib's main loop,
which is why QEMU polls its own AioContexts (patch 0004).

**The display backend outside `qemu-src/`.** It is this project's own
code, talking only to host imports; `scripts/fix_build_ninja.py` compiles
it into QEMU, so changing it needs no patch and no meson change. The
reason is not recorded; the code began as a client of wasi-gfx, the
host's graphics interface.

**User networking in the host instead of libslirp.** libslirp, QEMU's
usual `-nic user`, relays guest traffic through host sockets, and WASI
preview 1 cannot open network connections. So QEMU passes raw Ethernet
frames, and m-browser, which has real sockets, emulates the same network
on a thread of its own, servicing sockets even while QEMU computes.

## Limits

- **4 GiB for everything.** Guest RAM, the code buffer, QEMU's heap and
  the thread stacks share wasm32's 4 GiB and must stay well below it.
- **At most 8 vCPUs** in the threaded build (`WASI_JIT_SLOTS` in
  `tcg/wasm32.h`; m-browser clamps `QEMU_SMP` to 1..8), one without.
- **x86-64 hosts for the threaded build.** TB memory accesses stay in
  program order and x86-64 keeps x86's memory order, so the backend emits
  fences only where the guest asks; elsewhere a guest with several vCPUs
  could see its accesses reordered.
- **No KVM**: a wasm module cannot run guest code directly, so every guest
  instruction goes through TCG.
- **No QMP, synchronous disk I/O**: coroutines cannot really wait. More in
  [known-issues.md](known-issues.md).

## Where to start for common tasks

| Task | Start in | Chapter |
|---|---|---|
| Build, install, run | `Makefile`, `scripts/` | [03](03-getting-started.md) |
| Find a file, or what a script does | the repository root | [05](05-repository-tour.md) |
| Change QEMU code and keep the patch series | `qemu-src/`, `patches/` | [10](10-changing-qemu.md), [06](06-patch-series.md) |
| Move to a newer QEMU | `patches/BASE`, `patches/` | [11](11-upgrading-qemu.md) |
| Use another QEMU device or option | QEMU's arguments in m-browser's `src/app.rs`; maybe `scripts/configure.sh` | [12](12-adding-features.md) |
| Add a host import or export | the caller's file (imports), `system/main.c` (exports), and m-browser | [12](12-adding-features.md), [host interface](reference/host-interface.md) |
| A guest instruction misbehaves | `tcg/wasm32/tcg-target.c.inc`, `tests/x86-insn-diff/` | [07](07-tcg-backend.md), [13](13-testing.md), [14](14-debugging.md) |
| A hang, crash or wasm trap | `QEMU_STATUS_EVERY` (`system/runstate.c`) | [14](14-debugging.md) |
| Make the guest faster | `tcg/wasm32.c` (tiers, slices), `tools/perf/` | [15](15-performance.md), [07](07-tcg-backend.md) |
| Display, input or network | `display/`, `net/wasi-user.c.inc` | [09](09-display-input-network.md) |
| Something only the threaded build does | `accel/tcg/tcg-accel-ops-mttcg.c`, `#ifdef WASI_THREADS` | [08](08-threads.md) |
| A missing libc or GLib function | `wasi-stubs/`, `glib-wasi/` | [05](05-repository-tour.md), [12](12-adding-features.md) |
| Tune or add an environment variable | | [environment variables](reference/environment-variables.md) |
| Change the guest's disk image | | [16](16-guest-image.md) |
| Memory growth, Store moves, the JIT cache | m-browser's `src/wasm.rs`, `src/qemu_jit.rs` | the m-browser repository's docs/06-qemu-runner.md, docs/07-jit-host.md |
