# Known issues and limitations (qemu-wasi)

**What this covers.** Problems, limitations and rough edges that are known
but not fixed, collected while writing this documentation and during the
work described in [17-history.md](17-history.md).  Each entry says how it
shows, why it happens, and, where known, how to fix it.  Check here before
debugging something strange; add to it when you find something you cannot
fix right away.  Items marked "(from reading the code)" were found by
reading, not by running.  Host-side issues are in the m-browser
repository's docs/known-issues.md.

## Limitations by design

| Limitation | Why | Notes |
|---|---|---|
| At most 4 GiB for everything QEMU uses (guest RAM, the 256 MiB translation buffer, QEMU itself) | wasm32 addresses are 32 bits | Guest RAM above about 2 GiB will not fit |
| At most 8 vCPUs in the threads build | `WASI_JIT_SLOTS` in tcg/wasm32.h: one handle slot per vCPU in each translated block | `-smp 9` or more aborts; see [08-threads.md](08-threads.md) to raise it |
| Threads build: guest memory ordering is right only on x86-64 hosts | `TCG_TARGET_DEFAULT_MO` relies on the host keeping x86's ordering (TSO) | m-browser warns on other hosts |
| Only `x86_64-softmmu` is built | `scripts/configure.sh`, display and test scripts assume it | Another guest architecture: [12-adding-features.md](12-adding-features.md) |
| No KVM or other hardware virtualization | WebAssembly has none | Everything runs through TCG |
| Only HMP, not QMP | The QMP dispatcher coroutine is never created on WASI (patch 0007, monitor/monitor.c) | The text monitor (Ctrl+Alt+2) works |
| User networking only, IPv4, TCP-only port forwarding | The network is emulated by m-browser | See the m-browser repository's docs/10-networking.md |
| No disassembler (capstone) | Not built | `-d in_asm` shows raw bytes; the monitor's `x/i` cannot disassemble |

## Bugs and risks

- **Threads build: vCPU threads are not ended when QEMU exits** (from
  reading the code).  `wasi_qemu_step()` in system/main.c unplugs the
  vCPUs after `qemu_cleanup()`, but the main thread still holds the big
  QEMU lock (BQL), so vCPU threads waiting for it never see the unplug.
  In m-browser's headless mode the process exits anyway; in its GUI the
  threads and QEMU's memory stay allocated until the browser exits, so
  starting QEMU again in the same browser session costs that memory again.
  A likely fix is `bql_unlock()` after the unplug loop; untested, and the
  vCPU threads would then run code after `qemu_cleanup()`, which must be
  checked carefully (a failing thread ends the whole m-browser process).
- **Threads build: the main loop can oversleep by up to 30 ms** when it
  sleeps without the user network's wait function (no `-nic user`, or the
  network card's queue full): `host_main_wake` cannot interrupt a plain
  `nanosleep()` (util/main-loop.c, from reading the code).
- **`-d out_asm` probably hangs** (from reading the code):
  `print_insn_tci()` in tcg/wasm32.c returns 0, so QEMU's disassembly loop
  never advances.
- **Unchecked limits when translating** (from reading the code): in
  release builds nothing stops a translated block from using more than 100
  labels (`current_label[100]` in tcg/wasm32/tcg-target.c.inc), 1024
  helpers or 4096 label uses (tcg-wasm32-helpers.c.inc); going over would
  corrupt memory silently.  Never observed.
- **Display: button codes above 9 from the host abort QEMU** (an
  assertion in `qapi_enum_lookup()`), and text consoles keep only the low
  8 bits of each character (characters above U+00FF arrive garbled).
  See [09-display-input-network.md](09-display-input-network.md).
- **Display: no console means no display and no input**, silently:
  `wasi_gfx_display_register_dcl()` prints "deferring init" and nothing
  retries.  Not seen with the default machine.
- **`-icount` with the threads build** probably hangs: MTTCG is turned off
  and nothing runs the vCPU (from reading the code; untested).
- **32-bit guest addresses**: the backend's `qemu_ld/st_a32` forms do not
  zero-extend the address.  The x86_64 guest does not use them.
- **A block jumping to itself through `goto_ptr`** loops inside one wasm
  call and is not counted toward the single-threaded build's time slice
  (a very long loop could delay timers).
- **glib-wasi's checksum functions return zeros** (stubs), and its main
  context never dispatches sources: `g_timeout_add`, `g_idle_add` and I/O
  watches never run, `g_poll` never waits, `g_usleep` does nothing.  Code
  relying on them silently does nothing; patch 0004 works around it for
  the main loop.  See [05-repository-tour.md](05-repository-tour.md).
- **glib-wasi's threads variant is not fully thread-safe**: `gatomic.h`
  operations are plain (non-atomic), and reference counts in its hash
  tables and arrays and the quark table are unprotected.  QEMU only uses
  them under its own locks so far.
- **`cpu_io_recompile()` is skipped** (accel/tcg/cputlb.c, patch 0003): I/O
  instructions in the middle of a block run without QEMU's usual exact
  instruction boundary.  The reason given in the code comment predates
  today's design; whether the original behavior would work now has not
  been re-tested.
- **Native builds** (used only by the instruction test) need
  `--with-coroutine=ucontext` (patch 0001 lists the WASI-oriented `fiber`
  backend first) and probably pixman's development files (patch 0005
  builds `ui/console-vc.c` unconditionally).
- **Patches that also change non-WASI builds**: patch 0001 disables the
  `ATOMIC_REG_SIZE` assertions in include/qemu/atomic.h and configure's
  "unknown OS" check for every host; patch 0003's debugging hooks
  (`QEMU_DUMP_AT`, `QEMU_LOOP_DIAG`, `QEMU_STATUS_EVERY`) are compiled on
  all hosts.  Harmless for this project, but they could not go upstream
  as they are.

## Tooling rough edges

- **ninja may re-run meson by itself** when a `meson.build` file changes,
  regenerating `build.ninja` without `scripts/fix_build_ninja.py`'s edits;
  the link then fails.  Running `make` again fixes it (the Makefile runs
  the script before ninja).
- **`make export-patches` touches the stamp file**, so the next `make`
  reconfigures (a minute or so).  By design.
- **After pulling new patches, `make` keeps building the old tree** and
  only warns.  Its advice to export first could overwrite the pulled
  patches with your older series: follow
  [10-changing-qemu.md](10-changing-qemu.md).
- **fetch-qemu.sh clones shallowly**, so `make reset-qemu` cannot switch to
  a new base tag: see [11-upgrading-qemu.md](11-upgrading-qemu.md).
- **glib-wasi's single-threaded variant is compiled with
  `wasm32-wasip2-clang`**, everything else with `wasm32-wasip1`.  It works;
  noted in case a wasi-sdk upgrade breaks it.
- **`QEMU_PROF_EVERY` measures but never prints in the threads build**, and
  m-browser's sampling profiler does not follow vCPU threads: profile with
  the single-threaded build.  Statistics counters in tcg/wasm32.c are not
  atomic in the threads build (cosmetic).
- **`QEMU_LOOP_DIAG`** fires after 20,000,000 runs of the same block,
  although the comment above it says a million.
- **tools/perf's Chromium detection** (`menupx.py find`) looks for three
  dark dots stacked on a light background, scanning leftwards from the
  right edge of the screen in its top 260 rows.  It fits Chromium's default
  light theme at 100% zoom, and anything further right that looks the same
  would be taken for the menu button; other layouts need `MENU_X`/`MENU_Y`.
  `click-latency.sh` closes the menu with a click at `AWAY_X`,`AWAY_Y`
  (1270,600), assumed to be bare desktop right of Chromium's window.

## Stale comments

Comments that no longer match the code; harmless, but misleading:

- display/wasi_gfx_display.c: the input event list does not mention type 5
  (text); display/wasi_gfx_dcl.c: the trailing comment about constructors
  and a main wrapper.
- tcg/wasm32.c: `QEMU_TB_DUMP`'s comment says modules are compiled "just
  before a TB first runs" (they are compiled when hot); several comments
  still describe TCG registers as wasm globals.
- glib-wasi/pkgconfig/gthread-2.0.pc ("single-threaded, all no-ops") and
  glib-2.0.pc (mentions a `build.sh`); glib-wasi/glib/glib.h's version
  comment.
- wasi-stubs/pthread_stubs.c: the comment about RCU callbacks leaking
  (patch 0004 runs them).
- tcg/wasi_jit.h declares `wasi_jit_call`, `wasi_jit_drop` and
  `wasi_jit_take_pending_exception`, which no current build imports.

## The guest image

- The root password is unknown; use `tools/perf/guest-shell.sh`
  ([16-guest-image.md](16-guest-image.md)).
- X blanks the screen after 10 minutes without input.
- Closing m-browser is a power cut for the guest; Chromium is configured
  to hide its "Restore pages?" bubble because of that.
