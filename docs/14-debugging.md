# Debugging

**What this covers.** How to find and fix bugs in qemu-wasi on your own: a
method for any symptom, the diagnostic switches of this port and of
m-browser, a guide per class of symptom, eight bugs from the project's
history as worked examples, and how to read generated WebAssembly, wasm
backtraces and the guest's state. TB (translation block), vCPU, helper and
the two execution tiers are explained in [02-concepts.md](02-concepts.md)
and [reference/glossary.md](reference/glossary.md); exact variable syntax
is in [reference/environment-variables.md](reference/environment-variables.md).
The host side has its own guide with a symptom table, the m-browser
repository's docs/14-testing-and-debugging.md. Paths are relative to
`qemu-src/` unless they start with `display/`, `tests/` or `tools/`, or
are m-browser's.

## First steps when something goes wrong

### The method

```text
reproduce -> isolate the layer -> minimize -> inspect -> fix -> add a test
```

1. **Reproduce.** Turn the report into a command you can run again: the
   flavor (`make` or `make THREADS=1`), QEMU's arguments, the environment,
   the disk image, how long it takes to go wrong. Prefer the headless
   runner ([below](#a-debugging-run)) and keep its whole output. A bug
   that shows up one run in five counts; note the rate.
2. **Isolate the layer.** Every bug lives in one layer of the table below,
   and each layer has an experiment that tells it apart. Run them before
   reading code. The interpreter runs exactly the wasm the JIT compiles
   ([07-tcg-backend.md](07-tcg-backend.md#running-tbs-the-tiers)), which
   makes the tier experiment decisive.
3. **Minimize.** One vCPU, the single-threaded build, a root shell rather
   than a desktop, one program, ideally one instruction form in the
   instruction test. Remove randomness ([below](#isolating-the-tier)).
4. **Inspect** the state where it goes wrong (registers, memory, the TB,
   its TCG ops, its wasm); compare with a run that works.
5. **Fix** it in `qemu-src/`, in the patch the code belongs to
   ([10-changing-qemu.md](10-changing-qemu.md)); rerun the reproducer.
6. **Add a test**: an instruction form in `tests/x86-insn-diff/insn-test.c`
   for anything the CPU computes ([13-testing.md](13-testing.md)); else the
   reproducing command in the commit message and a comment at the fix.

| Layer | Code | Points to it |
|---|---|---|
| Guest | Alpine's kernel and programs, the disk image | The same happens under the native QEMU built from the same tree (`make native`) |
| TCG front end | `target/i386/tcg/`: x86 instructions to TCG ops | Wrong under the native QEMU too, which shares it: an upstream bug, rare |
| QEMU devices and core | `hw/`, `system/`, `util/`, `ui/`, `net/`, the WASI changes of patches 0003 to 0008 | Not under native QEMU, but in both tiers; tied to I/O, timers, the main loop or start-up rather than to instructions |
| wasm backend | `tcg/wasm32/tcg-target.c.inc`, `tcg/wasm32/tcg-wasm32-helpers.c.inc`: TCG ops to wasm | Both tiers fail alike; the instruction test fails in every mode; `wasm-tools validate` rejects a module |
| Interpreter | `tcg/wasm32/wasi-interp.c.inc` | Fails with `QEMU_JIT_HOT=4000000000` (interpreter only), not with `QEMU_JIT_HOT=0` (JIT only) |
| JIT host | m-browser's `src/qemu_jit.rs`, wasmtime, Cranelift | Fails with `QEMU_JIT_HOT=0`, not with `QEMU_JIT_HOT=4000000000`; `[qemu-jit]` messages |
| Tier switching, handles | `tcg_qemu_tb_exec()` in `tcg/wasm32.c`, Store moves | Fails in the default mode only, in neither single-tier mode |
| Host | m-browser's runner, threads, display, network | `[qemu]`, `[gfx]` or `[qemu-net]` messages, Rust panics; only in the GUI, or on one machine |

### Triage

1. **Read the log first**: most failures announce themselves (table
   below). A failed build: [The build fails](#the-build-fails).
2. **QEMU is gone**: [QEMU exits at startup](#qemu-exits-at-startup) or
   [A wasm trap](#a-wasm-trap).
3. **QEMU runs, the guest is stuck**: watch m-browser in `top`. A core at
   100% means a busy (perhaps looping) guest; near 0%, a halted vCPU or a
   waiting QEMU. Then set `QEMU_STATUS_EVERY=10`: [The guest hangs during
   boot](#the-guest-hangs-during-boot).
4. **Wrong results**: [The guest crashes or
   misbehaves](#the-guest-crashes-or-misbehaves). Only in the threads
   build: try `-smp 1` first ([threads](#problems-only-in-the-threads-build)).

| Message | Meaning | Look at |
|---|---|---|
| `QEMU exited during startup with status N (its error message is above)` | QEMU called `exit()` in `wasi_qemu_init()`; the reason is the `qemu-system-x86_64: ...` line above | [Startup](#qemu-exits-at-startup) |
| `step failed:`, `QEMU _initialize failed`, `QEMU init failed`, `thread N failed:`, `vCPU N failed:`, with a wasm backtrace | QEMU trapped | [Backtraces](#reading-a-wasm-backtrace) |
| `[wasi-interp] wasm trap: <op>` | An interpreted TB executed `unreachable` or a trapping division; QEMU aborts | [Generated code](#inspecting-generated-code) |
| `qemu-wasi: fatal: TB 0x... can be neither compiled nor interpreted` | The interpreter cannot decode the TB and the JIT rejected it | [Generated code](#inspecting-generated-code) |
| `[wasi-interp] TB 0x... is not valid wasm: ...` | The backend emitted invalid wasm; the TB stays interpreted (first 20 reported) | [Generated code](#inspecting-generated-code) |
| `[wasi-interp] TB 0x... not interpretable: ..., using the JIT` | The backend emitted wasm the interpreter does not know; the TB is compiled on its first run. Slow if frequent | [07](07-tcg-backend.md#things-a-porter-must-preserve), item 13 |
| `qemu-wasi: JIT compile failed for TB at 0x...` | A synchronous compile failed; m-browser's reason is just above | [Generated code](#inspecting-generated-code) |
| `[qemu-jit] compile: module compilation failed: ... (module saved to ...)`, `[qemu-jit] background compile failed: ...` | wasmtime rejected a TB module | [Generated code](#inspecting-generated-code) |
| `[qemu-wasi] TB buffer flush #N (...)` | The code buffer filled up and every translation was dropped (first 10 flushes, then every 10th) | [Slow](#slow) |
| `[qemu] moved QEMU to a fresh store: ...`, `[qemu] vCPU N: moved to a fresh store, ...` | Normal: m-browser freed compiled TB modules. Suspect it only if failures follow it | [Tiers](#isolating-the-tier) |
| `[gfx] no swap chain texture (...); skipping frame` | The host's display takes no frames (screen blanked); harmless | [Case 7](#7-a-screen-saver-killed-the-vm) |

**Note:** the `0x...` in `TB` messages is the TB's address in QEMU's code
buffer (`tb->tc.ptr`), not a guest address. `-d exec` prints both:
`Trace 0: 0x<code buffer> [<cs_base>/<guest pc>/<flags>/<cflags>]`.

## The tool catalogue

*Single* is the `make` flavor, *threads* the `make THREADS=1` one. Only
variables named `QEMU_*` or `WASI_*` reach QEMU; set them on m-browser's
command line. QEMU's options go after the module path.

| Tool | What it shows | How to enable | Cost | Flavor |
|---|---|---|---|---|
| `WASI_DEBUG=1`, `=2` | 1: start-up breadcrumbs (`WASI_DBG:` lines from `qemu_init()`, block devices, reset), display updates, every key event (`wasi-gfx:`). 2 adds sampled per-TB tracing (`[WASI_TB]`, `WASI_HI`, `WASI_RR`) | Environment | 1: small; 2: large | both |
| `QEMU_STATUS_EVERY=<s>` | vCPU 0's registers, 96 code bytes around pc, 32 stack words, the last 64 TBs, the interpreter's counters | Environment | Per-TB hooks: slower guest | both |
| `QEMU_DUMP_AT=<pc>,<n>,<addr>,<len>,<path>` | Guest physical memory and CPU state at the n-th run of the TB at pc | Environment, with `-d nochain` | Per-TB hooks | both, native |
| `QEMU_LOOP_DIAG` | Logs guest code, TCG ops and CPU state once one TB has run 20,000,000 times in a row | Environment (presence), with `-d nochain` | Per-TB hooks | both, native |
| `QEMU_TB_DUMP=<pc>,<dir>` | The wasm module of the TB at guest pc | Environment, with `QEMU_JIT_HOT=0` | None | both |
| `QEMU_JIT_DUMP=<dir>[,<n>]` | Every n-th module m-browser compiles (default 100), as `tb-NNNNNNN.wasm`, in a host directory | Environment (m-browser) | Disk | both |
| `QEMU_JIT_HOT=0`, `=4000000000` | Which tier is at fault: JIT only, interpreter only | Environment | Slower | both |
| `QEMU_PROF_EVERY=<s>` | Time in translation, compiles, each tier, main loop, idle; memory size | Environment | Clock reads | single prints it |
| `QEMU_PROFILE=1` | Sampling profile of QEMU's functions | Environment (m-browser) | Moderate | single only |
| `QEMU_JIT_STATS=1` | JIT host statistics every minute | Environment (m-browser) | None | both |
| `QEMU_DISPLAY_STATS` | What display refreshes cost | Environment (presence) | Small | both |
| `WASI_GFX_FULL_REFRESH` | Whole-screen redraws, ruling out change tracking | Environment (presence) | High | both |
| `QEMU_NET_DEBUG=1` | Every packet m-browser's user network handles | Environment (m-browser) | Log volume | both |
| `RUST_BACKTRACE=1` | Backtraces of m-browser panics | Environment | None | both |
| `-d <items> -D <file>` | `in_asm` (guest code, as hex here), `op`, `op_opt` (TCG ops), `exec` (TBs entered), `cpu`, `int` (interrupts, exceptions), `cpu_reset`, `guest_errors`, `unimp` | QEMU options; `-dfilter <start>+<len>` limits `in_asm`, `op*`, `exec` and `cpu` to an address range | `exec`, `cpu`, `int`: huge | both |
| `-d nochain` | Every TB returns to `cpu_exec()`, so the per-TB hooks and `-d exec` see it; also a test of TB chaining | QEMU option | Slower | both |
| `-accel tcg,one-insn-per-tb=on` | One guest instruction per TB | QEMU option, or HMP `one-insn-per-tb on` | Much slower | both |
| HMP monitor | Registers, memory, devices, run control, `log` at run time | GUI: Ctrl+Alt+2 | None | both |
| Serial console | The guest kernel's messages | GUI: Ctrl+Alt+3; `-serial file:` | None | both |
| Native QEMU | Reference behaviour; gdb works there | `make native` | One build | native |
| Instruction test | Per-instruction-form results against native QEMU | `make test` | Minutes | both |
| `tools/perf/guest-shell.sh` | A root shell in the guest, no password | Script | One boot | both |
| `debugfs -c` | Files on the guest's disk, from the host | e2fsprogs | None | host |

**Note:** some usual tools do not work here. QMP, QEMU's JSON control
protocol: its dispatcher coroutine is never created on WASI, as coroutines
cannot suspend (patch 0007, `monitor/monitor.c`); HMP, the text monitor,
works. gdbstub: no workflow is known to work (untested); `-gdb tcp::1234`
needs a socket, and every socket function in `wasi-stubs/socket_stubs.c`
fails with `ENOSYS`, so use gdb with the native QEMU. From reading the
code: `-d out_asm` most likely hangs, as `disas()` in `disas/disas-host.c`
never advances when `print_insn_tci()` returns 0; and with no capstone
(the disassembler library) in either build, `-d in_asm` prints hex
(`OBJD-T:` lines) and the monitor's `x/16i $pc` answers "Asm output not
supported on this arch" (`monitor_disas()`): disassemble on the host
([below](#the-monitor)). `-icount` is untested and probably broken: patch
0003 changed `accel/tcg/cputlb.c` assuming it is off.

### Isolating the tier

The default mode mixes three things; these settings take them apart:

| Setting | Effect |
|---|---|
| `QEMU_JIT_HOT=4000000000` | Interpreter only (except TBs it cannot decode) |
| `QEMU_JIT_HOT=0` | JIT only: every TB compiled before its first run |
| `QEMU_JIT_ASYNC=0` | A hot TB is compiled synchronously, after exactly `QEMU_JIT_HOT` runs, instead of whenever a background compile finishes |
| `QEMU_JIT_WARM` at or above `QEMU_JIT_HOT` | No reloads of modules the host still has |
| m-browser's `QEMU_MIGRATE_AFTER=<small n>` | Frequent Store moves and handle resets, to provoke bugs there |

### A debugging run

The headless runner uses the same wasmtime host as the GUI, without a
window. Run it from the m-browser directory: like the GUI, it preopens
`pc-bios/` as `/bios` and the current directory as `/data`;
`--preopen HOST=GUEST` adds more. Work on a copy of the disk image.

```sh
# from the m-browser directory
mkdir -p /tmp/dbg && cp m.qcow2 /tmp/dbg/test.qcow2
QEMU_STATUS_EVERY=30 QEMU_SNAPSHOT=/tmp/dbg/screen.png \
./target/release/m-browser --qemu-headless --preopen /tmp/dbg=/out \
    ../qemu-wasi/qemu-build/qemu-system-x86_64 \
    -L /bios -drive file=/out/test.qcow2,format=qcow2 -m 1024 \
    -accel tcg,tb-size=256 -device qemu-xhci -device usb-tablet \
    -serial file:/out/serial.log -nic user 2>&1 | tee /tmp/dbg/run.log
```

These are the GUI's arguments, except that the serial port goes to a file
and `-monitor vc` is left out ([below](#the-monitor)). Add `-d <items> -D
/out/qemu.log` for QEMU's logs. `QEMU_SNAPSHOT` saves the screen about once
a second, `QEMU_QUIT_AFTER=<s>` quits cleanly, `QEMU_TYPE` and
`QEMU_TYPE_FIFO` type into the guest (m-browser's `src/headless.rs`). The
threads build, `qemu-build-threads/qemu-system-x86_64`, gets 4 vCPUs (2 on
small hosts) unless you pass `-smp`.

**Pitfall:** never point the headless runner at the image the GUI is
running: it takes no lock, and two QEMUs writing one qcow2 file corrupt it.

## Symptoms

### The build fails

- **Link errors after a rebase or a `meson.build` change** (wasm-ld rejects
  `--start-group`): `make` again ([10](10-changing-qemu.md#troubleshooting)).
- **Only one flavor fails**: they compile different `#ifdef WASI_THREADS`
  paths of the same files. Build both after every change.
- **A missing function is not a link error**: it becomes an import, and
  m-browser refuses the module ("unknown import"). Compare the imports
  with [reference/host-interface.md](reference/host-interface.md):

  ```sh
  # from the qemu-wasi root
  wasm-tools print --skeleton qemu-build/qemu-system-x86_64 | grep '(import "env"'
  ```

- **A configure check failed silently**: meson turns a failed test into a
  missing feature ([case 4](#4-slow-with-several-vcpus)). After changing
  the toolchain or a `meson.build`, read `qemu-build*/config-host.h` and
  the summary at the end of `qemu-build*/meson-logs/meson-log.txt`.

### QEMU exits at startup

`QEMU exited during startup with status N`: QEMU rejected its command line
or could not open something, and said why in the `qemu-system-x86_64:`
line above. It sees only preopened directories, so the usual culprit is a
path: start m-browser from its own directory (for `/data/m.qcow2` and
`/bios`) and preopen anything else. Socket options (`-gdb`, socket
netdevs) cannot work, nor can features the build leaves out
(`scripts/configure.sh` passes `--without-default-features`); `-monitor
stdio` is untested. If QEMU stops silently or traps while starting, rerun
with `WASI_DEBUG=1`: the last `WASI_DBG:` line names the phase reached
(`early_backends`, `configure_blockdev`, `init_board`, ...), and a
[backtrace](#reading-a-wasm-backtrace) names the function.

### The guest hangs during boot

First tell slow from stuck. m-browser's smoke test expects the desktop
after about 2.5 minutes (threads build, 4 vCPUs, fast host); the README
gives 5 minutes to X and 7 to the desktop. Then locate the vCPU with
`QEMU_STATUS_EVERY=10` ([output
format](reference/environment-variables.md#qemu_status_every-output)):

- **RIP moves and the total in the `TB history` line grows**: the guest
  works. It is slow ([Slow](#slow)), or loops over many TBs.
- **The same TB, dump after dump**: a tight loop. The code bytes and
  registers tell what it waits for (`pause` and a compare: a spin lock; an
  `in` from a port: polling a device). `QEMU_LOOP_DIAG=1` with `-d nochain`
  logs the loop once it has run 20,000,000 times in a row (the README and
  the comment above `tb_loop_diag()` say a million; the code says 20
  million); it catches only loops of a single TB.
- **`halted=1` every time**: the vCPU waits for an interrupt. Normal when
  idle; but if the TB total barely grows and the screen is still, no
  interrupts arrive: timers, devices or the main loop (patches 0003, 0004,
  0007; threads: `host_main_wake`, [08-threads.md](08-threads.md)).
- **The dumps stop** (single-threaded build): the vCPU never returns to the
  main loop. A host call blocks, or a TB loops inside one wasm call: one
  that jumps to itself through `goto_ptr` never ends its time slice
  ([07](07-tcg-backend.md#limits-and-rough-edges)).

RIP says which stage hangs. Below 0x100000 is the BIOS or the boot loader;
`0xffffffff8...` is the kernel. In the register dump `CS =0033` marks a
user program and `CS =0010` the kernel; kernel-mode code at other low
addresses early in the boot is the kernel's decompressor. If the kernel is
randomized (KASLR), map addresses to symbols with `/proc/kallsyms` from the
same boot, or boot with `nokaslr`. For the kernel's messages, see [the
serial console](#the-serial-console).

### The guest crashes or misbehaves

A kernel oops on the serial console (it names the function), a program that
segfaults (`dmesg` in the guest shows where), a wrong result:

1. Make it fail fast: one command in a [root
   shell](#a-root-shell-without-a-password) beats a desktop.
2. Run the tier experiment, then the native QEMU with the same arguments,
   to place the bug in a layer ([table](#the-method)).
3. Both tiers fail, native QEMU does not: generated code, a helper or a
   device. `-accel tcg,one-insn-per-tb=on` makes every TB one instruction:
   if the bug goes away, it lies in how a TB's instructions interact (values
   carried in locals from one to the next); if not, one instruction is
   translated wrongly, internal branches included (as in `rep`).
   `-d nochain` tests chaining the same way.
4. `-d int` logs interrupts and exceptions with the CPU state: `v=0e` is a
   page fault (CR2 holds the address), `v=0d` a general protection fault,
   `v=06` an invalid opcode. Timer interrupts make it big, and `-dfilter`
   does not apply. A guest that resets in a loop has probably
   triple-faulted: `-no-reboot -d int,cpu_reset` stops at the first reset.
5. Once you know the TB, give the same `QEMU_DUMP_AT`, with `-d nochain`,
   to both QEMUs to dump the same memory at the same execution
   ([syntax](reference/environment-variables.md#qemu_dump_at-syntax)); it
   lines up only where the guest runs identically, as in bare-metal tests
   and early boot. Then [inspect the TB](#inspecting-generated-code).

### The instruction test fails

`make test` lists the instruction forms whose results differ. Rerun in the
three modes (default, `QEMU_JIT_HOT=0`, `QEMU_JIT_HOT=4000000000`): all
failing points at code generation or a helper, one failing at that tier,
the default mode alone at [tier switching](#isolating-the-tier). A
difference only in the header line, `x86-insn-diff: cpuid...`, is a CPU
configuration difference ([case 8](#8-the-header-line-of-the-instruction-test)).
Fewer wasm lines than native ones: the wasm run trapped or timed out (read
`wasm-run.log`). [13-testing.md](13-testing.md#when-the-instruction-test-fails)
has the full table and the commands to find and dump the failing TB.

### A wasm trap

QEMU did something WebAssembly forbids, or called `abort()`, and wasmtime
printed a backtrace: see [below](#reading-a-wasm-backtrace).

### Slow

Compare with the reference times first. Usual causes: m-browser built
without `--release`; a debugging switch left set (`QEMU_STATUS_EVERY`,
`QEMU_DUMP_AT`, `QEMU_LOOP_DIAG`, `WASI_DEBUG=2`); Xorg's glamor in the
guest (README); frequent `TB buffer flush` messages (raise `-accel
tcg,tb-size=`); a guest that swaps. Then measure: in the single-threaded
build, `QEMU_PROF_EVERY=10`
([output](reference/environment-variables.md#qemu_prof_every-output))
splits the time between translation, compiles, each tier and the main
loop, and `QEMU_PROFILE=1` names functions. Slowness that grows with the
vCPU count points at stop-the-world sections
([case 4](#4-slow-with-several-vcpus)); tuning is in
[15-performance.md](15-performance.md).

### Display or input problems

- **Black screen**: the guest blanked it (move the mouse), or another
  console is shown (Ctrl+Alt+1 is the guest's; see `wasi-gfx: console N`).
- **Stale or garbled areas**: rerun with `WASI_GFX_FULL_REFRESH=1`. If that
  cures it, change tracking is at fault (the display device's dirty
  regions, `display/wasi_gfx_dcl.c`), not drawing.
- **Lost keys**: with `WASI_DEBUG=1`, each key event reaching QEMU prints
  `wasi-gfx: key evdev=...`. Printed but lost: the guest is busy or
  swapping ([case 5](#5-lost-keystrokes)). Not printed: the host side
  (m-browser's docs/09-display-and-input.md).
- **Pointer off or dead**: the absolute pointer needs `-device qemu-xhci
  -device usb-tablet`; `info usb` and `info mice` list the devices.
- **Sluggish screen**: `QEMU_DISPLAY_STATS` reports the refresh cost, which
  the single-threaded build takes from the guest. More in
  [09-display-input-network.md](09-display-input-network.md).

### Network problems

Walk from the guest outwards. In the guest: `ip addr` (expect 10.0.2.15),
`ping -c 3 10.0.2.2` (the gateway, answered by m-browser), `nslookup
example.com 10.0.2.3`, `wget -O /dev/null http://example.com`. In the
monitor, `info network` shows the NIC and the user network "provided by the
WASI host". On the host, m-browser's `QEMU_NET_DEBUG=1` logs every packet
it handles. Sent but absent from that log: lost in QEMU (`net/`, patch
0006). Logged but unanswered: m-browser or the outside world (for pings,
the host's `net.ipv4.ping_group_range`); `cargo test --release --offline
qemu_net` runs m-browser's network tests.

### Problems only in the threads build

1. Run the threads build with `-smp 1`. If the bug goes away, it needs
   several vCPUs: a race, a missing barrier or atomic, a stop-the-world
   section. If not, it is in code only this flavor compiles (`#ifdef
   WASI_THREADS`): vCPU threads (`host_vcpu_run`, `wasi_vcpu_step()`),
   per-vCPU JIT handles and Store moves, the main loop's wake-up, the RCU
   thread, the timer slack ([08-threads.md](08-threads.md)).
2. Check `CONFIG_ATOMIC64` in `qemu-build-threads/config-host.h`.
3. Rule out the timer slack with `QEMU_TICK_SLACK_MS=0`.

The tools are weaker here: `QEMU_PROF_EVERY` never prints, `QEMU_PROFILE`
does not work, `QEMU_STATUS_EVERY` shows vCPU 0 only (read while it runs),
`QEMU_DUMP_AT` and `QEMU_LOOP_DIAG` count all vCPUs together, statistics
are approximate. A trap or `exit()` on any thread ends the whole process
(`[qemu] vCPU N failed`). In the monitor, `info registers -a` shows every
vCPU and `cpu N` selects one. Per-thread CPU use, gdb backtraces of
m-browser's threads and a table of deadlock symptoms are in
[08-threads.md](08-threads.md#debugging-threads).

## Case studies

Eight bugs from this project. Where the history records only the cause and
the fix, the narrowing is how you would find it today.

### 1. "push; ...; rep movsl" pushed twice

**Symptom.** Code like "push; ...; rep movsl" pushed twice and later
returned to a bogus address; "mov ecx,ebx; rep movsb" looped forever.
**Narrowing.** To one TB with `QEMU_DUMP_AT` (the stack at the suspect
code, against native QEMU) and `QEMU_TB_DUMP`, then reading that TB's wasm.
**Cause.** In the dispatch loop
([07](07-tcg-backend.md#the-generated-function-block-dispatch)), block 0's
guard was `if (1)` and labels were numbered from 0, so a backward branch to
the first label re-ran the TB from its start, pushes included; `rep`
instructions branch back to their exit label when ECX reaches 0.
**Fix.** The guard is `BLOCK_PTR <= 0`, and `wasm_reset_state()` numbers
labels from 1. **Lesson.** Once the bug is in one TB, read its wasm.

### 2. cmpxchg16b stored to the wrong address

**Symptom.** `cmpxchg16b` wrote to an address equal to the data it had
loaded. **Narrowing.** Both tiers fail: code generation. `-d op_opt` shows
the ops around the helper call, the wasm which locals the call overwrites.
**Cause.** A helper returning 128 bits (`Int128`) fills A0 and A1, but
only A0 was declared clobbered by calls (`tcg_target_init()`), so TCG kept
the store address in A1 across the call. **Fix.** Both are clobbered, as
in upstream TCI. **Lesson.** What survives a call is a contract with TCG's
register allocator; the test's `cmpxchg16b` form now covers it.

### 3. The upper bits of 32-bit operations

**Symptom.** 32-bit instructions went wrong for some operands.
**Narrowing.** The instruction test, whose special operands set the upper
halves. **Cause.** Values live in i64 locals and 32-bit ops are mostly
emitted as 64-bit ones, so the upper half of an i32 value is garbage; ops
that depend on it (clz, ctz, extract, deposit, multiplications, shifts,
division, ctpop, byte swaps, add2/sub2 carries) were wrong. **Fix.** Each
wraps or extends its inputs ([07](07-tcg-backend.md#values)).
**Lesson.** Test emitters with operands whose upper bits are set.

### 4. Slow with several vCPUs

**Symptom.** With 2 or more vCPUs the threads build crawled. **Narrowing.**
Status dumps showed the boot stuck in the kernel's decompressor. With a
second vCPU, TCG must keep guest accesses atomic, and those the host cannot
do atomically go through `cpu_loop_exit_atomic()` and
`cpu_exec_step_atomic()`, which stop every other vCPU. For 8-byte accesses
that depends on `CONFIG_ATOMIC64` (`HAVE_al8` in
`accel/tcg/ldst_atomicity.c.inc`), which `config-host.h` showed undefined.
**Cause.** Meson's link test failed on wasm-ld for an unrelated reason
(`--start-group`). **Fix.** `meson.build` sets `CONFIG_ATOMIC64` for wasm32
with atomics. **Lesson.** If speed falls with more vCPUs, look for
exclusive sections; check `config-host.h` for lost features.

### 5. Lost keystrokes

**Symptom.** Typed characters went missing. **Narrowing.** The guest, with
128 MB, was swapping and its input stalled; events then came in bursts,
and input devices buffer about 16 entries (PS/2, USB keyboard, text
consoles). `WASI_DEBUG=1` tells keys that never reached QEMU from keys
lost after it. **Fix.** `-m 512`, then 1024 (for Chromium), and at most
`KEYS_PER_POLL` (6) key events per display refresh
(`display/wasi_gfx_display.c`). **Lesson.** Check the guest's health
(`free`, `top`) before blaming the emulator.

### 6. An RCU leak

**Symptom.** In the single-threaded build, memory QEMU should have freed
never was. **Narrowing.** Watch `memory` in `QEMU_PROF_EVERY` under a
steady workload: it should level off. **Cause.** Callbacks queued with
`call_rcu()` (old TB hash tables, FlatViews, ...) run on a thread of their
own, which never runs without threads. **Fix.**
`rcu_run_callbacks_inline()` (`util/rcu.c`), called by
`qemu_main_loop_step()`. **Lesson.** Whatever QEMU hands to a thread is
suspect in the single-threaded build; patch 0004 lists them.

### 7. A screen saver killed the VM

**Symptom.** Once the host's screen had blanked, m-browser panicked with
`Failed to acquire next swap chain texture: Timeout`, taking the guest
along. **Narrowing.** The panic is m-browser's window code, not QEMU's: the
graphics library timed out waiting for an image to draw into
(`RUST_BACKTRACE=1` shows a panic's call path). **Fix.** m-browser skips
such frames. **Lesson.** Read who printed the last message.

### 8. The header line of the instruction test

**Symptom.** In the threads build only, the test's first line,
`x86-insn-diff: cpuid...`, differed. **Cause.** m-browser gives that build
4 vCPUs unless `-smp` is given, and with several vCPUs CPUID leaf
0x80000001 sets its CmpLegacy bit, which the line prints (from reading
`cpu_x86_cpuid()` in `target/i386/cpu.c`). **Fix.** `run.sh` passes
`-smp 1`. **Lesson.** Compare configurations before code.

## Inspecting generated code

When the evidence points at one TB:

1. **Find its guest address**: the status dump's TB history, `-d exec`
   (`Trace` lines), `-d in_asm` (`IN:` blocks), or `objdump -d` of a test.
2. **Dump its module** with `QEMU_TB_DUMP=<pc>,/out` and `QEMU_JIT_HOT=0`,
   which writes it on the first run (`QEMU_TB_DUMP: wrote ...`); otherwise
   only once the host is asked for the module, at 50 runs and when hot.
   A module that fails a synchronous compile is saved by m-browser as
   `qemu-jit-failed.wasm` in the host's temporary directory; add
   `QEMU_JIT_ASYNC=0` if the failure was a background compile.
3. **Validate and print it**:

   ```sh
   # in the directory the dump was written to
   wasm-tools validate tb_ffffffff81234560.wasm
   wasm-tools print --print-offsets tb_ffffffff81234560.wasm
   ```

   A validation error is a backend bug; its `(at offset 0x...)` matches
   the printed offsets. `[wasi-interp] ... is not valid wasm` reports the
   same kind of error, counting from the start of the function body.
4. **Get what the backend was given**: `-d in_asm,op_opt -D /out/qemu.log
   -dfilter <pc>+0x100` logs that TB's guest code and optimized TCG ops.
   Turn the hex into instructions with
   `qemu-src/scripts/disas-objdump.pl -t i386:x86-64 qemu.log` (untested
   here).
5. **Read the wasm against the ops**
   ([07-tcg-backend.md](07-tcg-backend.md)): the prologue, the `loop`, an
   `if` per block guarded on local 32 (`BLOCK_PTR`), TLB fast paths,
   `call_indirect` for helpers. Look for a branch to the wrong block, a
   call type unlike the helper's C prototype, a value used without
   `i32.wrap_i64` or an extension, a local a call overwrote.

A TB reported as `not valid wasm` is never offered to the host, so neither
dump switch saves it: add a temporary `fwrite()` of the module (header and
body, as `compile_and_cache_tb()` joins them) to the type-error path of
`interp_decode()`, and do not commit it. `QEMU_JIT_DUMP=/tmp/jit,1` saves
every compiled module, to validate or study many at once; files are named
by sequence number, not guest address.

## Reading a wasm backtrace

On a trap, m-browser prints the error and a backtrace, innermost frame
first. Illustrative and shortened (the layout varies with the wasmtime
version):

```text
[qemu] [ 412.311s] step failed: error while executing at wasm backtrace:
    0: 0x9f3c1 - abort
    1: 0x4b2a7 - interp_trap
                    at .../qemu-src/tcg/wasm32/wasi-interp.c.inc:<line>:<column>
    2: 0x4c9d2 - interp_exec
    3: 0x51f80 - tcg_qemu_tb_exec
    4: 0x2e115 - cpu_exec
    ...
Caused by:
    wasm trap: wasm `unreachable` instruction executed
```

1. Read the cause at the bottom:

   | Trap | Usual meaning here |
   |---|---|
   | ``wasm `unreachable` instruction executed`` | `abort()`, a failed `assert()` or `g_assert()` (message printed just before), or `unreachable` in a TB |
   | `out of bounds memory access` | A pointer outside QEMU's memory: a bad address from TB code, or corrupted data |
   | `indirect call type mismatch` | A function pointer called with the wrong type: a helper type in a TB, or a mis-cast pointer in C |
   | `uninitialized element`, `undefined element` | A stale or bogus table index, e.g. a JIT handle kept across a Store move |
   | `integer divide by zero`, `integer overflow` | A wasm division trapped; guest divisions must be checked before they get there |
   | `call stack exhausted` | Deep recursion, or a thread stack too small (patch 0008 gives threads 16 MiB) |

2. Skip `abort`, `__assert_fail` and GLib's assertion helpers; the first
   QEMU function after them is where to start. `assert()` is active in
   these builds; TCG's `tcg_debug_assert()` is not (`CONFIG_DEBUG_TCG` is
   off), so TCG's internal limits go unchecked
   ([07](07-tcg-backend.md#limits-and-rough-edges)).
3. A frame without a useful name (`<wasm function 0>`) called from
   `tcg_qemu_tb_exec` is a compiled TB. Innermost, the generated code
   itself trapped: dump that TB. An innermost helper failed by itself or
   was handed bad values by the TB.
4. File and line come from DWARF (QEMU is built with `-O2 -g`); with
   m-browser's `QEMU_PROFILE` set, only names remain.

## Working on the guest side

### The monitor

In the GUI, Ctrl+Alt+2 shows QEMU's monitor (HMP); Ctrl+Alt+1 returns.
Headless, add `-monitor vc` and show it with `WASI_GFX_CONSOLE=1` (without
that variable, QEMU lists the consoles at start-up: `wasi-gfx: console N
is ...`); type with `QEMU_TYPE='60:info registers\n'` and read the
`QEMU_SNAPSHOT` image.

| Command | Use |
|---|---|
| `info status`, `stop`, `cont` | Whether the guest runs; freeze it to look, resume |
| `info registers`, `info registers -a`, `cpu N` | The current vCPU's registers, all vCPUs', select one |
| `x/32xb $pc`, `x/8xg $sp` | Guest virtual memory: code bytes, stack |
| `gva2gpa <addr>`, `xp/16xg <paddr>` | Virtual to physical (`QEMU_DUMP_AT` takes physical addresses); physical memory |
| `info tlb`, `info mem`, `info mtree` | The guest's page mappings; the physical memory map |
| `pmemsave <paddr> <size> <file>` | Physical memory to a file in the sandbox |
| `log in_asm,int`, `log none`, `logfile <file>` | QEMU's `-d` logging, switched at the right moment |
| `info network`, `info usb`, `info mice`, `info block` | Devices |
| `sendkey alt-sysrq-t` | Linux's SysRq: task states to the kernel log, if the guest enables SysRq |

To disassemble code bytes, take them from a status dump (the bytes after
the `|`, which start at pc) or from `x/32xb $pc` (without the addresses and
`0x` prefixes):

```sh
# anywhere on the host; --adjust-vma is the address of the first byte
echo '55 48 89 e5 41 57' | xxd -r -p > /tmp/code.bin
objdump -D -b binary -m i386:x86-64 --adjust-vma=0xffffffff81234560 /tmp/code.bin
```

### The serial console

Ctrl+Alt+3 in the GUI; headless, `-serial file:/out/serial.log`. It shows
the kernel's messages only if the kernel command line (`/proc/cmdline`)
has `console=ttyS0`.

### A root shell without a password

`tools/perf/guest-shell.sh` boots the guest's own kernel with
`init=/bin/sh` on a throwaway overlay, with the root filesystem writable
and the network up. Run `tools/perf/prepare-base.sh` first: it copies
m-browser's `m.qcow2` to a raw image and extracts the kernel, initramfs
and command line. `QEMU_WASM` picks the build (default: m-browser's).

```sh
# from the qemu-wasi root
tools/perf/prepare-base.sh
tools/perf/guest-shell.sh dbg
# then, from another terminal in the same directory:
echo 'dmesg > /dev/ttyS0' > tools/perf/work/type-dbg.fifo
```

What the guest writes to `/dev/ttyS0` lands in
`tools/perf/work/serial-dbg.log`, the screen in `screen-dbg.png`. Kernel
options such as `nokaslr` go into `tools/perf/work/cmdline`
(`prepare-base.sh` rewrites it when run again).

### Reading the disk image from the host

`debugfs` (e2fsprogs, often in `/sbin`) reads an ext4 filesystem inside a
raw image, given the partition's byte offset; `-c` opens it read-only,
without reading the allocation bitmaps:

```sh
# from the qemu-wasi root; base.raw comes from prepare-base.sh
fdisk -l tools/perf/work/base.raw    # partition starts, in 512-byte sectors
/sbin/debugfs -c -R 'cat /var/log/messages' \
    'tools/perf/work/base.raw?offset=<start sector * 512>' > messages
```

**Pitfall:** `base.raw` is a copy from when `prepare-base.sh` last ran;
rerun it with `FORCE=1`, while m-browser is not running the image, for
current contents. Do not run the host's `e2fsck` on the image: it may be
too old for the image's ext4 features.
