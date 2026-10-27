# Adding features

**What this covers.** How to add a feature on the QEMU side of qemu-wasi:
a device or option QEMU already has, an environment setting, a function
QEMU calls in the host (an *import*) or the host calls in QEMU (an
*export*), display backend and QEMU code changes, missing library
functions, instruction tests, more vCPUs, and another guest architecture.
The host side has its own guide, the m-browser repository's
docs/13-adding-features.md, which this one links to rather than repeats.

## Which recipe do I need?

| You want to | Recipe | Where | QEMU patch? | m-browser change? |
|---|---|---|---|---|
| Give the guest a device or option QEMU already has (virtio disk, another network card) | [1](#1-use-a-qemu-feature-or-device-that-already-exists) | QEMU's arguments; maybe `scripts/configure.sh` | Only to change QEMU's device selection | Its argument list |
| Add a tuning or debugging setting | [2](#2-add-an-environment-knob) | The C file that needs it | Yes, unless in `display/` | No |
| Let QEMU ask the host for something | [3](#3-add-a-host-import-qemu-side) | `extern` declaration and call | Yes, unless in `display/` | Yes, shipped first |
| Let the host tell QEMU something | [4](#4-add-an-export-the-host-calls) | `qemu-src/system/main.c` | 0007 | Yes |
| Change screen output or input handling | [5](#5-add-or-change-code-in-display) | `display/` | No | For a new import or event type |
| Change what QEMU does | [6](#6-change-qemu-code-in-qemu-src) | `qemu-src/` | Yes | Rarely |
| Fix "undeclared function" or "unknown import" for a libc or GLib function | [7](#7-missing-c-library-functions) | `wasi-stubs/`, `glib-wasi/` | No | No |
| Test a guest instruction | [8](#8-a-new-instruction-form-in-the-instruction-test) | `tests/x86-insn-diff/insn-test.c` | No | No |
| Run more than 8 vCPUs | [9](#9-allowing-more-than-8-vcpus) | `qemu-src/tcg/wasm32.h` | 0008 | Yes |
| Emulate another CPU architecture | [10](#10-another-guest-architecture-outline-untested) | Many | Several | Yes |

Three facts sit behind every recipe:

1. **QEMU is sandboxed.** It runs as a WebAssembly module and reaches the
   outside world (window, network, a compiler for guest code) only through
   functions the host, m-browser, hands it. Linked with
   `-Wl,--allow-undefined` (`scripts/fix_build_ninja.py`), every C function
   declared but defined nowhere becomes an *import* from wasm module `env`
   under its C name, which the host must supply: how host functions work,
   and why a misspelt name still links.
2. **Two flavors and a native build.** `make` builds the single-threaded
   flavor (`qemu-build/`), `make THREADS=1` the multi-threaded one
   (`qemu-build-threads/`, C macro `WASI_THREADS`, a thread per vCPU, that
   is per virtual CPU): one source, different `#ifdef` paths.
   `make native` builds a Linux QEMU from the same tree for the tests.
3. **QEMU changes travel as patches.** qemu-wasi does not track
   `qemu-src/`: a change there is committed into its patch and exported
   ([10-changing-qemu.md](10-changing-qemu.md)). `display/`, `scripts/`,
   `wasi-stubs/`, `glib-wasi/` and `tests/` are plain qemu-wasi files.

## 1. Use a QEMU feature or device that already exists

QEMU has two kinds of build switches. *Devices* (emulated hardware: network
cards, disk controllers) are selected by Kconfig, a system borrowed from
Linux: each `hw/*/Kconfig` declares devices and their dependencies, and
`configs/devices/x86_64-softmmu/default.mak` adjusts the defaults; every
default PC device whose requirements are met is built. *Features* are
optional subsystems, mostly needing a host library (VNC, SDL, slirp, PNG,
extra disk image formats): options of meson, QEMU's build-file generator,
listed in `qemu-src/meson_options.txt`.
`--without-default-features` in `scripts/configure.sh` sets meson's
`auto_features=disabled`: nearly every feature not enabled there is off.

1. **Check the build**, in either flavor's build directory. A device's
   symbol is `CONFIG_` plus its `config` entry in a `Kconfig` file
   (`config VIRTIO_NET` in `hw/net/Kconfig`):

   ```sh
   # from the qemu-wasi root
   grep -E 'CONFIG_VIRTIO_(NET|BLK|PCI) ' \
       qemu-build/x86_64-softmmu-config-devices.h     # selected devices
   grep -c 'hw/net/virtio-net.c' qemu-build/compile_commands.json  # > 0
   grep -A40 'Block layer support' qemu-build/meson-logs/meson-log.txt
   ```

   The second tells whether a file is compiled (in `qemu-src/`,
   `git grep -n "CONFIG_VIRTIO_NET'" -- '*meson.build'` lists a symbol's
   files); the last prints configure's summary: features, backends, disk
   image formats (qcow2 and raw are built; `vdi`, `vmdk` and others not).
2. **Or ask the module.** QEMU prints its device types and exits, and the
   runner reports "QEMU exited during startup with status 0", as expected:

   ```sh
   # from the m-browser directory
   ./target/release/m-browser --qemu-headless \
       ../qemu-wasi/qemu-build/qemu-system-x86_64 -device help
   ```

   `-device virtio-net-pci,help` lists one device's properties. Ask the
   wasm build: a native QEMU, with more features, may list more devices.
3. **A missing device:** read its `depends on` line. A host requirement
   (`PIXMAN`, `SPICE`, `OPENGL`, `VHOST_USER`, `LINUX`; the list is
   `host_kconfig` in `qemu-src/meson.build`) needs a feature first (step
   4). A device merely not selected for x86 needs `CONFIG_<NAME>=y` in
   `qemu-src/configs/devices/x86_64-softmmu/default.mak`: a QEMU change,
   for patch 0001 (build configuration) or a new patch.
4. **A missing feature:** add `--enable-<option>` (the name from
   `meson_options.txt`, `_` written as `-`) to the `configure` call in
   `scripts/configure.sh`; the next `make` and `make THREADS=1` re-run it.
   The catch:
   - A library it needs must first be built for `wasm32-wasip1` and
     `wasm32-wasip1-threads`, with a pkg-config file (which tells the build
     where a library is) on `PKG_CONFIG_LIBDIR`. `configure.sh` limits that
     path to glib-wasi and zlib so that nothing of the host leaks in; else
     configure stops: "Dependency ... not found".
   - New code may need what WASI lacks (sockets, signals, `fork()`,
     threads): expect compile errors or new imports
     ([recipe 7](#7-missing-c-library-functions)).
   - A feature may displace a patch: `--enable-slirp` would turn off 0006's
     host network (`net/net.c` includes it only without `CONFIG_SLIRP`), and
     libslirp is not ported anyway.
5. **Pass the option.** The GUI's QEMU arguments are the `args` vector in
   the core-module branch of `App::window_event()` in m-browser's
   `src/app.rs` (then `cargo build --release --offline`); `-nic` takes
   its value from `QEMU_NIC`, without a rebuild. The headless runner takes
   QEMU's arguments on its command line; `tools/perf/boot-timing.sh` has a
   copy of the GUI's.
6. **Give the guest a driver**, and boot-test ([Checklist](#checklist)).

**Example: virtio devices.** virtio devices are made for virtual machines:
the guest queues requests in rings in its own memory and notifies the
device once per batch, where the emulated IDE disk and e1000 network card
take several register accesses per request, each a call from translated
code into QEMU's device code. In the current builds `CONFIG_VIRTIO_PCI`,
`CONFIG_VIRTIO_NET` and `CONFIG_VIRTIO_BLK` are 1 in both flavors,
`virtio-net-pci` and `virtio-blk-pci` are compiled, and Alpine's `virt`
kernel has the drivers. Nobody has tried them with this port; a speed-up
is expected, not measured.

- Network card: `QEMU_NIC=user,model=virtio-net-pci ./target/release/m-browser`.
  QEMU loads the card's option ROM (its network-boot firmware),
  `efi-virtio.rom`, from `-L /bios`. m-browser's `pc-bios/` has it and
  `efi-e1000.rom`, not those of other models (`efi-rtl8139.rom`, ...), for
  which QEMU stops: `failed to find romfile`. The guest's driver is
  `virtio_net`; `start_shell` in `tools/perf/common.sh` loads `e1000` by
  name.
- Disk: add `,if=virtio` to `"file=/data/m.qcow2,format=qcow2"` in
  `src/app.rs`. The guest then sees `/dev/vda`, not `/dev/sda`: its
  initramfs (the early file system the kernel starts from) must contain
  `virtio_blk` (on Alpine, `virtio` in the `features` of
  `/etc/mkinitfs/mkinitfs.conf`, then `mkinitfs`), and `/etc/fstab` and
  the boot loader's `root=` must name the root file system by UUID. Try it
  headless, on a copy of the image, and measure with
  `tools/perf/boot-timing.sh` after changing its `-drive` and `-nic`
  arguments ([15-performance.md](15-performance.md)).

## 2. Add an environment knob

A knob is a setting read from an environment variable, for tuning or
debugging without a rebuild. m-browser copies every variable whose name
starts with `QEMU_` or `WASI_` into QEMU's WASI environment, and no other
(`run_wasm_module()` in its `src/wasm.rs`), so it needs no change.

1. **Name it** `QEMU_<AREA>_<SETTING>` (`WASI_GFX_*` in `display/`), with
   the unit in the name unless obvious (`QEMU_TB_SLICE_MS`). Check that
   m-browser does not read the name itself (`grep -rn` in its `src/`).
2. **Read it once** into a `static`, like `apic_timer_slack()` in
   `qemu-src/hw/intc/apic.c` and `wasi_gfx_scale()` in
   `display/wasi_gfx_display.c`. `getenv()` scans the whole environment,
   too slow for code that runs per frame or per TB (translation block: a
   piece of guest code QEMU has translated; TBs run millions of times a
   second):

   ```c
   /* QEMU_FOO_MS: what it changes (default 30; 0 turns it off). */
   static int64_t wasi_foo_ms(void)
   {
       static int64_t ms = -1;             /* -1: not read yet */

       if (ms < 0) {
           const char *env = getenv("QEMU_FOO_MS");

           ms = env ? atoll(env) : 30;
           ms = ms < 0 ? 0 : ms;
       }
       return ms;
   }
   ```

3. **Parse defensively.** `atoi()` turns text into 0 silently, and
   `strtoul(s, NULL, 0)` reads `010` as octal. A presence test
   (`getenv(...) != NULL`) is on even for `=0`; if people will write `=0`
   for off, compare the value, as `QEMU_JIT_ASYNC` does. In the threaded
   flavor two threads may do the first read at once: harmless when both
   compute the same value; anything more belongs in start-up code.
4. **Guard it** with `#ifdef WASI_PLATFORM` in files the native build
   compiles, unless it should work natively too (`QEMU_DUMP_AT` does, to
   compare runs). `display/` needs no guard.
5. **Commit** it with its code, and add a row to
   [reference/environment-variables.md](reference/environment-variables.md):
   default, values, effect, where it is read, flavor (see its "Adding a
   variable"). m-browser's environment is fixed at its start: restart it.

## 3. Add a host import (QEMU side)

A host import is a C function QEMU calls and m-browser implements, such as
`host_net_send()`. Its host half (the Rust function, QEMU's memory,
threads, errors) is Part 1 of m-browser's docs/13-adding-features.md.

1. **Design the call.** Name it after its area (`host_gfx_*`,
   `host_net_*`). Pass numbers and pointers only, strings and buffers as a
   pointer and a length; return `int32_t` (0 or -1, a length, a handle).
   `long` and `size_t` are 32 bits on wasm32: write `int64_t` for 64. Note
   which thread calls it (step 5).
2. **Declare it** at the top of the calling file with a comment block like
   those in `qemu-src/net/wasi-user.c.inc` and
   `display/wasi_gfx_display.c`, or in a header if several files call it
   (`qemu-src/tcg/wasi_jit.h`):

   ```c
   /*
    * Host import, from the "env" module (m-browser: src/qemu_foo.rs):
    *
    *   int32_t host_foo_set(const char *name, int32_t len)
    *       What the host does with it.  Returns 0, or -1 after the host
    *       has printed why.  Called from the main loop, BQL held.
    */
   extern int32_t host_foo_set(const char *name, int32_t len);
   ```

3. **Call it** inside `#ifdef WASI_PLATFORM` (or `WASI_THREADS` for the
   threaded flavor only) in any file under `qemu-src/`: the native build
   links without `--allow-undefined` and fails with "undefined reference".
   `display/` is never compiled natively.
4. **Build both flavors and look at the import:**

   ```sh
   # from the qemu-wasi root
   make && make THREADS=1
   for b in qemu-build qemu-build-threads; do
       wasm-tools print --skeleton $b/qemu-system-x86_64 | grep '"host_foo_set"'
   done
   ```

   Each line ends in `(type N)`; `grep -E '\(type \(;N;\)'` on the same
   output shows the signature, `(param i32 i32) (result i32)` here.
5. **Know the calling thread** (threaded flavor). Main-loop code (display,
   timers, network polling, the monitor) holds the BQL, QEMU's big lock,
   except while sleeping. Device register accesses run on a vCPU's thread,
   most with the BQL; helpers (C functions that translated code calls) and
   translated code itself run there without it. So the host function may
   run on any thread, on several at once, and must return quickly: vCPUs
   waiting for the BQL stall meanwhile ([08-threads.md](08-threads.md)).
6. **Ship m-browser first.** A module importing a function the host lacks
   fails to instantiate (``unknown import: `env::host_foo_set` has not been
   defined``); an extra host function is harmless. Build m-browser, test
   the uninstalled QEMU with the headless runner, then `make install` or
   `make THREADS=1 install`. For a new signature, add a new name.
7. **Commit and document:** fold into the patch and export
   ([10-changing-qemu.md](10-changing-qemu.md)); describe the function in
   [reference/host-interface.md](reference/host-interface.md), README.md's
   host table and m-browser's docs/reference/host-imports.md.

## 4. Add an export the host calls

An export is a QEMU function the host calls, like `wasi_qemu_quit()` in
`qemu-src/system/main.c` (patch 0007). QEMU is a WASI *reactor*: rather
than running `main()` once, the host calls `wasi_qemu_init()`, then
`wasi_qemu_step()` (one main-loop iteration) again and again, and regains
control in between. Exports are called there.

1. **Define it** next to the others, in the `#ifdef WASI_PLATFORM` part of
   `system/main.c`; not `static`, numbers in and out:

   ```c
   /* Press the ACPI power button, as the monitor's system_powerdown does. */
   __attribute__((export_name("wasi_qemu_powerdown")))
   void wasi_qemu_powerdown(void)
   {
       qemu_system_powerdown_request();
   }
   ```

   **Pitfall:** keep exports in `system/main.c`, which is linked as an
   object file. Most of `util/` is in the archive `libqemuutil.a`, whose
   files are linked only when referenced, so an export there can vanish
   silently (check with `wasm-tools print --skeleton ... | grep '(export'`).
2. **Return promptly; request rather than do.** While an export runs, no
   main-loop iteration runs, and in the single-threaded flavor no vCPU
   either. Never wait for work the main loop or a vCPU must do first: set
   a flag, schedule a bottom half (a callback the main loop runs soon,
   `qemu_bh_schedule()`) or call a request function, as `wasi_qemu_quit()`
   calls `qemu_system_shutdown_request()`; the next step does the work.
3. **Know where it runs:** on QEMU's main instance (the loaded copy of the
   module that runs the main loop) and its thread, which holds the BQL
   between steps (the main loop drops it only while sleeping, in
   `main_loop_wait()`). With threads the vCPUs keep running: reach a vCPU's
   state with `async_run_on_cpu()` (`run_on_cpu()` waits). Never call an
   export from inside a host import: QEMU is mid-operation then.
4. **Host side:** `run_qemu_reactor()` in m-browser's `src/wasm.rs` calls
   `wasi_qemu_quit` between steps and copes with builds that lack it; do
   the same. The single-threaded flavor moves QEMU to a new instance now
   and then (`migrate_store()`): look exports up on the current one (the
   m-browser repository's docs/06-qemu-runner.md). An export nobody calls
   is harmless, so QEMU may ship first. Add it to the Exports table of
   [reference/host-interface.md](reference/host-interface.md).

## 5. Add or change code in display/

`display/` is the display backend. `wasi_gfx_dcl.c` is QEMU glue (display
listener, console choice, refresh); `wasi_gfx_display.c` copies and scales
frames, declares the `host_gfx_*` imports and turns host events into QEMU
input; `wasi_gfx_display.h` connects them
([09-display-input-network.md](09-display-input-network.md)). It is outside
`qemu-src/`: commit changes in qemu-wasi, no patch.

1. Edit, then `make` and `make THREADS=1`. `scripts/fix_build_ninja.py`
   compiles each file with the flags of QEMU's `ui/console.c` plus
   `DISPLAY_DEFINES` (`-DQEMU_BUILD`), so QEMU headers work through
   `qemu/osdep.h`, and ninja recompiles when a file or a header changes.
   Only the wasm builds compile `display/`: no `WASI_PLATFORM` guard.
2. It runs on the main loop's thread with the BQL held, mostly from
   `wasi_gfx_dpy_refresh()`, about 30 times a second. The single-threaded
   flavor takes that time from the guest; `QEMU_DISPLAY_STATS=1` shows it.
3. A new file also goes into `DISPLAY_SOURCES` in
   `scripts/fix_build_ninja.py`. **Pitfall:** the script adds compile
   statements only when the first listed object has none yet, but links
   every listed object. In an already adapted `build.ninja` the new object
   has no rule: "'wasi_gfx_new.o', needed by 'qemu-system-x86_64', missing
   and no known rule to make it". Re-configure: delete
   `qemu-build/build.ninja` and `qemu-build-threads/build.ninja`, then run
   `make` and `make THREADS=1`.
4. A new input event type needs m-browser too (Part 4 of its
   docs/13-adding-features.md).

## 6. Change QEMU code in qemu-src

1. **Pick the patch** by topic ([06-patch-series.md](06-patch-series.md),
   the table in [10-changing-qemu.md](10-changing-qemu.md#choosing-the-patch)),
   or start a new one, at the end of the series, for a new topic.
2. **Guard WASI-only code**, so other builds keep upstream behaviour:

   | Code for | Guard |
   |---|---|
   | Both wasm flavors | `#ifdef WASI_PLATFORM` |
   | The threaded flavor | `#ifdef WASI_THREADS` |
   | The single-threaded flavor | `#if defined(WASI_PLATFORM) && !defined(WASI_THREADS)` |
   | Every build, native too (rare: diagnostics to compare runs) | none |

3. **The single-threaded flavor has no threads.** `pthread_create()`
   succeeds but the thread never runs (`wasi-stubs/pthread_stubs.c`), and
   GLib timeouts and idle callbacks never fire (`glib-wasi/src/gmain.c`).
   Use QEMU timers (`timer_new_ms()`) and bottom halves, and never sleep:
   the guest stops meanwhile.
4. **A new source file** is simplest included from an existing `.c`, as
   `net/net.c` includes `net/wasi-user.c.inc`. In a `meson.build` it works
   too, but ninja then regenerates `build.ninja`, dropping the edits of
   `fix_build_ninja.py`: run `make` again
   ([10-changing-qemu.md](10-changing-qemu.md#building-after-an-edit)).
5. **Functions shared between patches** are declared by local `extern`s
   that the compiler never checks against the definition: rename or change
   one everywhere (`git grep -n 'extern.*<name>'` in `qemu-src/`).
6. **Build, test, fold and export** as in
   [10-changing-qemu.md](10-changing-qemu.md), with `make native` too if
   code outside the guards changed.

## 7. Missing C library functions

wasi-libc (wasi-sdk's C library) and glib-wasi (this project's minimal
GLib) lack many functions. With `--allow-undefined` that is rarely a link
error:

| Symptom | Meaning |
|---|---|
| `call to undeclared function 'foo'`, unknown type or constant | No header declares it |
| The build links; m-browser: ``unknown import: `env::foo` has not been defined`` | A header declares it, nothing defines it |
| configure reports a function or feature missing | Usually fine: QEMU falls back |

1. **Avoid the call** under `#ifdef WASI_PLATFORM` when the function makes
   no sense in a sandbox (`fork()`, signal handlers).
2. **Or stub it in `wasi-stubs/`**, whose headers come before the
   sysroot's (wasi-sdk's C headers) on QEMU's include path;
   `include/qemu/osdep.h` includes its three `wasi_*.h`. Declare in a
   header there (`wasi_process_stubs.h`, `wasi_socket_extras.h`, or one
   named like the system header, such as `pwd.h`). Define in
   `posix_stubs.c` (both flavors) or, for pthreads, `pthread_stubs.c`
   (single-threaded) or `pthread_threads_stubs.c` (only what the threaded
   libc lacks). A new `.c` file goes into `SRCS` in both branches of
   `wasi-stubs/Makefile`. Fail honestly (`errno = ENOSYS; return -1;`)
   unless faking success is harmless, and say why in a comment.
3. **Or add the GLib function to `glib-wasi/`**: declaration in the
   matching `glib/*.h`, code in `src/` (every `.c` there is compiled). Both
   variants build from the same source, the threaded one with
   `-DWASI_THREADS`: guard shared state with a pthread mutex there; the
   `g_atomic_*` macros are not atomic.
4. **Rebuild:** `make` and `make THREADS=1` rebuild the flavor's libraries
   first (`make libs` does only that) and relink QEMU when one is newer. A
   function a configure check should now find needs a re-configure
   (delete the flavor's `build.ninja`, run `make`).

## 8. A new instruction form in the instruction test

`tests/x86-insn-diff/insn-test.c`, a bare-metal x86-64 program, runs under
the native and the wasm QEMU, and the test compares one hash per
instruction form. A difference is a bug in how the wasm build runs what
TCG, QEMU's translator of guest code, produced ([13-testing.md](13-testing.md)).

1. Add the form to the matching `test_*()` function. `TEST("name") { ...
   MIX(result); } END` runs the body for `ITERS` (1024) operand sets `a`,
   `b`, `c`, `d` with incoming flags `fi`, and prints `name <hash>`.
   `ALU2`, `ALUI`, `ALU1`, `SHCL`, `MOV2`, `SSE2` and others cover common
   shapes. Mix in every result, but only the flags the instruction defines
   (the rules: [13-testing.md](13-testing.md#adding-a-new-instruction-form)).
2. Guard optional extensions by their CPUID bit, as `test_bits()` and
   `test_sse()` do.
3. Run `make test` and `make THREADS=1 test` (after `make native` if QEMU
   changed). **Note:** the test runs with `-smp 1`.

## 9. Allowing more than 8 vCPUs

In the threaded flavor each TB holds one JIT handle (the function-table
index of its compiled module) per vCPU, since each vCPU's wasm instance
has a function table of its own. Details:
[08-threads.md](08-threads.md#changing-the-number-of-vcpus).

1. `WASI_JIT_SLOTS` in `qemu-src/tcg/wasm32.h` (8 with `WASI_THREADS`) is
   the number of slots; the layout written by `wasm_finalize_module()`
   and read by `tb_handle_slot()` follows from it, and `tcg_qemu_tb_exec()`
   aborts QEMU when a vCPU's index reaches it ("at most 8 vCPUs are
   supported"). Each slot costs 4 bytes per TB. A fixup of 0008.
2. m-browser clamps `QEMU_SMP` to 1..8 in `run_wasm_module()`
   (`src/wasm.rs`); an explicit `-smp` passes unchecked. Its docs say 8 in
   several places (`grep -rn 'at most 8' docs` in m-browser).
3. Memory: each vCPU thread's 16 MiB stack (`qemu_thread_create()` in
   `util/qemu-thread-posix.c`) comes out of QEMU's 4 GiB, with guest RAM
   (`-m 1024`) and the TB buffer (`tb-size=256`). In the host, each vCPU
   has its own wasmtime Store, which keeps the TB modules compiled for it.
4. Test: the instruction test uses one vCPU, so boot with `QEMU_SMP=12`,
   check `nproc` in the guest and `info cpus` in the monitor, and measure:
   vCPUs beyond the host's cores cannot help.

## 10. Another guest architecture (outline, untested)

Nobody has tried this. These are the places that assume x86-64, for
example when adding `aarch64-softmmu`:

1. **Build:** `--target-list` in `scripts/configure.sh`; `TARGET`, a
   single name, in `scripts/fix_build_ninja.py`; `QEMU_WASM`, the ninja
   target, the `install` name and the `native` target list in the Makefile.
2. **Devices and display:** `configs/devices/aarch64-softmmu/`. The `virt`
   machine has no VGA; `virtio-gpu-pci` draws with pixman functions, which
   this build takes from the partial stand-in of patch 0005.
3. **Firmware:** m-browser's `pc-bios/` has only x86 firmware (SeaBIOS,
   VGA BIOSes, option ROMs); aarch64 needs UEFI firmware or `-kernel`.
4. **Backend assumptions:** in the threaded flavor `TCG_TARGET_DEFAULT_MO`
   (`tcg/wasm32/tcg-target.h`) claims x86-like ordering for TB code: true
   on x86-64 hosts, and it spares an x86 guest barriers. A weakly ordered
   guest such as aarch64 relies only on the barriers it asks for, which
   become `atomic.fence`. Guests with 32-bit addresses use the
   `qemu_ld/st_a32` ops, emitted without zero-extending the address
   ([07-tcg-backend.md](07-tcg-backend.md#limits-and-rough-edges)).
   Helpers with more than 8 wasm parameters need a regenerated dispatcher
   (`MAX_PARAMS` in `scripts/gen_interp_calls.py`).
5. **x86 parts of patches and tests:** 0008's tick grid is in the x86
   APIC (`hw/intc/apic.c`); the instruction test is x86-only.
6. **m-browser:** `qemu-system-x86_64` as argv[0] (`src/app.rs`,
   `src/headless.rs`), the GUI's x86 arguments, the installed file name
   `qemu-system-x86_64.wasm`.

## Worked example: a host_log import (sketch, not tested)

`host_log()` prints a message from QEMU on m-browser's stderr. Its Rust
half is the example in Part 1 of the m-browser repository's
docs/13-adding-features.md: a new `src/qemu_log.rs` with
`add_log_to_linker()`, `mod qemu_log;` in `src/main.rs`, and a call after
`add_net_to_linker()` in `run_wasm_module()`. Add that first, and run
`cargo build --release --offline` in the m-browser directory.

1. **QEMU:** in `qemu-src/system/main.c` (patch 0007), inside
   `#ifdef WASI_PLATFORM`, above `wasi_qemu_init()`:

   ```c
   /*
    * Host import, from the "env" module (m-browser: src/qemu_log.rs):
    *
    *   int32_t host_log(const char *msg, int32_t len)
    *       Print msg, len bytes without a NUL, on the host's stderr.
    *       Returns 0, or -1 if the host could not read it.  Keeps no
    *       state: any thread may call it.
    */
   extern int32_t host_log(const char *msg, int32_t len);
   ```

   and in `wasi_qemu_init()`, between `qemu_init(argc, argv);` and
   `return 0;`:

   ```c
       {
           static const char msg[] = "machine created";

           host_log(msg, sizeof(msg) - 1);
       }
   ```

   The native build does not compile this part of the file. To call
   `host_log()` from other files too, `display/` included, declare it in
   `include/qemu/wasi-debug.h` (patch 0001) inside its
   `#ifdef WASI_PLATFORM` instead: every file gets it via `qemu/osdep.h`.
2. **Build and check** both flavors:

   ```sh
   # from the qemu-wasi root
   make && make THREADS=1
   for b in qemu-build qemu-build-threads; do
       wasm-tools print --skeleton $b/qemu-system-x86_64 | grep '"host_log"'
   done
   ```

3. **Run both uninstalled**, without a disk (the guest stops in the BIOS):

   ```sh
   # from the m-browser directory
   for b in qemu-build qemu-build-threads; do
       QEMU_QUIT_AFTER=20 ./target/release/m-browser --qemu-headless \
           ../qemu-wasi/$b/qemu-system-x86_64 -L /bios -m 256 2>&1 |
           grep -E 'qemu-log|Running|exited'
   done
   ```

   Expected for each: `[qemu-log] machine created`, the runner's `Running`
   line, and at the end `exited with status 0` (`QEMU_QUIT_AFTER` counts
   from m-browser's start, which includes compiling a new module).
4. **Fold into 0007, export, commit:**

   ```sh
   # from qemu-src/: the hash of "system: run QEMU as a WASI reactor"
   git log --oneline v9.1.0..HEAD
   git add system/main.c && git commit --fixup=<hash>
   old=$(git rev-parse HEAD)
   GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash v9.1.0
   git diff --stat "$old" HEAD     # must print nothing
   # from the qemu-wasi root
   make export-patches && git diff --stat patches/
   ```

   Only 0007 should change, plus `system/main.c`'s `index` line and line
   numbers in 0008. Commit `patches/` with a "Logging" section in
   [reference/host-interface.md](reference/host-interface.md) and a row in
   README.md's host table (m-browser: its docs/reference/host-imports.md).
5. **Install** (`make install` or `make THREADS=1 install`) and start the
   GUI once.

## Pitfalls

- **Both flavors must build and run.** `#ifdef WASI_THREADS` code is
  compiled by only one of them; build and boot-test both.
- **The two sides ship together.** A new import needs m-browser first, a
  new export an m-browser that copes without it, a changed signature a new
  name.
- **Never edit `patches/*.patch` by hand**: change `qemu-src/` and export
  ([06-patch-series.md](06-patch-series.md#how-the-patches-are-stored)).
- **Keep WASI code under `#ifdef`, and check that `make native` still
  builds**: the instruction test needs it.
- **Thread safety in the threaded flavor.** Any host import may be called
  from a vCPU thread, holding the BQL or not, and from several at once. A
  `static` written after start-up needs `qatomic_*()` or a `QemuMutex`
  (glib-wasi's `g_atomic_*()` are plain reads and writes).
- **Misspelt names become imports** (`--allow-undefined`): compare the
  imports after every change.
- **Re-run the instruction test after any change near TCG** (`tcg/`,
  `accel/tcg/`, `target/i386/`): `make test` and `make THREADS=1 test`, each
  also with `QEMU_JIT_HOT=0` and `QEMU_JIT_HOT=4000000000` (the two tiers).

## Checklist

1. `make` and `make THREADS=1` build; so does `make native` if code
   outside the WASI guards changed.
2. Both modules' imports and exports are as intended
   ([reference/host-interface.md](reference/host-interface.md#checking-a-builds-interface)).
3. The instruction test passes in both flavors, in all three modes after
   changes near TCG.
4. m-browser's half is merged and built first (for imports), and its tests
   pass (`cargo test --release --offline`).
5. Each affected flavor boots in the GUI: desktop, typing, the monitor
   (Ctrl+Alt+2, `info status`), and `poweroff` ends QEMU with status 0.
6. The QEMU change is folded into its patch and exported, and
   `git diff patches/` shows only it.
7. The documentation is updated: environment variables, host interface,
   README.md's tables, [06-patch-series.md](06-patch-series.md) for a new
   patch or design, and m-browser's documents.
