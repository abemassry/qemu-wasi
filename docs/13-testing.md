# Testing

**What this covers.** The tests of this project: the x86 instruction test
(`make test`), the boot and responsiveness scripts in `tools/perf/`,
m-browser's network unit tests, and the manual smoke test in m-browser's
window: what each catches, how to run it, how to read its output. Then
what to run before committing each kind of change, how to add an
instruction form, what to do when the instruction test fails, and how to
read boot timings. Debugging is in [14-debugging.md](14-debugging.md),
speed work in [15-performance.md](15-performance.md), terms in
[reference/glossary.md](reference/glossary.md).

## Overview

| Test | What it catches | How long | When to run |
|---|---|---|---|
| Instruction test, `make test` (`tests/x86-insn-diff/`) | x86 instructions computing wrong results in the wasm build: TCG backend, interpreter, m-browser's JIT host, helpers miscompiled for wasm | Seconds per run | Every QEMU change; all three modes and both flavors for TCG, JIT and threads changes |
| Boot timing, `tools/perf/boot-timing.sh` (with `click-latency.sh`) | A guest that no longer boots, boots slower or answers clicks late; what only a real workload shows (devices, timers, several vCPUs) | 15 minutes per run, plus a few minutes once for `prepare-base.sh` | Speed, threads, main loop and JIT tier changes; QEMU upgrades |
| Guest benchmark, `tools/perf/bench-run.sh` | Slowdowns in particular kinds of guest work | About 10 minutes | Performance work |
| m-browser's unit tests, `cargo test --release --offline qemu_net` | The host side of the user network: packets, DHCP, TCP relaying, port forwarding | Seconds, after a first build | Network changes in either repository |
| Manual smoke test in m-browser's window | Display, keyboard, mouse, consoles, guest network, Chromium, clean shutdown, a healthy disk image | 10-15 minutes | Before committing anything that reaches QEMU |

Only the instruction test and m-browser's unit tests decide pass or fail by
themselves; the others are measurements and checklists that you judge. A
*flavor* is one of the two builds: single-threaded (`make`, in
`qemu-build/`) or multi-threaded (`make THREADS=1`, `qemu-build-threads/`).

## The instruction test

### Why it is built this way

An *instruction form* is one instruction with one operand size and kind of
operand: `addl` is a 32-bit add of two registers, `shlq$63` a 64-bit shift
left by the constant 63. The test runs 351 forms, each on 1024 inputs, and
prints one hash of all the results per form.

It is *differential*: the same program runs under a native QEMU, built by
`make native` from the same patched tree into `qemu-native/`, and under the
wasm QEMU; the outputs must be identical. Both translate x86 code with
TCG, QEMU's code generator, using the same front end (which decodes x86)
and the same helpers (C functions the generated code calls), but different
backends (which emit host code): the mature x86-64 one, `tcg/i386`,
against `tcg/wasm32` with its interpreter and m-browser's JIT host, which
compiles the wasm. A difference points at the wasm side, and there is no
file of expected values to maintain.

The program is *bare-metal*: it runs on the emulated PC without an
operating system, so it starts in milliseconds, nothing runs beside it and
its output depends only on the CPU emulation. It uses QEMU's own test
scaffolding in `qemu-src/tests/tcg/`:

- `x86_64/system/boot.S`: QEMU's `-kernel` recognises its *PVH* note (an
  ELF note of Xen's boot protocol) and starts it at `_start` in 32-bit
  mode; boot.S switches to 64-bit mode (identity page table, 64 KiB
  stack), calls `main()`, and when that returns powers the machine off
  (ACPI port 0x604), which ends QEMU. `kernel.ld` loads it at 1 MiB.
- `minilib`: `ml_printf()` (`%s %c %d %u %x %o`, `l`, `ll`, no widths)
  writes each character with boot.S's `__sys_outc`, an `out` to I/O port
  0xE9, which QEMU's *debugcon* device (`-device isa-debugcon`) appends
  to a file.

### Running it

Needs a gcc that produces x86-64 code (`run.sh` calls plain `gcc`), the
native QEMU (`make native`, which needs QEMU's usual build dependencies
such as the glib development package), m-browser built with
`cargo build --release` in `../m-browser`, and the wasm build to test.

```sh
# from the qemu-wasi root
make native                          # once; again after non-WASI changes
make test                            # single-threaded build, qemu-build/
make THREADS=1 test                  # multi-threaded build, qemu-build-threads/
QEMU_JIT_HOT=0 make test             # JIT only
QEMU_JIT_HOT=4000000000 make test    # interpreter only
```

A good run ends with `native: 353 lines, wasm: 353 lines` and
`PASS: all instruction hashes match` (353 lines: a header line, the 351
forms and `x86-insn-diff: done`).

The modes differ in who runs each translation block (TB):

| Mode | `QEMU_JIT_HOT` | Who runs each TB | Tests mainly |
|---|---|---|---|
| Default | unset (5000) | The interpreter until the TB has run 5000 times, then its module, compiled in the background | The forms in the interpreter, shared code compiled, the switch between tiers |
| JIT only | `0` | The module m-browser compiled (Cranelift) before the TB's first run | The backend's code as compiled, the JIT host, calls into compiled TBs |
| Interpreter only | `4000000000` | QEMU's interpreter, `tcg/wasm32/wasi-interp.c.inc` (TBs it cannot decode are still compiled) | The interpreter |

**Note:** a form's own code runs about 1024 times, below the default
threshold, so in the default mode it stays in the interpreter; only code
shared by all forms (such as the operand generator) gets compiled. So
`make test` alone hardly tests compiled code; `QEMU_JIT_HOT=0` does.

**Pitfall:** `QEMU_JIT_HOT=` (set but empty) reads as 0, JIT only. For the
default mode the variable must be unset, in your shell's environment too.

```sh
# from the qemu-wasi root: all six combinations, each with its own OUT
for t in 0 1; do
    OUT=/tmp/insn-$t-default make THREADS=$t test
    OUT=/tmp/insn-$t-jit QEMU_JIT_HOT=0 make THREADS=$t test
    OUT=/tmp/insn-$t-interp QEMU_JIT_HOT=4000000000 make THREADS=$t test
done
```

| Variable | Default | Meaning |
|---|---|---|
| `QEMU_WASM` | `qemu-build/qemu-system-x86_64`; `make test` sets the selected flavor's | The wasm QEMU to test; any module, e.g. the installed `$M_BROWSER/qemu-system-x86_64.wasm` |
| `NATIVE_QEMU` | `qemu-native/qemu-system-x86_64` | The reference |
| `M_BROWSER` | `../m-browser` | Where `target/release/m-browser` is |
| `OUT` | `${TMPDIR:-/tmp}/qemu-wasi-insn` | Where each run writes, overwriting: `insn-test.elf` (and the copy the wasm run boots, `insn-test-wasm.elf`), `printf.o`, `native.txt`, `wasm.txt`, `diff.txt`, `wasm-run.log` (m-browser's and QEMU's messages) |
| `WASM_TIMEOUT` | 900 | Seconds before the wasm run is killed (the native run gets 120) |
| `QEMU_*`, `WASI_*` | | Reach the wasm QEMU through m-browser, and the native QEMU directly |

### What run.sh does

1. Compiles minilib's `printf.c` at `-O0` (boot.S's `__sys_outc` writes
   `%al` although the character arrives in `%dil`, which only works for
   `-O0` callers), then `boot.S` and `insn-test.c` at `-O1` with
   `-fno-tree-vectorize` and `-mno-red-zone` (the tests push inside `asm`
   statements, which would overwrite the *red zone*, the 128 bytes below
   the stack pointer where the compiler may keep locals), and links them
   with `kernel.ld`.
2. Runs the native QEMU, then the wasm QEMU through m-browser's headless
   runner from the m-browser directory, with `OUT` preopened as `/out`.
   The wasm run's exit status is ignored; only the outputs count.
3. Diffs `native.txt` and `wasm.txt` into `diff.txt`. On a difference it
   prints `FAIL: mismatching instruction forms:` and the differing native
   lines without their hash, that is the form names, and exits with 1.

| QEMU arguments (both) | Why |
|---|---|
| `-M pc -cpu max` | The standard PC, with every CPU feature TCG implements, so every optional form runs |
| `-m 64 -smp 1` | 64 MiB is plenty; one CPU (below) |
| `-no-reboot` | The program has no exception handlers: a fault ends in a CPU reset, which this turns into an exit |
| `-monitor none -serial none` (and `-display none` for native) | Nothing to look at |
| `-L` | The firmware of `qemu-src/pc-bios` for both; `/bios2` for the wasm QEMU, as m-browser preopens its own `pc-bios/` as `/bios` |
| `-kernel`, `-chardev file,...`, `-device isa-debugcon,...` | The program, and its output into a file |

**Pitfall:** without `-smp 1` the multi-threaded build fails. m-browser
adds `-smp 4` (`QEMU_SMP`; 2 on hosts with fewer than 8 CPUs) when the
arguments have none. A program learns the CPU's features with the *CPUID*
instruction, whose answers come in numbered *leaves*, and with several
CPUs QEMU sets the CmpLegacy bit (bit 1 of ECX in leaf 0x80000001) for
CPUs not claiming to be Intel; under TCG `-cpu max` claims AMD
(`cpu_x86_cpuid()`, `max_x86_cpu_initfn()` in `target/i386/cpu.c`). The
header line then ends in `ecx=177` instead of `ecx=175`, the only
difference. This is how the threads build first failed the test.

### Inside insn-test.c

`main()` enables SSE (`enable_sse()`: without CR4.OSFXSR every SSE
instruction faults), prints CPUID leaves 1, 7 and 0x80000001 as the
header line, and calls `test_alu()`, `test_shifts()`, `test_muldiv()`,
`test_bits()`, `test_strings()` and `test_sse()`.

- **`TEST(name) { ... } END`**: `TEST` opens a block, sets the hash `h` to
  its start value, resets the random generator `rng` to its seed, and
  loops `it` from 0 to `ITERS - 1` (1024), setting the inputs `a`, `b`,
  `c`, `d` (`operands()`) and the incoming flags `fi` (`flags_in()`). The
  body runs the instruction and feeds the results to `MIX`; `END` closes
  the loop and prints `name hash`. Every form gets the same inputs.
- **Inputs** (`operands()`): the first 676 iterations take every pair of
  the 26 values in `special[]`: zero and small numbers, both sides of each
  operand size's sign boundary (0x7f/0x80 up to 0x7fffffffffffffff/
  0x8000000000000000), all ones, and values whose 32-bit halves differ
  (0x100000000, 0x17fffffff, 0xffffffff00000000, 0x8000000080000000),
  which catch an upper half leaking into a 32-bit operation. The other 348
  are random (`next_rand()`, a xorshift generator), `b` cut to 8 bits on
  odd iterations, as small values matter for shifts, bit offsets, divisors.
- **Flags**: x86 keeps the outcome of arithmetic (carry, zero, sign,
  overflow, parity, auxiliary carry: CF, ZF, SF, OF, PF, AF) in its flags
  register. `flags_in()` cycles through none, CF only and all six (`0x2`
  is a bit that is always 1). `PRE` (`push %[fi]; popfq`) loads them and
  `POST` (`pushfq; pop %[f]`) saves them afterwards, so instructions that
  read the carry (`adc`, `sbb`, `rcl`, `adcx`) see both values, and those
  that must keep a flag (`inc` and `dec` keep CF) are checked for it.
- **Masks**: only flags the instruction defines are hashed: `FLAGS`
  (0x8d5, all six) for most forms, 0x801 (OF, CF) for `mul` and `imul`,
  none for `div` and `idiv`, 0x40 (ZF) for `cmpxchg16b`, 0x41 for `ptest`.
  So a mismatch always concerns a result that programs may rely on.
- **Hash**: `MIX(v)` is `h = (h ^ v) * 0x100000001b3` from
  0xcbf29ce484222325 (FNV-1a's 64-bit constants, on whole values). It runs
  on the emulated CPU too, so a broken basic operation changes every line.
- **No faults**, since one ends the program: divisions get inputs whose
  quotient fits (high half below an odd divisor; for `idiv` a sign-extended
  dividend, divisors 0 and -1 replaced by 7), `cmpxchg16b` aligned memory.
- **CPUID gating**: forms of optional features run only if their CPUID bit
  is set, since an unsupported instruction faults. Leaf 1 ECX: `popcnt`
  (bit 23), `movbe` (22), `crc32` and SSE4.2 (20), SSE4.1 (19), SSSE3 (9).
  Leaf 7 EBX: BMI1 (bit 3: `tzcnt`, `andn`, `bextr`, `blsi`, `blsmsk`,
  `blsr`), BMI2 (8: `bzhi`, `pdep`, `pext`, `sarx`, `shlx`, `shrx`, `rorx`,
  `mulx`), ADX (19: `adcx`, `adox`). Leaf 0x80000001 ECX: `lzcnt` (bit 5).
  `-cpu max` has them all.

Most forms are one macro call: `ALU2` (register op register), `ALUI` (op
constant), `ALU1` (one operand), `SHCL` (shift by CL), `SHDCL`
(`shld`/`shrd`), `MOV2` (destination only: `movzx`, `bsf`, `popcnt`),
`VEX3` (three-operand BMI), `SSE2`, `SSEI` (with a constant), `SSESH` (SSE
shift by a constant); `CONDS` lists the 16 conditions. Size arguments pick
register names with GCC's operand modifiers, `q` 64, `k` 32, `w` 16, `b` 8
bits (`MOV2` has two). `MULDIV` and `DV_ANY` are unused.

| Function | Forms | Contents |
|---|---|---|
| `test_alu()` | 60 | `add adc sub sbb and or xor cmp test` and `neg not inc dec`, each in four sizes; eight forms with constants |
| `test_shifts()` | 89 | `shl shr sar rol ror rcl rcr` by CL in four sizes and by constants; `shld`/`shrd` |
| `test_muldiv()` | 18 | One-, two- and three-operand `mul`/`imul`; `div` in four sizes, `idiv` in two |
| `test_bits()` | 64 + 35 gated | `bsf bsr bt bts btr btc bswap`, `movzx`/`movsx`, `cbw` and relatives, `lea`, `xadd`/`xchg`/`cmpxchg`, `cmpxchg16b`, 16 `setcc`, 16 `cmovcc`; `popcnt`, `lzcnt`, BMI1, BMI2, ADX, `crc32`, `movbe` |
| `test_strings()` | 2 | `rep movsb`, `rep stosq`, `repe cmpsb`, `repne scasb` on stack buffers (hashing offsets, not addresses); 64- and 16-bit `push`/`pop` |
| `test_sse()` | 62 + 21 gated | SSE2 integer arithmetic, compares, packing, unpacking, shifts, shuffles, moves; SSSE3, SSE4.1, SSE4.2 |

### What it has caught

These wasm-only bugs were found by the test (details in
[07-tcg-backend.md](07-tcg-backend.md#things-a-porter-must-preserve)):

- Operations that let in the upper 32 bits of 32-bit values, which the
  backend leaves as garbage: clz, ctz, extract, deposit, the double-width
  multiplies (mul2), shifts, division, ctpop, byte swaps, and the carries
  of add2/sub2.
- `cmpxchg16b`: its helper returns 128 bits in the backend's registers A0
  and A1, but calls were declared to clobber only A0, so the loaded data
  became the store address.
- `rep` string instructions branching back re-ran a TB's first block.
- Not a bug: the threads build's first run failed only on the CPUID header
  line (`-smp 1`, above).

### What it does not cover

Floating point (x87, SSE and AVX arithmetic), AVX, most memory-operand
forms, exceptions, interrupts, 16- and 32-bit modes, several vCPUs, the
devices, and long runs (TB buffer flushes, m-browser replacing its
wasmtime Store). And both QEMUs share the x86 front end and helpers: a bug
there gives the same wrong answer twice. The boot tests cover the rest.

## Boot and responsiveness tests (tools/perf)

These scripts boot the real guest, a copy of m-browser's `m.qcow2`
([16-guest-image.md](16-guest-image.md): Alpine, Xorg, Fluxbox, Chromium),
through m-browser's headless runner, and record what happens; options in
[tools/perf/README.md](../tools/perf/README.md), use for speed work in
[15-performance.md](15-performance.md). Settings (`common.sh`): `WORK`
(default `tools/perf/work`), `M_BROWSER`, `QEMU_WASM` (default the module
installed in m-browser, which the GUI runs: `make install` first), `MEM`
(1024 MiB), `SMP` (default m-browser's: 4 for the threads build on hosts
with 8+ CPUs, else 2; always 1 single-threaded); `QEMU_*`/`WASI_*` reach
QEMU.

| Script | What it does |
|---|---|
| `prepare-base.sh [DISK]` | Once: copies the image (default `$M_BROWSER/m.qcow2`, only read) to `WORK/base.raw`, extracts the guest's kernel, initramfs and command line from its `extlinux.conf`, and makes `perf-base.qcow2` with the guest logger installed. A few minutes; needs `qemu-img`, `debugfs` and python3; `FORCE=1` starts over |
| `boot-timing.sh NAME` | Boots like the GUI (1 GiB, `tb-size=256`, USB tablet, user network) on a throwaway overlay of `perf-base.qcow2` for `DURATION` seconds (900), then quits QEMU cleanly. Keeps `serial-NAME.log`, `run-NAME.log` and a screenshot every `SHOTS` seconds (60) in `shots-NAME/`, named by seconds since the start; prints `analyze-serial.py`'s summary. `CLICKS=1` also runs `click-latency.sh`; `KBOOT=1` starts the kernel directly (about 10 s faster, so not comparable) |
| `click-latency.sh NAME` | Waits until Chromium's toolbar is painted (`menupx.py find` sees its menu button), then three times clicks the button, measures until the menu is drawn (`menupx.py count`: newly dark pixels where it opens), clicking again after a minute without effect, and closes it. Writes `click-NAME.log`; `WAIT_IDLE=1` first waits for an idle guest. The detection fits Chromium's light theme at 100% zoom |
| `analyze-serial.py LOG` | Summarizes the guest logger's output: when Xorg, Fluxbox, Chromium and its first renderer were first seen, when the guest became idle, CPU use, swapping, timer interrupt rate |
| `bench-run.sh NAME [STEPS]` | Boots a root shell with the network up, serves `tools/perf/guest/` over HTTP and runs `guest/bench.sh` (`forkexec find bzip2 python chromium`); prints `NAME: BENCH <step> <seconds>`, in guest time |
| `guest-shell.sh [NAME]` | A root shell (kernel started with `init=/bin/sh`, no password) with the network up, typed into through `WORK/type-NAME.fifo` |
| `mkbase.sh NAME NEW_BASE COMMANDS_FILE [BASE]` | Types a file of commands into a root shell and keeps the result as a new base image, e.g. with `guest/chromium-fast.cmds` |

```sh
# from the qemu-wasi root, with the build to test installed in m-browser
tools/perf/prepare-base.sh                  # once per disk image
CLICKS=1 SHOTS=5 tools/perf/boot-timing.sh mt1
```

The guest logger (`guest/qemu-profile.start`, run by OpenRC at the end of
boot) writes the kernel's counters and `top`'s output to the serial port
every 10 s. Each run boots a fresh copy-on-write *overlay*
(`WORK/k-NAME.qcow2`) of the base image, so runs never change the base
and all start from the same disk state.

**Pitfall:** the headless runner takes no lock on the disk image, and two
QEMUs on one image corrupt it: never point the scripts at the GUI's image
(`prepare-base.sh` refuses to copy one in use). Overlays name their base
by its path inside QEMU's sandbox (`/out/...`), so the host's `qemu-img`
cannot follow them. `base.raw` is as large as the virtual disk (2 GiB
here). Git ignores the default `WORK` directory, `tools/perf/work/`.

## m-browser's network tests

```sh
# from the m-browser directory (--offline: the crates are already fetched)
cargo test --release --offline qemu_net
cargo test --release --offline qemu_net::tests::tcp_relays_both_ways \
    -- --nocapture                    # one test, with its output
```

Eleven tests (`qemu_net` matches all of m-browser's) of its user network:
fragments, checksums, DHCP options, TCP relaying, refused and reset
connections, half close, parallel connections, `hostfwd`. No network
access or privileges needed: the TCP tests run a small guest-side TCP/IP
stack against m-browser's network on 127.0.0.1. QEMU's network backend
(patch 0006) is not involved. More in the m-browser repository's
docs/14-testing-and-debugging.md and docs/10-networking.md.

## The manual smoke test

There is no automated test of the GUI. Install the flavor to test
(`make install` or `make THREADS=1 install`; both write the same file),
build m-browser (`cargo build --release`), then:

1. From the m-browser directory run
   `./target/release/m-browser 2>&1 | tee /tmp/m-browser.log` and open
   `file:///<path to m-browser>/qemu-system-x86_64.wasm`. The threads
   build logs `Multi-threaded QEMU: one thread per vCPU` and `4 vCPUs`.
2. The desktop appears after 2 to 2.5 minutes
   ([baselines](#interpreting-boot-timings)).
3. Keyboard: click into QEMU's picture (not the address bar), open a
   terminal, type, try Shift and Ctrl+C. Mouse: the guest pointer follows
   the host's; left and right clicks work; the wheel scrolls.
4. Ctrl+Alt+2 shows QEMU's monitor, `(qemu)`: `info status` answers
   `VM status: running`, `info network` lists the network card and the
   user network. Ctrl+Alt+3 shows the guest's serial console; Ctrl+Alt+1
   returns to the guest's screen.
5. Network, in a guest terminal: `ip addr` shows 10.0.2.15,
   `ping -c 3 10.0.2.2` answers, `wget -O /dev/null http://example.com`
   works (DNS and TCP), `apk update` (as root) fetches the package index.
6. Chromium (our image starts it after login) opens its window, 3:40 to
   3:55 after QEMU started, and responds to clicks.
7. Close m-browser's window (or navigate away): the log shows
   `[qemu] asking QEMU to quit`, then `[qemu] exited with status 0`, not
   `QEMU did not quit within ...`.
8. Start again: QEMU boots, with no `in use by another QEMU`. The guest
   saw a power cut, so its file systems replay their journals and
   Chromium may offer to restore pages; both are normal.
9. Close m-browser, then run `qemu-img check m.qcow2` in its directory
   (the host's `qemu-img`, e.g. from the qemu-utils package). Expect
   `No errors were found on the image.`; leaked clusters only waste space,
   reported corruption is damage. Never check while QEMU runs on the
   image: m-browser's lock does not stop `qemu-img` (as far as we know),
   which would then read a half-written image.

## Before you commit

Always: both flavors build (`make`, `make THREADS=1`), `make test` and
`make THREADS=1 test` pass (after `make native` if code outside the WASI
`#ifdef`s changed), and the checklist of
[10-changing-qemu.md](10-changing-qemu.md#checklist-before-committing) is
done. Then, by kind of change:

| Change | Also run |
|---|---|
| TCG backend, interpreter, JIT interface (`tcg/wasm32*`, `tcg/wasi_jit.h`, `tcg/tcg.c`, `tcg/region.c`), m-browser's JIT host | The instruction test in all three modes with both flavors; the smoke test up to Chromium; for speed changes, boot timing before and after |
| Threads (patch 0008, `WASI_THREADS` code, m-browser's `src/qemu_threads.rs`) | `make THREADS=1 test` in all three modes, and `make test` (shared code); the smoke test with the threads build and 4 vCPUs, clicking while Chromium starts; `CLICKS=1 boot-timing.sh` against the baselines |
| Display, input, consoles (`display/`, patch 0005, m-browser's display code) | Smoke test steps 2-4 with both flavors; `QEMU_DISPLAY_STATS=1` if the refresh cost may change |
| Network (patch 0006, the sleeping and waking of 0004, 0007 and 0008, m-browser's `src/qemu_net*`) | m-browser's unit tests; smoke test steps 4-5 with both flavors; a `hostfwd` rule through `QEMU_NIC` if forwarding changed |
| Build scripts, Makefile, glib-wasi, wasi-stubs | Both flavors from clean (`make clean` and `make THREADS=1 clean`; a fresh clone for fetch or configure changes); compare the module's imports ([reference/host-interface.md](reference/host-interface.md)); the instruction test; one boot |
| QEMU upgrade ([11-upgrading-qemu.md](11-upgrading-qemu.md)) | Everything: `make native` from the new tree, the six instruction-test runs, m-browser's unit tests, the whole smoke test with both flavors, boot timings of both against the baselines |

## Adding a new instruction form

1. Pick the function of its category (`test_alu()` ... `test_sse()`). For
   an optional feature, put the form inside the `if` testing its CPUID bit;
   for a leaf not read yet, read it in `main()` and add it to the header.
2. If a macro fits, it is one line, for example in the SSE4.1 block:

   ```c
   SSE2("pmaxsb", "pmaxsb")
   ```

3. Otherwise write a `TEST` block. A sketch, not in the file: a 64-bit add
   to memory, modelled on the `cmpxchg16b` form. `"+m"` keeps `m` in
   memory, so the form also exercises QEMU's guest memory accesses:

   ```c
   /* Sketch: addq with a memory destination, for test_alu(). */
   TEST("addq-mem") {
       u64 m = a, f;
       asm volatile(PRE "addq %[b], %[m]" POST
                    : [m] "+m" (m), [f] "=&r" (f)
                    : [b] "r" (b), [fi] "r" (fi) : "cc");
       MIX(m); MIX(f & FLAGS);
   } END
   ```

4. Follow the rules the existing forms follow:
   - a unique name: it is how a failure is reported;
   - `MIX` every result, including registers changed implicitly (`rdx` for
     `mul`; `rcx`, `rsi`, `rdi` for string instructions), but only the
     flags the instruction defines ("Flags Affected" in Intel's manual);
   - no faults: no division by zero or overflow, aligned memory where the
     instruction requires it;
   - exact constraints: `"+r"` for registers read and written, `"=&r"` for
     outputs written before all inputs are read, `"cc"` when flags change,
     `"memory"` for memory not named as an operand, fixed registers (`"a"`,
     `"c"`, `"d"`, `"S"`, `"D"`) for implicit operands;
   - offsets rather than absolute addresses, as `test_strings()` does.
5. Check once that the inputs reach the interesting cases: for a while,
   print every iteration in the body, for example with
   `ml_printf("%s %x %lx %lx\n", test_name, it, a, m);`, and read
   `native.txt`.
6. `make test`: both line counts grow by one per form (354 for one). Then
   the three modes with both flavors, as for a backend change.
7. Commit `insn-test.c` like any qemu-wasi file; it is not part of a
   patch. The README and the glossary quote the number of forms (351), and
   this chapter and [03-getting-started.md](03-getting-started.md) the line
   count of a full run (353): keep them in step.

## When the instruction test fails

Start from what `run.sh` printed and from `OUT/diff.txt` (normal diff
format: `<` lines are native, `>` lines wasm):

| You see | Meaning | Next |
|---|---|---|
| Only the header line, `x86-insn-diff: cpuid...` | The CPUs differ, not the results | `-smp` missing from the arguments, or `qemu-native/` built from another QEMU version: `make native` |
| A few form names, equal line counts | Those forms compute something else | The steps below |
| A list ending with `x86-insn-diff: done`, fewer wasm lines | The wasm run stopped or hung in the first form listed | `wasm-run.log`: a trap and its backtrace, `[wasi-interp] wasm trap`, `not valid wasm`, `can be neither compiled nor interpreted`; a hang lasts `WASM_TIMEOUT` |
| Every form | Something basic is broken: the hash's multiply, loops, the flags push and pop | The single-tier modes; your latest change |
| `FAIL` and no names | No `wasm.txt`: QEMU did not start | `wasm-run.log`, e.g. `unknown import` when m-browser is older than the QEMU build |
| `error: ... not found` | A QEMU or m-browser is not built | `make native`, `make`, `cargo build --release` |

Then rerun in the other modes:

| Fails in | Look at |
|---|---|
| All three modes | The wasm the backend generates (`tcg/wasm32/tcg-target.c.inc`: the op emitters, memory accesses, helper calls, `Int128`), or C code both tiers run: the helpers as clang compiled them |
| JIT only | Compiling and calling modules: m-browser's JIT host, handles and the function table, chaining between compiled TBs |
| Interpreter only | `tcg/wasm32/wasi-interp.c.inc` (decoding, execution) and the helper dispatcher `wasi-interp-calls.c.inc` |
| Default mode only | The switch between tiers: warm reloads, background compiles, the per-TB slots. Background compiles finish at different times, so run it several times |

The interpreter and the JIT execute the same wasm bytes, so when one
passes, the bytes are probably right and the other executor is wrong.
Then compare the flavors in the failing mode. If only `THREADS=1` fails,
look at what differs even with one vCPU: code under `#ifdef WASI_THREADS`
(patch 0008, [08-threads.md](08-threads.md)) and m-browser's vCPU thread
with its own Store. To find the input, print every iteration of the
failing form (step 5 above) and diff again: the first differing line shows
the inputs. Comment out the other `test_*()` calls in `main()` meanwhile.

To find the TB, log the native run's guest code and TCG ops. Each `IN:`
block is one TB, headed by its start address. Neither build here has the
capstone disassembler (`CONFIG_CAPSTONE` is off), so the code shows as
raw bytes (`OBJD-T:` lines); count bytes from the start to find the block
containing the instruction's address (`objdump -d "$out/insn-test.elf"`),
or convert the log with `qemu-src/scripts/disas-objdump.pl -t i386:x86-64`
(not tried here).

```sh
# from the qemu-wasi root, after a run.sh run
out=/tmp/qemu-wasi-insn
qemu-native/qemu-system-x86_64 -M pc -cpu max -m 64 -smp 1 -no-reboot \
    -monitor none -serial none -display none -L qemu-src/pc-bios \
    -kernel "$out/insn-test.elf" -chardev file,id=out,path="$out/n.txt" \
    -device isa-debugcon,iobase=0xe9,chardev=out \
    -d in_asm,op_opt -D "$out/native-tcg.log"
```

Then dump that TB's module from the wasm QEMU with `QEMU_TB_DUMP` (and
`QEMU_JIT_HOT=0`, so it is written at the TB's first run), and read or
validate it with `wasm-tools`
([07-tcg-backend.md](07-tcg-backend.md#how-to-inspect-generated-code)):

```sh
# from the m-browser directory; 0x100abc stands for the TB's address
out=/tmp/qemu-wasi-insn q=../qemu-wasi
QEMU_JIT_HOT=0 QEMU_TB_DUMP=0x100abc,/out ./target/release/m-browser \
    --qemu-headless --preopen "$out=/out" \
    --preopen "$q/qemu-src/pc-bios=/bios2" "$q/qemu-build/qemu-system-x86_64" \
    -M pc -cpu max -m 64 -smp 1 -no-reboot -monitor none -serial none \
    -L /bios2 -kernel /out/insn-test-wasm.elf \
    -chardev file,id=out,path=/out/w.txt \
    -device isa-debugcon,iobase=0xe9,chardev=out \
    -d in_asm,op_opt -D /out/wasm-tcg.log
wasm-tools print "$out/tb_100abc.wasm"
```

The TB usually starts at the same address in both QEMUs; if no
`QEMU_TB_DUMP: wrote` line appears, look the TB up in `wasm-tcg.log`. Its
TCG ops should match the native log's, except where the backends support
different ops. Status dumps, memory dumps at a chosen TB and loop
detection are in [14-debugging.md](14-debugging.md).

## Interpreting boot timings

Baselines on the development machine (a 20-thread x86-64 host), each run
alone, in minutes since QEMU started:

| Build | vCPUs | Desktop | Chromium window | Clicks |
|---|---|---|---|---|
| Single-threaded | 1 | ~2:03 | ~3:40 | Can be lost while Chromium starts |
| Multi-threaded | 4 | ~2:35 | ~3:55 | Answered in 6-9 s, even while Chromium starts |

Two clocks appear in the results:

- **Host time since QEMU started**: the screenshots in `shots-NAME/`
  (named by seconds; `SHOTS=5` gives 5-second steps instead of a minute)
  and `click-NAME.log`: `window drawn at N s`, and per round
  `menu after N s, K extra clicks` (extra clicks: the first one was lost).
- **Guest uptime**, in `analyze-serial.py`'s summary: since the guest's
  kernel started, 10-30 s after QEMU (firmware and bootloader). The logger
  samples every 10 s, so its milestones are up to 10 s late.

Before trusting a difference, rule out noise:

- **One VM at a time.** Runs in parallel slow each other down; the threads
  build alone keeps several host threads busy (4 vCPUs, the main loop, up
  to 4 background compilers).
- **A quiet host**: no builds, other VMs or videos meanwhile.
- **The first run after a new build** also compiles QEMU's own module once
  (about a second; `compiled in N s` in `run-NAME.log`) and caches it in
  `~/.cache/m-browser/`.
- **The same settings**: base image, `MEM`, `SMP`, `KBOOT`, `QEMU_*`
  variables (`boot-timing.sh` logs `MEM` and `SMP`); `run-NAME.log` shows
  which flavor ran (`Multi-threaded QEMU`).
- **Repeat and alternate** (old, new, old, new): a difference smaller than
  the spread between runs of the same build is noise.
- **Sanity**: no swapping in the summary (`swap: 0 pages in, 0 pages out`;
  otherwise the guest is short of RAM, which dominates), no errors printed
  from `run-NAME.log`, no stream of `[qemu-wasi] TB buffer flush` lines.
