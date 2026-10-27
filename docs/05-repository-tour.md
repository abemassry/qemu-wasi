# Repository tour

**What this covers.** Every file and directory of the qemu-wasi repository:
what it is for, what each build script does step by step, what the two
support libraries (glib-wasi and wasi-stubs) implement and leave out, and
which paths and versions are hardcoded. Read it when you first open the
repository and again before you change the build. The changes to QEMU
itself are described in [06-patch-series.md](06-patch-series.md); terms
are explained in [reference/glossary.md](reference/glossary.md).

## Top level

| Path | What it is |
|---|---|
| `Makefile` | Entry point: fetch, build, install, test (targets below) |
| `README.md` | Project summary, status, quick start |
| `.gitignore` | Ignores the build outputs, `*.a`, `*.o`, `*.d`, `*.wasm`, disk images |
| `patches/` | The QEMU changes as a patch series, `0001`..`0008`, and `BASE`, the upstream tag they apply to (`v9.1.0`) |
| `scripts/` | Build helpers the Makefile runs ([below](#scripts)) |
| `display/` | QEMU display backend that shows the guest screen in the host's window |
| `glib-wasi/` | Minimal replacement for GLib, a C utility library QEMU requires |
| `wasi-stubs/` | Stand-ins for POSIX functions and headers that wasi-libc (the C library of wasi-sdk) lacks |
| `tests/x86-insn-diff/` | Differential instruction test against a native QEMU |
| `docs/` | This documentation; start at [README.md](README.md) |
| `tools/perf/` | Scripts that measure boot time, input latency and guest speed |

The Makefile variables are `WASI_SDK` (default `/opt/wasi-sdk`),
`M_BROWSER` (default `../m-browser`), `NINJA` (default `ninja`) and
`THREADS` (0 or 1: which of the two build flavors). All are exported to the
scripts.

| Target | What it does |
|---|---|
| `make` | Build the libraries, fetch and patch QEMU, configure, build `qemu-build/qemu-system-x86_64` |
| `make THREADS=1` | The same for the multi-threaded flavor in `qemu-build-threads/`; `THREADS=1` works with every target |
| `make install` | Copy the module to `$(M_BROWSER)/qemu-system-x86_64.wasm` (both flavors use this name) |
| `make native` | Build a native QEMU from the same patched tree in `qemu-native/` (needs the host's usual QEMU build dependencies) |
| `make test` | Run `tests/x86-insn-diff/run.sh` against the selected flavor |
| `make reset-qemu`, `make export-patches` | Run `scripts/fetch-qemu.sh --reset` (discards changes in `qemu-src/`), `scripts/export-patches.sh` |
| `make clean`, `make distclean` | Remove `qemu-build[-threads]/` and that flavor's glib-wasi and wasi-stubs outputs; `distclean` also removes `qemu-src/`, `qemu-native/`, `zlib-src/`, `zlib-wasi*/`. **Pitfall:** only the flavor selected by `THREADS` is cleaned; repeat with `THREADS=1` |

What `make` runs, in order (meson is the build-file generator QEMU uses; it
writes `build.ninja`, which the ninja build tool executes):

```text
libs       scripts/build-zlib.sh    -> zlib-wasi[-threads]/lib/libz.a (once)
           make -C glib-wasi        -> glib-wasi/libglib-wasi[-threads].a
           make -C wasi-stubs       -> wasi-stubs/libwasi-stubs[-threads].a
qemu-src   scripts/fetch-qemu.sh    -> qemu-src/ + stamp
                                       (when the stamp is missing or older
                                       than a patch)
configure  scripts/configure.sh     -> qemu-build[-threads]/build.ninja
                                       (when the stamp or the script is newer)
build      scripts/fix_build_ninja.py  (every time; idempotent)
           ninja qemu-system-x86_64 -> qemu-build[-threads]/qemu-system-x86_64
```

The libraries are link arguments, not ninja inputs, so the Makefile deletes
the module when a library is newer than it, which forces a relink.

## Build outputs

None of these are tracked by git.

| Path | Contents |
|---|---|
| `qemu-src/` | QEMU checkout: branch `qemu-wasi`, one commit per patch on top of `v9.1.0`; stamp file `.git/qemu-wasi-patched` |
| `qemu-build/`, `qemu-build-threads/` | Build directories. The result is `qemu-system-x86_64`, a ~40 MB wasm module without a `.wasm` extension. Also `pkgconfig/` (from `configure.sh`), `wasi_gfx_*.o` (the display backend) and `compile_commands.json` (compiler flags per file, for editors) |
| `qemu-native/` | Native QEMU for `make test` |
| `zlib-src/` | zlib checkout, shared by both flavors |
| `zlib-wasi/`, `zlib-wasi-threads/` | `include/`, `lib/libz.a`, `lib/pkgconfig/zlib.pc` |
| `glib-wasi/build*/`, `wasi-stubs/build*/` | Object files; the libraries are written next to each Makefile |

## scripts/

### fetch-qemu.sh

Inputs: `patches/BASE`, `patches/*.patch` and `QEMU_GIT` (default
`https://gitlab.com/qemu-project/qemu.git`). Without arguments:

1. If the stamp `qemu-src/.git/qemu-wasi-patched` exists, it only checks
   whether a patch is newer than the stamp, prints a warning telling you to
   run `make reset-qemu` if so, and exits successfully either way.
2. If `qemu-src/` does not exist, it makes a **shallow** clone of the tag
   (`git clone --depth 1 --branch v9.1.0`) and creates branch `qemu-wasi`.
3. It applies the series with `git am --whitespace=nowarn`, one commit per
   patch. If git has no user identity, a placeholder
   (`qemu-wasi@localhost`) is passed with `-c`, as `git am` needs one.
4. It touches the stamp.

`--reset` aborts a half-finished `git am`, force-resets branch `qemu-wasi`
to the tag, deletes untracked files (`git clean -fd`; ignored files stay),
removes the stamp, then does steps 3 and 4.

**Pitfall:** after pulling a changed series, plain `make` only warns; you
must run `make reset-qemu`, which throws away anything not yet exported.
If `git am` fails half way, no stamp is written and the `am` session stays
open; `make reset-qemu` starts over cleanly.

**Note:** the clone has no history before `v9.1.0`. Fetch the newer
release before comparing with it ([11-upgrading-qemu.md](11-upgrading-qemu.md)).

### export-patches.sh

1. Refuses to run if tracked files in `qemu-src/` have uncommitted changes,
   and warns about untracked files, which are not exported: commit new
   files first.
2. Deletes `patches/*.patch`, so a renamed commit leaves no stale file.
3. Runs `git format-patch --quiet --no-signature --zero-commit
   --base=v9.1.0 -o patches v9.1.0..HEAD`. `--zero-commit` writes
   `From 0000000...` instead of the commit hash, which changes on every
   rebase and would make every file differ; `--no-signature` drops the git
   version trailer; `--base` records `base-commit:` in the first patch.
   File names come from the commit subjects.
4. Touches the stamp, so `fetch-qemu.sh` does not report the new files as
   unapplied. Side effect: the next `make` re-runs `configure.sh`.

### configure.sh

Configures `qemu-src/` into `qemu-build/` (`qemu-build-threads/` with
`THREADS=1`). Inputs: `WASI_SDK`, `THREADS`.

1. Checks that `libglib-wasi$variant.a`, `libwasi-stubs$variant.a` and
   zlib exist (`variant` is empty or `-threads`).
2. Writes `glib-2.0.pc`, `gmodule-2.0.pc` and `gthread-2.0.pc` (pkg-config
   files, which tell a build where a library's headers and archives are)
   into `<build>/pkgconfig/`, filling in `@GLIB_WASI_PREFIX@` and rewriting
   a trailing `-lglib-wasi` to `-lglib-wasi$variant`: the threads build must
   not name, or link the no-op locks of, the single-threaded library.
   `PKG_CONFIG_LIBDIR` is limited to that directory and zlib's, so no
   library of the host system can be picked up.
3. Points `CC`/`CXX` at `$WASI_SDK/bin/<target>-clang[++]` (`wasm32-wasip1`
   or `wasm32-wasip1-threads`) and `AR`, `NM`, etc. at the LLVM tools.
4. Runs QEMU's `configure`:

| Option or flag | Why |
|---|---|
| `--cross-prefix= --cpu=wasm32` | Cross build; the tools are set explicitly |
| `--target-list=x86_64-softmmu` | Only the x86-64 system emulator |
| `--static --disable-pie` | One self-contained module |
| `--without-default-features --enable-tcg --enable-system` | Nothing optional: each library dependency would need porting |
| `--disable-docs --disable-install-blobs` | Firmware comes from m-browser's `pc-bios/` |
| `--with-coroutine=wasi` | The coroutine backend without stack switching (patch 0001) |
| `-D__wasi__ -DWASI_PLATFORM` | Select QEMU's WASI code paths |
| `-D_WASI_EMULATED_{SIGNAL,PROCESS_CLOCKS,MMAN}`, `-lwasi-emulated-*` | wasi-libc's emulations of signals, process clocks and `mmap` |
| `-DWASI_THREADS -pthread` (threads only) | Real threads and multi-threaded TCG |
| `-mllvm -wasm-enable-sjlj -mllvm -wasm-enable-eh -mllvm --wasm-use-legacy-eh=false` | `setjmp`/`longjmp` become standard wasm exceptions; QEMU's `cpu_loop_exit()` relies on them |
| `-I glib-wasi/glib -I glib-wasi -I wasi-stubs -I zlib-wasi*/include` | wasi-stubs' headers come before the sysroot's (wasi-sdk's directory of C headers and libraries); some QEMU headers include `zlib.h` without declaring the dependency |
| `-lglib-wasi$variant -lwasi-stubs$variant` | The support libraries |

5. Runs `fix_build_ninja.py`. QEMU's configure itself clones the meson
   subprojects it needs (`dtc`, `keycodemapdb`, `berkeley-softfloat-3`,
   `berkeley-testfloat-3`), so it needs network access, and creates a
   Python venv in the build directory.

### fix_build_ninja.py

Meson writes a link command for a POSIX linker and knows nothing about
`display/`. This script edits `build.ninja` (path as argument, default
`qemu-build/build.ninja`); it is idempotent and prints what it changed.

1. Before the `qemu-system-x86_64` link it adds build statements compiling
   `display/wasi_gfx_display.c` and `display/wasi_gfx_dcl.c` with the rule,
   `ARGS` and order-only dependencies of `libcommon.a.p/ui_console.c.o`
   (QEMU's `ui/console.c`), plus `-DQEMU_BUILD`.
2. Adds `wasi_gfx_display.o` and `wasi_gfx_dcl.o` to the link inputs.
3. In `LINK_ARGS` it removes `-Wl,--start-group`, `-Wl,--end-group` and
   `-pthread`, and appends `libsetjmp.a` from the sysroot,
   `-Wl,--export-table -Wl,--growable-table` (the host adds compiled
   translation blocks to QEMU's function table), `-Wl,--error-limit=0`,
   `-Wl,--allow-undefined` (host functions become imports),
   `-Wl,-z,stack-size=8388608` (8 MiB main stack) and `-mexec-model=reactor`
   (a WASI reactor: the host calls exported functions repeatedly instead of
   running `main()` once). For the threads build, recognised by
   `wasm32-wasip1-threads-clang` in the file, also `-Wl,--import-memory
   -Wl,--export-memory -Wl,--max-memory=4294967296`.

**Pitfall:** with `--allow-undefined`, a misspelt or missing function does
not fail the link: it becomes an import from `env`, and the host then
refuses to instantiate QEMU ("unknown import"). After adding code, compare
the imports ([reference/host-interface.md](reference/host-interface.md)).

**Pitfall:** when a `meson.build` file changes, ninja may regenerate
`build.ninja` by itself during the build, dropping these edits, and the
link fails. Running `make` again re-applies them.

### build-zlib.sh

Inputs: `WASI_SDK`, `THREADS`, `ZLIB_GIT` (default
`https://github.com/madler/zlib.git`), `ZLIB_TAG` (default `v1.3.1`).

1. Shallow-clones the tag into `zlib-src/` unless that directory exists
   (a new `ZLIB_TAG` has no effect until you delete it).
2. Runs `make distclean` there, as both flavors build in the same tree.
3. Configures with the flavor's clang, `CFLAGS="-O2 -g"` (plus `-pthread`)
   and `--static --prefix=zlib-wasi[-threads]`, builds `libz.a`, installs.

The Makefile runs it only while `zlib-wasi[-threads]/lib/libz.a` is missing.

### gen_interp_calls.py

Prints `tcg/wasm32/wasi-interp-calls.c.inc`, the helper-call dispatcher of
the translation-block interpreter. You almost never need it; see
[07-tcg-backend.md](07-tcg-backend.md#the-helper-call-dispatcher). Run from
the repository root, then commit in `qemu-src/` and export the patches:

```sh
scripts/gen_interp_calls.py > qemu-src/tcg/wasm32/wasi-interp-calls.c.inc
```

## glib-wasi/

QEMU uses GLib for containers, strings, logging and its main loop. Real
GLib does not build for WASI, so glib-wasi implements the subset QEMU uses:
headers in `glib/`, sources in `src/`, `.pc` templates in `pkgconfig/`
(claiming GLib 2.78.0), and a smoke test (`make -C glib-wasi test` builds
`build/test_glib.wasm`).

Implemented: GArray, GPtrArray, GByteArray, GHashTable, GList, GSList,
GQueue, GTree (`garray.c`, `ghash.c`, `glist.c`, `gqueue.c`, `gtree.c`);
`g_malloc` and friends, aborting on failure, with `g_slice_*` as plain
malloc (`gmem.c`); string functions, GString, GBytes, markup escaping
(`gstrfuncs.c`, `gstring.c`, `gmarkup.c`); GError, GQuark, logging and
assertions (`gerror.c`, `gmessages.c`); paths, environment, clocks, base64,
file contents, UTF-8 checks, GTimer, GRand, a minimal GDateTime, and
GMappedFile, which reads the whole file (`gutils.c`, `grand.c`,
`gdatetime.c`, `gmappedfile.c`); locks and threads (`gthread.c`). The
`g_atomic_*` operations in `glib/gatomic.h` are macros doing plain reads
and writes.

Stubbed, with the consequences:

- **Main loop** (`gmain.c`): sources never run, `g_main_loop_run()` returns
  at once, `g_timeout_add()` and `g_idle_add()` never call the function,
  `g_poll()` never waits. QEMU's main loop therefore services its own event
  loops (AioContexts) and sleeps by itself (patch 0004); QEMU code that
  relies on a GLib timeout or idle callback silently does nothing.
- **`g_usleep()`** returns without sleeping.
- **GChecksum, GHmac** produce no digest, and QEMU's `crypto/hash-glib.c`
  is this build's hash backend: anything using QEMU's hash functions
  (LUKS-encrypted disk images, for example) gets zeroed digests, no error.
- `g_spawn_*`, `g_unix_open_pipe()` and `g_uri_parse()` fail; GSequence and
  GData store nothing; user name, home, temporary directory and host name
  are constants (`wasi-user`, `/home`, `/tmp`, `wasi-host`); the gmodule
  and gthread pkg-config modules are empty.

**The threads variant** (`THREADS=1`) is built with
`wasm32-wasip1-threads-clang -DWASI_THREADS -pthread` into `build-threads/`
and `libglib-wasi-threads.a`. GMutex, GRecMutex, GRWLock and GCond wrap
wasi-libc's pthreads (zero-filled static objects are valid, as in GLib),
`g_cond_wait_until()` takes GLib's monotonic deadline, GOnce and
`g_once_init_enter()` make concurrent callers wait for the first one,
GPrivate is a pthread key created on first use, `g_thread_new()` starts a
real thread and `G_LOCK()` locks. In the single-threaded variant all of
these are no-ops and `g_thread_new()` runs the function synchronously.

**Pitfall:** still not thread-safe in the threads variant: the
`g_atomic_*` macros, the GQuark table, the generator behind `g_random_*()`,
the log handler and program name, `g_qsort_with_data()` and GMainContext
reference counts. QEMU uses them from one thread or during start-up; code
you add on vCPU (virtual CPU) threads must use QEMU's `qatomic_*()` and
`QemuMutex` instead.

**Note:** `glib-wasi/Makefile` compiles the single-threaded variant with
`wasm32-wasip2-clang`, although QEMU is `wasm32-wasip1`. It links and works
because glib-wasi only uses libc functions with the same ABI in both;
whether this is intentional is not recorded.

## wasi-stubs/

`libwasi-stubs.a` (or `libwasi-stubs-threads.a`), plus headers that are on
QEMU's include path ahead of the sysroot's. QEMU's `include/qemu/osdep.h`
includes the three `wasi_*.h` headers on WASI.

| File | What it provides |
|---|---|
| `pthread_stubs.c` | Single-threaded only. Overrides wasi-libc's weak stubs, whose `pthread_create()` fails and makes QEMU abort: `pthread_create()` succeeds but never runs the thread, locks do nothing, `pthread_cond_wait()` returns at once, `pthread_cond_timedwait()` times out, keys are a 128-entry table |
| `pthread_threads_stubs.c` | Threads only: what wasi-libc's threads build lacks (`pthread_sigmask`, `pthread_setname_np` succeed; `pthread_[gs]etaffinity_np` return `ENOSYS`) |
| `posix_stubs.c` | `getpid()` returns 1; `shm_open`/`shm_unlink` fail; `msync`, `pthread_atfork` do nothing |
| `socket_stubs.c` | The BSD socket API, failing with `ENOSYS`; the network goes through the host ([09-display-input-network.md](09-display-input-network.md)) |
| `syslog_stub.c` | `syslog()` prints to stderr |
| `wasi_process_stubs.h` | `fork`, `exec*`, `daemon`, `pipe`, `chroot` fail (`ENOSYS`), `waitpid` finds no child; rlimits report "unlimited"; `setuid`, `setsid` and similar succeed without effect; syslog constants |
| `wasi_signal_extras.h`, `wasi_socket_extras.h` | Signal and socket types, constants and declarations missing from the sysroot |
| `dirent.h`, `grp.h`, `pwd.h`, `termios.h` | Missing `DT_*` constants (wraps the sysroot header); user and group lookups that find nothing; terminal definitions whose functions fail |
| `net/if.h`, `sys/ioctl.h`, `sys/un.h`, `sys/wait.h` | Interface names; an empty header; `sockaddr_un` with `sun_path`; wait-status macros |

## display/

The display backend that hands frames to the host and fetches input from
it, compiled by `fix_build_ninja.py`; details in
[09-display-input-network.md](09-display-input-network.md).

| File | Contents |
|---|---|
| `wasi_gfx_dcl.c` | QEMU glue: the display listener, console choice (`WASI_GFX_CONSOLE`) and switching (`wasi_gfx_display_switch_console()`), refresh (`QEMU_DISPLAY_STATS`, `WASI_GFX_FULL_REFRESH`); `wasi_gfx_display_register_dcl()` is called from `system/vl.c` |
| `wasi_gfx_display.c` | Frame copy and scaling (`WASI_GFX_SCALE`), the `host_gfx_*` imports, input events, the Ctrl+Alt+n console hotkeys |
| `wasi_gfx_display.h` | Interface between the two |

## tests/x86-insn-diff/

`insn-test.c` is a bare-metal x86-64 program (booted with QEMU's
`tests/tcg/x86_64/system/boot.S` and minilib) that runs several hundred
instruction forms over many inputs and prints a hash per form. `run.sh`
compiles it with the host's gcc, runs it under the native QEMU and under
the wasm QEMU (through m-browser's `--qemu-headless`), and diffs the
output. Inputs: `NATIVE_QEMU`, `QEMU_WASM`, `M_BROWSER`, `OUT` (default
`${TMPDIR:-/tmp}/qemu-wasi-insn`), `WASM_TIMEOUT` (default 900 s). See
[13-testing.md](13-testing.md).

## tools/perf/

Measurements with a real guest, explained in
[15-performance.md](15-performance.md) and
[16-guest-image.md](16-guest-image.md): `common.sh` (shared settings),
`prepare-base.sh` (copies a disk image, extracts its kernel and initramfs
for direct boots), `guest-shell.sh` (boots into a root shell you can type
into), `mkbase.sh` (applies a file of guest commands to a new overlay
image), `guest/` (the `qemu-profile.start` guest logger, from which
`prepare-base.sh` generates the commands that install it; `chromium-fast.cmds`;
the guest benchmark `bench.sh`), `boot-timing.sh` (boots like m-browser's
GUI, records the serial log and screenshots), `click-latency.sh` with
`menupx.py` (time from a click to Chromium's menu), `analyze-serial.py`
(summarizes a guest log), and `bench-run.sh` (runs the guest benchmark).
Details in [tools/perf/README.md](../tools/perf/README.md).

## Hardcoded paths and assumptions

| Assumption | Where | To change it |
|---|---|---|
| wasi-sdk in `/opt/wasi-sdk`, tested with wasi-sdk 29 | Makefile, `configure.sh`, `build-zlib.sh`, `fix_build_ninja.py`, both library Makefiles | Set `WASI_SDK` |
| m-browser in `../m-browser` | Makefile, `run.sh` | Set `M_BROWSER` |
| QEMU from gitlab.com at the tag in `patches/BASE` | `fetch-qemu.sh` | `QEMU_GIT`; `BASE` ([11-upgrading-qemu.md](11-upgrading-qemu.md)) |
| zlib v1.3.1 from GitHub | `build-zlib.sh` | `ZLIB_TAG`, `ZLIB_GIT`; delete `zlib-src/` |
| Only `x86_64-softmmu`, i.e. `qemu-system-x86_64` | `configure.sh`, Makefile (`native`, ninja target), `TARGET` in `fix_build_ninja.py` | All of them |
| Display compiled with the flags of `libcommon.a.p/ui_console.c.o` | `FLAGS_FROM` in `fix_build_ninja.py` | Update if QEMU moves `ui/console.c` |
| Display sources listed by name | `DISPLAY_SOURCES` in `fix_build_ninja.py` | Add new `display/*.c` there |
| `libsetjmp.a` in `share/wasi-sysroot/lib/<target>/` | `fix_build_ninja.py` | Follow the wasi-sdk layout |
| 8 MiB main stack; 4 GiB maximum memory (threads) | `fix_build_ninja.py` | Link flags there |
| Threads flavor recognised by `wasm32-wasip1-threads-clang` in `build.ninja` | `fix_build_ninja.py` | Keep the compiler name |
| Helper signatures of at most 8 parameters | `MAX_PARAMS` in `gen_interp_calls.py` | Regenerate the dispatcher |
