# Getting started

## What this covers

From a fresh Linux machine to an Alpine Linux desktop running in m-browser,
on your own: what to install, where the two repositories go, how to build,
install and run QEMU for WebAssembly, how to check that it works, and the
everyday commands. It assumes an x86-64 Debian or Ubuntu host (the
development machine runs Ubuntu 22.04). Background is in
[02-concepts.md](02-concepts.md), terms in
[reference/glossary.md](reference/glossary.md).

## In a hurry

```sh
# Host tools (Debian/Ubuntu; typical package names)
sudo apt install build-essential curl git libssl-dev ninja-build pkg-config python3-venv
# wasi-sdk 29, the WebAssembly C toolchain, as /opt/wasi-sdk
curl -LO https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-29/wasi-sdk-29.0-x86_64-linux.tar.gz
sudo tar -C /opt -xzf wasi-sdk-29.0-x86_64-linux.tar.gz && sudo ln -s /opt/wasi-sdk-29.0-x86_64-linux /opt/wasi-sdk
# Rust 1.89 or newer for m-browser, unless cargo --version shows one; then open a new shell
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
# Both repositories, side by side
mkdir -p ~/src && cd ~/src
git clone git@github.com:abemassry/qemu-wasi.git && git clone git@github.com:abemassry/m-browser.git
# QEMU, multi-threaded flavor (the one m-browser ships), installed into m-browser: several minutes
cd ~/src/qemu-wasi && make THREADS=1 install
# m-browser, plus the guest's disk image from the team's shared storage
cd ~/src/m-browser && cargo build --release
cp /path/to/shared/storage/m.qcow2 .
./target/release/m-browser
```

Then type `file:///home/<you>/src/m-browser/qemu-system-x86_64.wasm` into
the address bar and press Enter: the desktop appears 2 to 3 minutes later.
When a step fails, see [Troubleshooting](#troubleshooting-the-first-build).

## Requirements

qemu-wasi is *cross-compiled*: built on your x86-64 machine for another
target, wasm32 (32-bit WebAssembly) with WASI, the system interface that
gives WebAssembly programs files and clocks. The cross-compiler, **wasi-sdk**,
bundles clang, the wasm-ld linker and a *sysroot* (wasi-libc, the target's C
library, with its headers). QEMU's build system, **meson**, runs in a Python
*virtual environment* and writes build files that **ninja** executes.

| Tool | Why | How to check | Debian/Ubuntu (typical packages) |
|---|---|---|---|
| wasi-sdk 29 in `/opt/wasi-sdk` | The cross-compiler for every wasm part | `/opt/wasi-sdk/bin/clang --version` shows `21.1.4-wasi-sdk` | Not packaged: the release tarball ([below](#installing-wasi-sdk)) |
| git | Clones QEMU, zlib and meson's subprojects; applies the patches | `git --version` | `git` |
| make, gcc | The Makefiles; meson also detects the host's own `cc` | `make --version`, `cc --version` | `build-essential` |
| Python 3.8+ with venv | QEMU's `configure` creates a virtual environment; `scripts/fix_build_ninja.py` | `python3 --version`; `python3 -c 'import ensurepip'` | `python3-venv`, which holds `ensurepip`; QEMU needs it only without pip and setuptools (the development machine has `python3-pip` and `python3-setuptools` instead) |
| ninja | Runs the compile | `ninja --version` | `ninja-build` |
| pkg-config | Lets meson find glib-wasi and zlib | `pkg-config --version` | `pkg-config` |
| Network access | The first `make`: gitlab.com (QEMU, meson subprojects), github.com (zlib) | `git ls-remote https://gitlab.com/qemu-project/qemu.git v9.1.0` | |
| Rust 1.89+, OpenSSL headers | Building m-browser (it uses `File::try_lock()` and links OpenSSL) | `cargo --version` | rustup, `libssl-dev`; the full list is in the m-browser repository's docs/03-getting-started.md |
| gcc for x86-64 | `make test` compiles its test program | `gcc --version` | `build-essential` |
| QEMU's native build dependencies | `make native`, the reference QEMU for `make test` | `pkg-config --modversion glib-2.0 zlib pixman-1` | `libglib2.0-dev` (GLib 2.66+), `zlib1g-dev`, probably `libpixman-1-dev` |
| qemu-img, debugfs | `tools/perf/`: copies of the disk image, the guest's kernel | `qemu-img --version`, `/sbin/debugfs -V` | `qemu-utils`, `e2fsprogs` |
| Pillow | `tools/perf/click-latency.sh` only | `python3 -c 'import PIL'` | `python3-pil` |
| wasm-tools (optional) | Inspecting modules: imports, exports, text form | `wasm-tools --version` | `cargo install wasm-tools` |
| Disk space | As measured: `qemu-src/` 0.2 GB, each build directory 0.16 GB, `qemu-native/` 0.3 GB, wasi-sdk 0.4 GB, the disk image 2 GB (plus a copy for `tools/perf/`), m-browser's `target/` about 10 GB | `df -h ~` | |

### Installing wasi-sdk

Distributions do not package it: take the Linux x86-64 tarball of release
29, the tested one (LLVM 21.1.4), from
<https://github.com/WebAssembly/wasi-sdk/releases>.

```sh
# from any scratch directory, e.g. /tmp
curl -LO https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-29/wasi-sdk-29.0-x86_64-linux.tar.gz
sudo tar -C /opt -xzf wasi-sdk-29.0-x86_64-linux.tar.gz   # /opt/wasi-sdk-29.0-x86_64-linux/
sudo ln -s /opt/wasi-sdk-29.0-x86_64-linux /opt/wasi-sdk
/opt/wasi-sdk/bin/clang --version     # clang version 21.1.4-wasi-sdk ...
```

Installed elsewhere, pass `WASI_SDK=<dir>` to every `make`, or export it.
**Pitfall:** the path is recorded when QEMU is configured and zlib is built;
another wasi-sdk later needs a rebuild from scratch
([11-upgrading-qemu.md](11-upgrading-qemu.md#wasi-sdk)).

## Getting the sources

The Makefile, the instruction test and `tools/perf/` expect m-browser in
`../m-browser` (or wherever `M_BROWSER` says), so clone both side by side:

```sh
# from the directory that will hold both, here ~/src
git clone git@github.com:abemassry/qemu-wasi.git   # or https://github.com/abemassry/qemu-wasi.git
git clone git@github.com:abemassry/m-browser.git   # or https://github.com/abemassry/m-browser.git
```

```text
~/src/qemu-wasi/                          this repository; patches/ holds the QEMU changes
~/src/qemu-wasi/qemu-src/                 QEMU v9.1.0 plus the patches: cloned by the first make
~/src/m-browser/                          the browser that runs QEMU
~/src/m-browser/pc-bios/                  firmware for QEMU (BIOS, VGA BIOS), in m-browser's git
~/src/m-browser/qemu-system-x86_64.wasm   the QEMU it runs; make install replaces it
~/src/m-browser/m.qcow2                   the guest's disk image: not in git, you copy it there
```

The disk image (2 GB: Alpine Linux with Xorg, Fluxbox and Chromium, logging
in automatically) comes from the team's shared storage
([16-guest-image.md](16-guest-image.md)).

**Note:** m-browser's git already contains a QEMU build (currently the
multi-threaded flavor): with `m.qcow2` in place you can boot it before
building anything, to separate m-browser problems from build problems.

## Building

```sh
# from the qemu-wasi root
make
```

The first `make` needs network access and takes several minutes; after an
edit to one QEMU file, about 10 to 20 seconds (20-thread machine). A first
build runs, in this order:

1. `scripts/fetch-qemu.sh`, once: a shallow clone of tag `v9.1.0` (named in
   `patches/BASE`) from gitlab.com into `qemu-src/`, branch `qemu-wasi`,
   then `git am` of the 8 patches, one commit each.
2. `scripts/build-zlib.sh`, once per flavor: zlib v1.3.1 from GitHub into
   `zlib-src/`, built into `zlib-wasi/` (the first use of wasi-sdk).
3. `make -C glib-wasi`, `make -C wasi-stubs`: a minimal GLib (the C utility
   library QEMU requires) and stand-ins for what wasi-libc lacks.
4. `scripts/configure.sh`, again only after `make clean`, `make reset-qemu`,
   `make export-patches` or a change to the script: QEMU's `configure` with
   wasi-sdk's clang. It creates `qemu-build/pyvenv/` with meson, downloads
   the meson *subprojects* (dtc, keycodemapdb, berkeley-softfloat-3,
   berkeley-testfloat-3) into `qemu-src/subprojects/`, and writes
   `qemu-build/build.ninja`.
5. `scripts/fix_build_ninja.py`, every time: adds the display backend
   (`display/`) and the link flags of a WASI module, unknown to meson.
6. `ninja -C qemu-build qemu-system-x86_64`, on all CPUs; then `ls -l` of
   the result.

The result is **`qemu-build/qemu-system-x86_64`**, a WebAssembly module of
about 40 MB without a `.wasm` extension. The scripts are described in
[05-repository-tour.md](05-repository-tour.md#scripts). Settings go on the
command line or in the environment: `WASI_SDK`, `THREADS`, `M_BROWSER`,
`NINJA`, and for the clones `QEMU_GIT`, `ZLIB_GIT` and `ZLIB_TAG` (see
[reference/environment-variables.md](reference/environment-variables.md)).

**Note:** configuring leaves an empty `qemu-src/subprojects/.wraplock` that
`make export-patches` warns about; exclude it once per QEMU clone, from the
qemu-wasi root, with `echo /subprojects/.wraplock >> qemu-src/.git/info/exclude`
([why](10-changing-qemu.md#the-wraplock-file)).

### The multi-threaded flavor

`make THREADS=1` builds the second *flavor*, in `qemu-build-threads/`; both
can exist side by side, and `THREADS=1` works with every target.

| | `make` | `make THREADS=1` |
|---|---|---|
| WASI target | `wasm32-wasip1` | `wasm32-wasip1-threads` |
| Virtual CPUs (vCPUs) | 1, run inside QEMU's main loop | One host thread each; m-browser starts 4 (`QEMU_SMP`; 2 on hosts with fewer than 8 CPUs) |
| Result | `qemu-build/qemu-system-x86_64` | `qemu-build-threads/qemu-system-x86_64` |
| Desktop, Chromium's window (development machine) | ~2:03, ~3:40 | ~2:35, ~3:55 |
| Clicks while Chromium starts | Can be lost | Answered in 6-9 s |

It reuses `qemu-src/` and `zlib-src/` but configures and compiles
everything again (several minutes). Build both after changing QEMU: they
compile different `#ifdef WASI_THREADS` paths ([08-threads.md](08-threads.md)).
**Pitfall:** run the two flavors' first builds one after the other: both
build zlib in `zlib-src/`.

## Installing into m-browser

```sh
# from the qemu-wasi root
make install              # the single-threaded build
make THREADS=1 install    # or the multi-threaded one
```

`install` builds if needed, then copies the module to
`../m-browser/qemu-system-x86_64.wasm` (or into `M_BROWSER`). Both flavors
write that file: the last one installed wins. m-browser recognizes the
multi-threaded build (it imports its memory; log: `Multi-threaded QEMU: one
thread per vCPU`) and needs no rebuild unless the host interface changed
([reference/host-interface.md](reference/host-interface.md)). m-browser's
git tracks the file: `git checkout qemu-system-x86_64.wasm` in m-browser
restores the shipped build.

## Running

### In m-browser's window

```sh
# from the m-browser directory (its own needs: its docs/03-getting-started.md)
cargo build --release          # the first time, and after pulling m-browser changes
./target/release/m-browser 2>&1 | tee /tmp/m-browser.log
```

1. Start it from the m-browser directory: QEMU sees that directory as
   `/data` (with `m.qcow2`) and `pc-bios/` as `/bios`.
2. Replace the address in the address bar with `file://` plus the module's
   absolute path, and press Enter (`echo "file://$PWD/qemu-system-x86_64.wasm"`
   in the m-browser directory prints it).
3. After a new build, the first start pauses while m-browser compiles QEMU
   to machine code (`compiling QEMU's module with optimizations`), cached in
   `~/.cache/m-browser/`.
4. The desktop appears after 2 to 3 minutes on the development machine,
   Chromium's window about 1.5 minutes later
   ([baselines](13-testing.md#interpreting-boot-timings)).
5. Click into the guest's picture before typing. Ctrl+Alt+2 shows QEMU's
   monitor, Ctrl+Alt+3 the guest's serial console, Ctrl+Alt+1 its screen.
6. Closing the window stops QEMU cleanly but is a power cut for the guest;
   `poweroff` as root in the guest is cleaner.

**Pitfall:** started elsewhere, QEMU stops at once (`QEMU exited during
startup`). Only one m-browser may run QEMU on `m.qcow2`; a second reports
`m.qcow2 is in use by another QEMU`. Under Wayland, QEMU's window may open
separately (m-browser's docs/05-browser-ui.md).

### Headless

m-browser's *headless runner* runs QEMU without a window, saving the screen
to a PNG (its docs/11-headless-and-automation.md). It takes no disk image
lock, and two QEMUs on one qcow2 file corrupt it: run it on a copy, never on
the `m.qcow2` a GUI may be using.

```sh
# from the m-browser directory, with no QEMU running on m.qcow2
mkdir -p ~/vm-test && cp m.qcow2 ~/vm-test/disk.qcow2
QEMU_SNAPSHOT=$HOME/vm-test/screen.png QEMU_QUIT_AFTER=300 \
./target/release/m-browser --qemu-headless --preopen "$HOME/vm-test=/out" \
    qemu-system-x86_64.wasm -L /bios -m 1024 \
    -drive file=/out/disk.qcow2,format=qcow2 \
    -accel tcg,tb-size=256 -device qemu-xhci -device usb-tablet \
    -monitor vc -serial vc -nic user > ~/vm-test/run.log 2>&1
```

These are the GUI's QEMU arguments, with `~/vm-test` shown to QEMU as
`/out`. Watch `screen.png` (rewritten at most once a second); QEMU quits
cleanly after 300 s. The module may also be an uninstalled build, such as
`../qemu-wasi/qemu-build/qemu-system-x86_64`. Easier, `tools/perf/` copies
the image itself:

```sh
# from the qemu-wasi root; runs the module installed in m-browser
tools/perf/prepare-base.sh                    # once: copies m.qcow2 (a few minutes)
DURATION=300 tools/perf/boot-timing.sh first  # boots for 5 minutes
```

Results in `tools/perf/work/`: `screen-first.png`, `shots-first/` (one a
minute), `run-first.log`, and a summary of when X, Fluxbox and Chromium
started ([13-testing.md](13-testing.md#boot-and-responsiveness-tests-toolsperf)
has the options).

## First checks

### The instruction test

It runs several hundred x86 instruction forms under a native QEMU and your
wasm build and compares the results
([13-testing.md](13-testing.md#the-instruction-test)). It needs gcc,
m-browser built and the native QEMU:

```sh
# from the qemu-wasi root
make native      # once: a native QEMU from the same tree, in qemu-native/
make test        # tests qemu-build/; make THREADS=1 test tests qemu-build-threads/
```

A run takes seconds and ends with `native: 353 lines, wasm: 353 lines` and
`PASS: all instruction hashes match` (files in `/tmp/qemu-wasi-insn/`).
`make native` configures with `--target-list=x86_64-softmmu --disable-docs
--disable-guest-agent --disable-tools --disable-werror
--with-coroutine=ucontext` and needs QEMU's usual native dependencies: GLib
and zlib development files, and probably pixman's (patch 0005 builds
`ui/console-vc.c` unconditionally). Rerun it after changing QEMU code
outside the WASI `#ifdef`s.

### A boot

In m-browser's window: the desktop appears, and keyboard and mouse work;
Ctrl+Alt+2 and `info status` answer `VM status: running`; in a guest
terminal, `ip addr` shows 10.0.2.15 and `ping -c 3 10.0.2.2` gets replies;
closing the window logs `[qemu] exited with status 0`. The full checklist
is the [manual smoke test](13-testing.md#the-manual-smoke-test).

## Daily workflow

| Command (from the qemu-wasi root) | What it does | When |
|---|---|---|
| `make` (`make all`) | Builds the single-threaded flavor, `qemu-build/qemu-system-x86_64` | After every change |
| `make THREADS=1` | The same for the multi-threaded flavor, in `qemu-build-threads/`; `THREADS=1` works with every target below | After every QEMU change, too |
| `make install` | Builds, then copies to `$(M_BROWSER)/qemu-system-x86_64.wasm` | To try a build in m-browser |
| `make native` | Builds a native QEMU from the same patched tree, in `qemu-native/` | Once; again after changes outside WASI `#ifdef`s |
| `make test` | The instruction test: native QEMU against the selected flavor | After every QEMU change |
| `make reset-qemu` | Resets `qemu-src/` to `v9.1.0` and applies `patches/` again, discarding all changes there | After pulling a changed patch series |
| `make export-patches` | Writes the commits in `qemu-src/` back to `patches/` | After committing a QEMU change in `qemu-src/` |
| `make clean` | Removes the flavor's build directory and its glib-wasi and wasi-stubs outputs | When a build is confused; the next `make` reconfigures |
| `make distclean` | `clean`, plus `qemu-src/`, `qemu-native/`, `zlib-src/`, `zlib-wasi*/` | To start over; the next `make` downloads everything again |

The loop for a QEMU change: edit and commit in `qemu-src/` (never in
`patches/*.patch`), `make` and `make THREADS=1`, `make test`, `make install`,
try it in m-browser, `make export-patches`, then commit `patches/` in
qemu-wasi. [10-changing-qemu.md](10-changing-qemu.md) walks through it.

**Pitfall:** after a `git pull` that changed `patches/`, `make` only warns
and builds the old tree: run `make reset-qemu`, then `make`. The reset
discards unexported work in `qemu-src/`, and exporting after the pull would
overwrite the pulled patches, so export and commit before you pull
([details](10-changing-qemu.md#getting-the-series-onto-another-machine)).

**Pitfall:** `clean` and `distclean` remove only the selected flavor's
build directory: add `make THREADS=1 clean`.

## Where things go

| Path | Contents | Made by | Removed by |
|---|---|---|---|
| `qemu-src/` | QEMU v9.1.0, branch `qemu-wasi`, one commit per patch; meson subprojects in `subprojects/` | The first `make` | `make distclean` |
| `qemu-build/`, `qemu-build-threads/` | The module `qemu-system-x86_64`, objects, `pyvenv/`, `meson-logs/`, `compile_commands.json` | `make`, `make THREADS=1` | `make clean`, per flavor |
| `qemu-native/` | The native QEMU for `make test` | `make native` | `make distclean` |
| `zlib-src/`, `zlib-wasi/`, `zlib-wasi-threads/` | zlib's source and its two wasm builds | The first `make` of each flavor | `make distclean` |
| `glib-wasi/`, `wasi-stubs/` | Next to the sources: `lib*.a`, and objects in `build*/` | Every `make` | `make clean`, per flavor |
| `tools/perf/work/` | Disk copies, logs and screenshots of the perf scripts | `tools/perf/*.sh` | By hand |
| `/tmp/qemu-wasi-insn/` | The instruction test's program, outputs and `diff.txt` | `make test` | Overwritten by each run |
| `../m-browser/qemu-system-x86_64.wasm` | The QEMU m-browser runs (tracked in m-browser's git) | `make install` | |
| `../m-browser/m.qcow2` | The guest's disk, which the guest writes to | You | |
| `../m-browser/downloaded.wasm` | m-browser's copy of the last wasm page it opened | m-browser | |
| `~/.cache/m-browser/qemu-*.cwasm` | QEMU's module compiled to machine code; the 3 newest are kept | m-browser | By hand |

qemu-wasi's git ignores everything above that lies inside qemu-wasi;
m-browser's git ignores `m.qcow2` and `downloaded.wasm`.

## Troubleshooting the first build

| Symptom | Cause | Fix |
|---|---|---|
| Early in the first `make`, zlib's configure prints `Compiler error reporting is too harsh for ./configure` | wasi-sdk is not in `/opt/wasi-sdk` (zlib is the first thing built with it) | Install it ([above](#installing-wasi-sdk)) or pass `WASI_SDK=<dir>`; `ls $WASI_SDK/bin/wasm32-wasip1-clang` must succeed. Then `make` again |
| `Python's ensurepip module is not found`, then `ERROR: python venv creation failed` | No ensurepip, and no pip and setuptools either | `sudo apt install python3-venv` (or `python3-pip python3-setuptools`), then `make` |
| `fatal: unable to access 'https://gitlab.com/...'` (or github.com), or configure fails downloading a subproject | No network, or a proxy or firewall in the way | Allow HTTPS to gitlab.com and github.com (git honours `https_proxy`), then `make` again; `QEMU_GIT` and `ZLIB_GIT` can point at mirrors |
| `ERROR: Cannot find Ninja` | ninja is missing | `sudo apt install ninja-build` |
| Configuring `qemu-build/` fails with `Dependency "glib-2.0" not found` | Probably no pkg-config: the wasm build finds glib-wasi through it | `sudo apt install pkg-config`; details in `qemu-build/meson-logs/meson-log.txt` |
| `make` warns `patches/ changed since qemu-src was patched` | You pulled a new patch series; `qemu-src/` still has the old one | `make reset-qemu` (discards unexported work in `qemu-src/`), then `make` |
| The link fails (wasm-ld rejects `--start-group`) | ninja re-ran meson after a `meson.build` changed, dropping `fix_build_ninja.py`'s edits | `make` again; if that fails, delete the flavor's `build.ninja` and `make` |
| `make native`: `Dependency "glib-2.0" not found` (or `"zlib"`) | GLib or zlib development files are missing | `sudo apt install libglib2.0-dev zlib1g-dev`, then `make native` (it configures again while `qemu-native/build.ninja` is missing) |
| `make native` fails compiling or linking `ui/console-vc.c` | Probably pixman's development files are missing | `sudo apt install libpixman-1-dev`, `rm -rf qemu-native`, `make native` |
| `make install`: `cp: cannot create regular file '.../m-browser/qemu-system-x86_64.wasm'` | m-browser is not next to qemu-wasi | Clone it there, or `make install M_BROWSER=<dir>` |
| `make test`: `error: .../qemu-native/qemu-system-x86_64 not found` (or `.../target/release/m-browser not found`) | No native QEMU, or m-browser not built | `make native`; `cargo build --release` in m-browser |
| m-browser: `QEMU exited during startup` | Started outside the m-browser directory, or no `m.qcow2` there | Start it from there, with `m.qcow2` in place; QEMU's own message is just above |

More: [14-debugging.md](14-debugging.md#the-build-fails) and
[10-changing-qemu.md](10-changing-qemu.md#troubleshooting).

## Next steps

Read on in this order ([README.md](README.md) lists every document; keep
[reference/glossary.md](reference/glossary.md) at hand):

1. [01-overview.md](01-overview.md), [02-concepts.md](02-concepts.md),
   [04-architecture.md](04-architecture.md) and
   [05-repository-tour.md](05-repository-tour.md): the project, its
   background, and how its parts and files fit together.
2. [10-changing-qemu.md](10-changing-qemu.md) before your first QEMU change,
   then [13-testing.md](13-testing.md) and [14-debugging.md](14-debugging.md).
3. As needed: [06](06-patch-series.md) to [09](09-display-input-network.md)
   (patches, TCG backend, threads, display and network),
   [11](11-upgrading-qemu.md), [12](12-adding-features.md),
   [15](15-performance.md), [16](16-guest-image.md), [17](17-history.md),
   [known-issues.md](known-issues.md), and for the host side the m-browser
   repository's docs/06-qemu-runner.md.
