# Environment variables

**What this covers.** Every environment variable that changes how qemu-wasi
builds or runs: the ones the patched QEMU and its display backend read at
run time (JIT tiers, time slices, diagnostics, display), the ones m-browser
reads for itself, and the ones the build and the instruction test use. For
each: its default, the values it accepts, what it does, where the code
reads it and which build flavor reads it. Read it when tuning or debugging
a run, and before adding a variable of your own. Terms such as TB
(translation block) and vCPU (virtual CPU) are explained in
[glossary.md](glossary.md).

## How to set variables

QEMU reads its variables from its WASI environment, which the host that
runs it fills in. m-browser copies into it every variable of its own
environment whose name starts with `QEMU_` or `WASI_` (case-sensitive;
`run_wasm_module()` in m-browser's `src/wasm.rs`), and nothing else: QEMU
does not even see `HOME`, `PATH` or `TMPDIR`. So put the variables on
m-browser's command line, for the GUI or for the headless runner:

```sh
# from the m-browser directory
QEMU_JIT_HOT=0 ./target/release/m-browser
WASI_DEBUG=1 ./target/release/m-browser --qemu-headless \
    qemu-system-x86_64.wasm -L /bios -drive file=/data/m.qcow2,format=qcow2

# from the qemu-wasi directory: passed on through run.sh and m-browser
QEMU_JIT_HOT=0 make test
```

- QEMU reads each variable once, when it first needs it, and m-browser's
  environment is fixed when it starts: restart to change a value.
- Reports and traces go to QEMU's stderr, which is m-browser's.
- Paths in values (`QEMU_TB_DUMP`, `QEMU_DUMP_AT`) are paths inside QEMU's
  sandbox, under a directory the host preopened. m-browser preopens its
  working directory as `/data` and its `pc-bios/` as `/bios`; the headless
  runner adds more with `--preopen HOST=GUEST`.
- *Presence* in the Values column means that any value, even `0` or an
  empty one, switches the variable on.
- Numbers go through C's `atoi`/`atoll` or `strtoul`/`strtoull`. Text that
  is not a number counts as 0, without a warning. The `strto*` parsers,
  marked *base 0*, also accept hexadecimal (`0x10`) and read a leading `0`
  as octal: `010` is 8.
- *Flavor*: *single* is the single-threaded build (`make`, `qemu-build/`),
  *threads* the multi-threaded one, one thread per vCPU (`make THREADS=1`,
  `qemu-build-threads/`, C macro `WASI_THREADS`); *both* means both. The
  m-browser GUI runs the flavor `make install` copied there last; the
  headless runner can run either build directly.
- *Read in*: paths are relative to `qemu-src/`, except `display/`, which is
  at the top of this repository.
- Not everything named `WASI_...` is a variable: `WASI_THREADS`,
  `WASI_PLATFORM` and `WASI_TB_YIELD_SLICE` are C macros fixed at build
  time, and `WASI_DBG:`, `WASI_HI`, `WASI_RR` and `WASI_FAULT:` are
  prefixes of trace lines.

## JIT tiers

QEMU translates guest code a block at a time; this port turns each
translation block (TB) into a small WebAssembly module. A new TB is run by
an interpreter built into QEMU. Once it has run often enough it is *hot*,
and the host compiles its module to machine code (m-browser uses
Cranelift). A compile costs about 1 to 1.5 ms, roughly a thousand runs'
worth, so it only pays off for TBs that keep running. The tiers are
explained in [../07-tcg-backend.md](../07-tcg-backend.md).

| Variable | Default | Values | Effect | Read in | Flavor | More |
|---|---|---|---|---|---|---|
| `QEMU_JIT_HOT` | 5000 | Integer, base 0, at most 4294967295 | Interpreted runs after which a TB is compiled. `0`: every TB is compiled, synchronously, before its first run; the interpreter then only runs TBs the JIT cannot take. A huge value such as `4000000000`: interpreter only, except for TBs the interpreter cannot decode, which are always compiled | `tcg/wasm32.c`, `tcg_qemu_tb_exec()`; default `INTERP_HOT_DEFAULT` in `tcg/wasm32/wasi-interp.c.inc` | both | [07-tcg-backend](../07-tcg-backend.md), [15-performance](../15-performance.md) |
| `QEMU_JIT_WARM` | 50 | Integer, base 0 | After this many interpreted runs, QEMU asks the host whether it still has an identical module compiled, for instance from before a TB buffer flush (which throws away all translations); if so, the TB runs compiled from then on. Single: asked once, at exactly this count. Threads: every 16 runs from this count up to `QEMU_JIT_HOT`, because each vCPU needs a handle of its own. No effect unless smaller than `QEMU_JIT_HOT` | `tcg/wasm32.c`, `tcg_qemu_tb_exec()`; default `INTERP_WARM_DEFAULT` in `tcg/wasm32/wasi-interp.c.inc` | both | [07-tcg-backend](../07-tcg-backend.md) |
| `QEMU_JIT_ASYNC` | on | Only exactly `0` turns it off; any other value (`false`, `no`, empty) leaves it on | On: a hot TB stays interpreted while the host compiles it on background threads (m-browser's `QEMU_JIT_THREADS`); QEMU asks again every 16 runs. `0`: QEMU waits for the compile when the TB gets hot. Always off when `QEMU_JIT_HOT=0` | `tcg/wasm32.c`, `tcg_qemu_tb_exec()` | both | [07-tcg-backend](../07-tcg-backend.md) |

`QEMU_JIT_HOT=0` and `QEMU_JIT_HOT=4000000000` test one tier alone
([../13-testing.md](../13-testing.md)). The default comes from a
measurement: booting Alpine to a login prompt was about 10% faster with
5000 than with 1000 (comment above `INTERP_HOT_DEFAULT`).

## Time slices and timer ticks

In the single-threaded build one thread runs both the guest's CPU and
QEMU's main loop (timers, devices, display, input, network), taking turns:
the vCPU runs for a *slice*, then yields to the main loop. Almost every
yield lets the guest's timer interrupt in (its *tick*, 1000 times a second
in Alpine's kernel), and on a busy guest each tick costs a couple of
thousand TBs of interrupt handling. So slices must be long enough to make
ticks rare, but short enough for the display and input to stay smooth. The
multi-threaded build has no slices, since its main loop has a thread of its
own; it saves the same work by making the guest's timer late on purpose.

| Variable | Default | Values | Effect | Read in | Flavor | More |
|---|---|---|---|---|---|---|
| `QEMU_TB_SLICE_MS` | 30 | Milliseconds, integer, base 0; 0, negative or not a number: 30 | Length of a slice, in host time. Checked every 1024 TBs, so a slice can run slightly over. The vCPU also returns early, for instance when the guest has nothing to do. Ignored when `QEMU_TB_SLICE` is set | `tcg/wasm32.c`, `wasi_slice_settings()`, via `wasi_slice_begin()` from `wasi_rr_cpu_step()` in `accel/tcg/tcg-accel-ops-rr.c` | single | [15-performance](../15-performance.md) |
| `QEMU_TB_SLICE` | unset | TB count, integer, base 0; 0 or not a number: as unset | Slices of this many TBs instead of a time, as before slices were timed (then 4000, the compile-time constant `WASI_TB_YIELD_SLICE`). Wins over `QEMU_TB_SLICE_MS` | `tcg/wasm32.c`, `wasi_slice_settings()` | single | [15-performance](../15-performance.md) |
| `QEMU_TICK_SLACK_MS` | 30 | Milliseconds, integer (`atoll`); 0, negative or not a number: no slack | The guest's local APIC timer, which drives its tick, goes off only on a grid of this many milliseconds of QEMU's virtual clock: each deadline is rounded up to the next multiple. The guest sees late ticks and spends less time on them. No slack: deadlines are kept exactly | `hw/intc/apic.c`, `apic_timer_slack()`, from `apic_timer_update()` | threads | [08-threads](../08-threads.md), [15-performance](../15-performance.md) |

## Diagnostics and debugging

All of these print to stderr. `QEMU_STATUS_EVERY`, `QEMU_DUMP_AT` and
`QEMU_LOOP_DIAG` slow the guest down as long as they are set at all
([below](#the-per-tb-debug-hooks)); unset them when done.

| Variable | Default | Values | Effect | Read in | Flavor | More |
|---|---|---|---|---|---|---|
| `QEMU_PROF_EVERY` | off | Seconds, integer (`atoll`); 0, negative or not a number: off | Measures where QEMU's time goes and prints a `[wasi-prof]` report to stderr every this many seconds ([below](#qemu_prof_every-output)). Taking the measurements costs some speed (clock reads are host calls) | `tcg/wasm32.c`, `wasi_prof_enabled()` and `wasi_prof_report_maybe()`, the latter called by `qemu_main_loop_step()` in `system/runstate.c` | single (threads: the measurements are taken, and cost time, but the report is never printed) | [15-performance](../15-performance.md), [07-tcg-backend](../07-tcg-backend.md) |
| `QEMU_STATUS_EVERY` | off | Seconds, integer (`atoi`); 0 or not a number: no dumps, but see Effect | Every this many seconds of host time, prints the state of vCPU 0 to stderr ([below](#qemu_status_every-output)). Its presence alone, even `0`, also switches on the per-TB debug hooks ([below](#the-per-tb-debug-hooks)), which slow execution down | `system/runstate.c`, `wasi_status_dump()`; `accel/tcg/cpu-exec.c`, `tb_debug_hooks_enabled()` | both | [14-debugging](../14-debugging.md) |
| `QEMU_TB_DUMP` | off | `<pc>,<dir>`: the guest address where the TB starts, hex (`0x` optional), and a directory inside the sandbox (up to 255 characters, no spaces). Malformed: off, silently | Writes the wasm module of that TB to `<dir>/tb_<pc>.wasm` (lowercase hex, no `0x`) each time QEMU hands the module to the host (see the JIT tiers above), so usually several times, and not on the TB's first run unless `QEMU_JIT_HOT=0`. Prints `QEMU_TB_DUMP: wrote ...`; nothing if the file cannot be created. Read the file with `wasm-tools print` | `tcg/wasm32.c`, `compile_and_cache_tb()` | both | [07-tcg-backend](../07-tcg-backend.md), [14-debugging](../14-debugging.md) |
| `QEMU_DUMP_AT` | off | `<pc>,<n>,<addr>,<len>,<path>` ([below](#qemu_dump_at-syntax)) | Once: on the n-th execution of the TB at guest address pc, writes len bytes of guest physical memory from addr to path and prints the CPU state to stderr. For comparing this build with a native QEMU. Its presence, even malformed, switches on the per-TB debug hooks | `accel/tcg/cpu-exec.c`, `debug_dump_at()`, from `cpu_tb_exec()` | both, and the native build | [14-debugging](../14-debugging.md) |
| `QEMU_LOOP_DIAG` | off | Presence | When the same TB is entered 20,000,000 times in a row (as counted by the per-TB debug hooks, [below](#the-per-tb-debug-hooks)), prints `QEMU_LOOP_DIAG: pc=0x... ran 20000000 times in a row`, switches on QEMU's logs of guest instructions, TCG ops, executions, CPU state and interrupts (as `-d in_asm,op_opt,exec,cpu,int`) for the next 40 TB entries, and makes QEMU retranslate that TB so its translation is logged too. Then it turns all logging off, including flags given with `-d`. Logs go where QEMU's log goes: stderr, or the `-D` file. For a guest stuck in a loop at an unknown address. Switches on the per-TB debug hooks | `accel/tcg/cpu-exec.c`, `tb_loop_diag()` | both, and the native build (one counter for all vCPUs: clearest with `-smp 1`) | [14-debugging](../14-debugging.md) |
| `WASI_DEBUG` | 0 | Integer (`atoi`): 0, negative or not a number: quiet; 1; 2 or more | 1: start-up and lifecycle tracing on stderr: `WASI_DBG:` lines from `main()`, `qemu_init()`, block device set-up and system reset; `wasi-gfx:` lines for display updates and refreshes (the first few, then every 60th or 120th) and for every key event; the first `[WASI_IO]` and `WASI_FAULT:` events. 2: adds hot-path tracing, sampled (the first five, then every 10,000,000th): TB executions (`[WASI_TB]`), interrupt checks (`WASI_HI`) and, single-threaded, vCPU loop steps (`WASI_RR`), plus more start-up detail | `include/qemu/wasi-debug.h`, `wasi_debug_level()` (macros `WASI_LOG`, level 1, and `WASI_LOG_V`, level 2); also `display/wasi_gfx_display.c` | both | [14-debugging](../14-debugging.md) |

### QEMU_PROF_EVERY output

Two lines per report, each covering the time since the previous report
(shown wrapped here; the numbers are only an example):

```text
[wasi-prof] 10 s: translate 0.4 s (2100 TBs, 190 us each), decode 0.1 s,
  jit-compile 0.8 s (650), exec-interp ~2.0 s (3.1M TBs, 0.65 us each),
  exec-jit ~4.1 s (40.2M TBs, 0.10 us each), other-cpu 1.2 s,
  main-loop 0.9 s, idle 0.5 s, memory 1650 MiB
[wasi-prof]   interpreted runs by how often the TB had run before: <50 41%,
  50-199 22%, 200-999 19%, 1000-5000 17%, never compiled 1%; TBs reaching
  50/200/1000 runs: 900/310/120
```

- *translate*: turning guest code into TBs (`tb_gen_code()`): time, count,
  mean. *decode*: the interpreter's one-time decoding of new TBs.
- *jit-compile*: time spent getting compiled modules from the host
  (compiles and cache loads), and how many it returned.
- *exec-interp*, *exec-jit*: time running TBs in each tier, estimated by
  timing every 256th run.
- *other-cpu*: the rest of the vCPU's time (QEMU's execution loop, TB
  lookups, interrupts, deferred frees).
- *main-loop*: timers, devices and display, not counting *idle*, the time
  slept while the guest had nothing to do.
- *memory*: the size of QEMU's WebAssembly memory.
- The second line splits the interpreted runs by how often the TB had run
  before (the fourth bucket ends at `QEMU_JIT_HOT`) and counts the TBs
  that reached 50, 200 and 1000 runs: what a lower `QEMU_JIT_HOT` would
  change.

How to act on it: [../15-performance.md](../15-performance.md); what each
tier does: [../07-tcg-backend.md](../07-tcg-backend.md).

### QEMU_STATUS_EVERY output

Each dump is a block of lines about vCPU 0 (in the threaded build it is
read while that vCPU runs, so it may be slightly inconsistent):

1. `QEMU_STATUS: t=<ms>ms halted=<0|1> interrupt_request=0x<hex>`: the
   host clock, whether the vCPU is halted (waiting for an interrupt), and
   its pending interrupt requests.
2. The registers, in the format of the monitor command `info registers`.
3. `QEMU_STATUS: code at pc-16:` 96 bytes of guest code starting 16 bytes
   before the program counter; a `|` marks the program counter.
4. `QEMU_STATUS: exit_request=... icount_decr=... exception_index=...
   cflags_next_tb=...`: the state of QEMU's execution loop.
5. `QEMU_STATUS: stack at 0x<sp>:` 32 64-bit words from the stack pointer,
   for mapping return addresses to symbols offline.
6. `TB history (<N> TBs total, oldest first):` the last 64 TBs the debug
   hooks recorded, with their flags.
7. A `[wasi-interp]` line: TBs decoded, not interpretable, invalid,
   promoted to the JIT, reloaded from its cache, rejected by the JIT, and
   interpreted executions. QEMU also prints it at TB buffer flushes.

### QEMU_DUMP_AT syntax

`QEMU_DUMP_AT=<pc>,<n>,<addr>,<len>,<path>`, no spaces:

| Field | Format | Meaning |
|---|---|---|
| `pc` | hex, `0x` optional | Guest address where the TB starts, as QEMU's `-d in_asm` and `-d exec` logs print it |
| `n` | decimal, from 1 | Which execution of that TB triggers the dump |
| `addr` | hex | First guest physical address to save |
| `len` | hex | Number of bytes to save |
| `path` | up to 511 characters | Output file, inside the sandbox |

For example, `QEMU_DUMP_AT=ffffffff81000000,3,100000,1000,/data/mem.bin`
saves 4 KiB of guest memory from physical address 0x100000 on the third
run of the TB at 0xffffffff81000000; under m-browser, `/data` is its
working directory. QEMU prints `QEMU_DUMP_AT: armed ...` when the first TB
runs, then `QEMU_DUMP_AT: hit #3 ...` and the registers; a malformed value
prints `QEMU_DUMP_AT: expected <pc>,<n>,<addr>,<len>,<path>` and nothing
is dumped.

Only executions that start in `cpu_tb_exec()` are counted, not TBs reached
by chaining from another TB. Add `-d nochain` to QEMU's arguments, so that
every execution counts and the count matches a `-d exec` trace. With
several vCPUs, executions on all of them count together.

### The per-TB debug hooks

`QEMU_DUMP_AT`, `QEMU_LOOP_DIAG` and `QEMU_STATUS_EVERY`, if present at
all, switch on hooks in `accel/tcg/cpu-exec.c` that run each time QEMU
starts a TB from its execution loop or looks one up for an indirect jump:
they record the TB history, count repeats for the loop detector and check
for the dump point. This slows the guest down, so unset these variables
when not debugging. TBs chained directly to each other bypass the hooks;
`-d nochain` makes every TB go through them, at a further cost in speed.

The hooks are plain QEMU code, outside the WASI-only parts, so the native
QEMU that `make native` builds from the same tree honours `QEMU_DUMP_AT`
and `QEMU_LOOP_DIAG` too: run the same guest under both and compare.
(`QEMU_STATUS_EVERY`'s dump only exists in the wasm builds.)

The comment above `tb_loop_diag()` says `QEMU_LOOP_DIAG` triggers after a
million repeats; the code uses 20,000,000.

## Display

These configure the display backend in `display/`, which shows QEMU's
consoles through the host (m-browser's window, or its PNG snapshots when
headless).

| Variable | Default | Values | Effect | Read in | Flavor | More |
|---|---|---|---|---|---|---|
| `QEMU_DISPLAY_STATS` | off | Presence | About every 20 s, prints `wasi-gfx: <n> refreshes in <ms> ms: <x> ms each (hw <a>, blit <b>, render <c>) = <p>% of the time`. *hw*: polling the host for input, then QEMU's display device updating its image (VGA redrawing); *blit*: copying the whole image (only on full refreshes); *render*: magnifying it and handing it to the host. *p*: the share of wall-clock time spent refreshing, which the single-threaded build takes from the guest | `display/wasi_gfx_dcl.c`, `refresh_stats_report()`, from `wasi_gfx_dpy_refresh()` | both | [09-display-input-network](../09-display-input-network.md) |
| `WASI_GFX_SCALE` | 2 | Integer (`atoi`) from 1 to 8; smaller values, empty or text give 1, larger give 8 | Magnifies the guest's screen by this whole factor (nearest neighbour, so text stays sharp). The factor is reduced as needed to keep the image within 1600x1200: a 720x400 text screen is doubled, a 1280x800 desktop is shown 1:1. The host window, the headless snapshots and pointer coordinates are in magnified pixels | `display/wasi_gfx_display.c`, `wasi_gfx_scale()`, via `wasi_gfx_scale_for()` | both | [09-display-input-network](../09-display-input-network.md) |
| `WASI_GFX_CONSOLE` | first graphic console, else the first text console | Console index from 0 (`atoi`; text gives 0); empty: default | The QEMU console shown, and given the input, at start-up. Console *n* is also reachable with Ctrl+Alt+*n+1*. With m-browser's GUI arguments, 0 is the guest's screen, 1 QEMU's monitor and 2 the serial port. An index with no console prints `wasi-gfx: WASI_GFX_CONSOLE=<value> not found, ignoring` and the default is used | `display/wasi_gfx_dcl.c`, `wasi_gfx_pick_console()`, from `wasi_gfx_display_register_dcl()` (called by `qemu_init_displays()` in `system/vl.c`) | both | [09-display-input-network](../09-display-input-network.md) |
| `WASI_GFX_FULL_REFRESH` | off | Presence | Redraws and copies the whole screen on every display refresh (about 30 a second) instead of relying on QEMU's change tracking, which is backed by a full redraw every 60th refresh. A debugging aid for stale or garbled screens; expensive in graphics modes, where each redraw makes VGA build a new image | `display/wasi_gfx_dcl.c`, `wasi_gfx_dpy_refresh()` | both | [09-display-input-network](../09-display-input-network.md) |

## Network

QEMU reads no network variables. Its network card and the user-mode
network come from the `-nic` option on its command line, and the network
itself is m-browser's. m-browser's `QEMU_NIC` replaces the GUI's
`-nic user` (for example to forward a host port into the guest), and its
`QEMU_NET_DEBUG` logs the traffic; see
[Variables read by m-browser](#variables-read-by-m-browser) and
[../09-display-input-network.md](../09-display-input-network.md).

## Variables of upstream QEMU

QEMU code that this port did not add also reads a few variables: for
example `G_MESSAGES_DEBUG` (`util/error-report.c`, GLib debug messages) and
`QEMU_DELETE_MACHINES` (`include/hw/boards.h`; when set, old versioned
machine types due for deletion are not registered). None is specific to
WebAssembly, and under m-browser only names starting with `QEMU_` or
`WASI_` reach QEMU at all.

## Variables read by m-browser

m-browser reads these for itself; the m-browser repository's
docs/reference/environment-variables.md describes them in full. It also
passes the `QEMU_` ones into QEMU, which ignores them.

| Variable | What it does |
|---|---|
| `QEMU_NIC` | Replaces the value of QEMU's `-nic` option in the GUI (default `user`) |
| `QEMU_NET_DEBUG` | Logs the guest's network traffic |
| `QEMU_SMP` | vCPUs given to the multi-threaded build (`-smp`), unless QEMU's arguments set it |
| `QEMU_JIT_THREADS` | Threads that compile TB modules in the background |
| `QEMU_JIT_STATS` | Prints the JIT's statistics every minute |
| `QEMU_JIT_DUMP` | Saves every n-th compiled TB module (a host path; compare `QEMU_TB_DUMP`) |
| `QEMU_MIGRATE_AFTER` | TB module instances a wasmtime Store may hold before it is replaced |
| `QEMU_WASM_OPT` | How much Cranelift optimizes QEMU's own module and the TB modules |
| `QEMU_PROFILE`, `QEMU_PROFILE_MS`, `QEMU_PROFILE_EVERY`, `QEMU_PROFILE_DIR`, `QEMU_PROFILE_TOP` | m-browser's sampling profiler, single-threaded build only (not `QEMU_PROF_EVERY`) |
| `QEMU_SNAPSHOT`, `QEMU_TYPE`, `QEMU_TYPE_FIFO`, `QEMU_QUIT_AFTER` | Headless runner: screen to PNG, typing, pointer commands, timed quit |
| `XDG_CACHE_HOME`, `HOME`, `TMPDIR` | Where QEMU's compiled module is cached |

## Variables used by the build and tests

The Makefile takes its variables from the command line (`make THREADS=1`)
or the environment (`THREADS=1 make`), and exports `WASI_SDK`, `M_BROWSER`
and `THREADS` to the scripts it runs; the scripts read only the
environment.

| Variable | Default | Values | Effect | Read in | More |
|---|---|---|---|---|---|
| `WASI_SDK` | `/opt/wasi-sdk` | Directory | The wasi-sdk installation: compilers, `llvm-ar`, the sysroot | `Makefile`, `scripts/configure.sh`, `scripts/build-zlib.sh`, `scripts/fix_build_ninja.py`, `glib-wasi/Makefile`, `wasi-stubs/Makefile` | [03-getting-started](../03-getting-started.md), [05-repository-tour](../05-repository-tour.md) |
| `THREADS` | 0 | Exactly `1`: multi-threaded; anything else: single-threaded | Selects the flavor: `qemu-build-threads/`, target `wasm32-wasip1-threads`, `-DWASI_THREADS -pthread`, and the `-threads` variants of glib-wasi, wasi-stubs and zlib. Also applies to `make install`, `make test` and `make clean` | `Makefile`, `scripts/configure.sh`, `scripts/build-zlib.sh`, `glib-wasi/Makefile`, `wasi-stubs/Makefile` | [08-threads](../08-threads.md), [05-repository-tour](../05-repository-tour.md) |
| `M_BROWSER` | `../m-browser`, next to this repository | Directory | Where `make install` copies the module, as `qemu-system-x86_64.wasm`; the m-browser whose `target/release/m-browser` runs the instruction test | `Makefile` (`install`), `tests/x86-insn-diff/run.sh` | [03-getting-started](../03-getting-started.md), [13-testing](../13-testing.md) |
| `NINJA` | `ninja` | Command | The ninja program that builds QEMU | `Makefile` (`all`, `native`) | [05-repository-tour](../05-repository-tour.md) |
| `QEMU_GIT` | `https://gitlab.com/qemu-project/qemu.git` | Git URL | Where QEMU is cloned from, only when `qemu-src/` does not exist yet. The tag is in `patches/BASE`, not a variable | `scripts/fetch-qemu.sh` | [05-repository-tour](../05-repository-tour.md), [11-upgrading-qemu](../11-upgrading-qemu.md) |
| `ZLIB_GIT` | `https://github.com/madler/zlib.git` | Git URL | Where zlib is cloned from, only when `zlib-src/` does not exist yet | `scripts/build-zlib.sh` | [05-repository-tour](../05-repository-tour.md) |
| `ZLIB_TAG` | `v1.3.1` | Git tag or branch | The zlib version cloned | `scripts/build-zlib.sh` | [05-repository-tour](../05-repository-tour.md) |
| `QEMU_WASM` | `make test`: the selected flavor's build; `run.sh` alone: `qemu-build/qemu-system-x86_64` | Path | The wasm QEMU the instruction test runs. `make test` sets it itself, so set it only when calling `run.sh` directly (for example to test `qemu-build-threads/qemu-system-x86_64`) | `tests/x86-insn-diff/run.sh` | [13-testing](../13-testing.md) |
| `NATIVE_QEMU` | `qemu-native/qemu-system-x86_64` (from `make native`) | Path | The native QEMU whose results are the reference | `tests/x86-insn-diff/run.sh` | [13-testing](../13-testing.md) |
| `OUT` | `${TMPDIR:-/tmp}/qemu-wasi-insn` | Directory | Where the test writes the test program, `native.txt`, `wasm.txt`, `wasm-run.log` and `diff.txt` | `tests/x86-insn-diff/run.sh` | [13-testing](../13-testing.md) |
| `WASM_TIMEOUT` | 900 | Seconds | Time limit for the wasm run (the native run has a fixed 120 s) | `tests/x86-insn-diff/run.sh` | [13-testing](../13-testing.md) |

The test runs the wasm QEMU through m-browser, so QEMU-side variables set
for `make test` or `run.sh` reach it, such as `QEMU_JIT_HOT` to pick a
tier. `QEMU_WASM`, which `make test` sets, and `QEMU_GIT`, if exported,
reach it too, harmlessly. The native QEMU gets the same environment, and
honours `QEMU_DUMP_AT` and `QEMU_LOOP_DIAG` as well.

## Performance scripts

The scripts in `tools/perf/` take variables of their own, documented in
[tools/perf/README.md](../../tools/perf/README.md).

## Adding a variable

- Start the name with `QEMU_` or `WASI_`, or m-browser will not pass it in.
- Read it once into a `static`, as the existing ones do, rather than
  calling `getenv()` on a hot path: each call searches the whole
  environment.
- Add it to this page, with its flavor (check for `#ifdef WASI_THREADS`
  around the code that reads it).
