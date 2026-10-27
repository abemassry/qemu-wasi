# Upgrading QEMU

**What this covers.** How to move qemu-wasi from QEMU 9.1 to a newer
release: preparation and a baseline, rebasing the eight patches in
`qemu-src/`, porting each patch, what outside the patches to re-check,
verification and finishing, and how to upgrade wasi-sdk, zlib and
wasmtime. Read [06-patch-series.md](06-patch-series.md) (the patches) and
[10-changing-qemu.md](10-changing-qemu.md) (the patch workflow) first.

Statements about upstream QEMU marked *(unverified)* were written from
memory. Check them in QEMU's ChangeLog, one page per release
(`https://wiki.qemu.org/ChangeLog/<version>`, for example
`https://wiki.qemu.org/ChangeLog/9.2`), and in the history of the files.

## Overview of the strategy

The repository holds no copy of QEMU, only `patches/BASE` (the release tag,
`v9.1.0`) and eight patches, which `qemu-src/` holds as eight commits on
branch `qemu-wasi`. Upgrading means *rebasing* them: git replays each
commit's change on top of the new tag, and where upstream changed the same
lines it stops with a *conflict*, for you to combine the two by hand.

```text
before                                after
v9.1.0 -- 0001 -- ... -- 0008         vX.Y.Z -- 0001' -- ... -- 0008'
branch qemu-wasi-v9.1.0 (kept)        branch upgrade-vX.Y.Z
patches/BASE = v9.1.0                 patches/BASE = vX.Y.Z, patches/ exported
```

Three rules apply throughout. **One patch at a time:** the rebase stops
after each patch for the checks possible then (0001 must configure, 0002's
TCG files compile, from 0007 on the tests pass;
[step 4](#4-check-each-patch-as-it-is-applied)), so an error shows up next
to its cause. **Keep each patch's purpose, not its text:** redo a hunk's
intent in code upstream rewrote; tracing lines (`WASI_LOG()`) may be
dropped. **Change nothing else at the same time** (wasi-sdk, zlib,
wasmtime: [last section](#upgrading-other-components)).

### One release at a time, or a jump?

| Approach | For | Against |
|---|---|---|
| One release at a time (9.1, 9.2, 10.0, ...) | Smaller conflicts, each explained by one ChangeLog; a regression is pinned to one release | Code that a later release rewrites again is resolved twice; every step needs builds and tests |
| Straight to the newest release | One round of conflicts, one verification | Large, mixed conflicts; when something breaks you do not know which release caused it |

Recommended: rebase release by release, one branch per step
(`upgrade-v9.2.4`, then `upgrade-v10.0.3` made from it, ...), with both
builds and the instruction test at each step and the full
[verification](#verification) at the end; the intermediate branches then
show which release caused a regression. In each step the "old" tag below
is the previous step's. Prefer the newest point release of each version
(the highest `vX.Y.N`): point releases only add bug fixes.

**What to expect.** 0001 and 0003 to 0007 are mostly mechanical (busy
files, re-indented meson blocks, tracing to drop, a few copies of upstream
functions to compare by hand). 0002, the TCG backend, is the hardest: the
backend interface was reworked in 2025 *(unverified)*, so `tcg/wasm32/`
needs a real port. 0008 is next: it edits lines of 0002, 0003, 0004 and
0007 again.

## Preparation

The first commands need the new tag: fetch it first
([procedure step 1](#1-fetch-the-new-tag)), which changes nothing else.

1. **Read the ChangeLogs** of every release between the two, and what
   upstream deprecated and removed:

   ```sh
   # from qemu-src/
   git diff v9.1.0 vX.Y.Z -- docs/about/deprecated.rst docs/about/removed-features.rst
   ```

   Look for: TCG (backend interface, host support); build requirements
   (GLib, Python, meson); renamed directories; main loop, coroutines, RCU;
   consoles and input; networking; and removed command-line options, as
   m-browser builds QEMU's command line (its `src/app.rs`). 9.1's
   `deprecated.rst` already announces the end of system emulation on
   32-bit x86 hosts, and in 2025 upstream was removing 32-bit hosts
   altogether *(unverified)*. wasm32 is a 32-bit host: if the new release
   refuses it, decide on a strategy (carry the support, move to wasm64, or
   stay) before porting anything.
2. **Look for upstream WebAssembly support.** Kohei Tokunaga's work to run
   QEMU in browsers (an Emscripten build using TCI, QEMU's TCG
   interpreter, plus a fiber-based coroutine backend) was merged around
   QEMU 10.1, and a WebAssembly TCG backend was proposed on the mailing
   list afterwards *(unverified)*; our backend descends from his earlier
   Emscripten one.

   ```sh
   # from qemu-src/
   git ls-tree --name-only vX.Y.Z tcg/          # a wasm, wasm32 or wasm64 directory?
   git ls-tree --name-only vX.Y.Z util/ | grep coroutine
   git grep -n -i 'emscripten\|wasm' vX.Y.Z -- configure meson.build
   ```

   If upstream has such pieces, compare them with ours before porting:
   they may replace parts of 0001 (build system) and 0002 (backend; the old
   Emscripten `util/coroutine-fiber.c`). The WASI-specific parts stay.
3. **Check that the current build works, and record a baseline**, without
   which you cannot tell a regression from an old problem (`wasm-tools`:
   see [reference/host-interface.md](reference/host-interface.md#checking-a-builds-interface)):

   ```sh
   # from the qemu-wasi root
   make && make THREADS=1 && make native
   make test && make THREADS=1 test
   QEMU_JIT_HOT=0 make test && QEMU_JIT_HOT=4000000000 make test
   QEMU_JIT_HOT=0 make THREADS=1 test && QEMU_JIT_HOT=4000000000 make THREADS=1 test
   mkdir -p ~/qemu-baseline
   cp qemu-build/qemu-system-x86_64 ~/qemu-baseline/single.wasm
   cp qemu-build-threads/qemu-system-x86_64 ~/qemu-baseline/threads.wasm
   cp qemu-build/config-host.h ~/qemu-baseline/config-host-single.h
   cp qemu-build-threads/config-host.h ~/qemu-baseline/config-host-threads.h
   for f in single threads; do
       wasm-tools print --skeleton ~/qemu-baseline/$f.wasm |
           grep -E '\((import|export)' > ~/qemu-baseline/$f-interface.txt
   done
   ```

   Then time a boot and Chromium's click latency with each saved module
   ([tools/perf/README.md](../tools/perf/README.md),
   [15-performance.md](15-performance.md)). A run takes about 15 minutes;
   run them one after the other, as parallel runs slow each other down:

   ```sh
   # from the qemu-wasi root; prepare-base.sh only the first time
   tools/perf/prepare-base.sh
   CLICKS=1 QEMU_WASM=~/qemu-baseline/single.wasm \
       tools/perf/boot-timing.sh base-single 2>&1 | tee ~/qemu-baseline/boot-single.txt
   CLICKS=1 QEMU_WASM=~/qemu-baseline/threads.wasm \
       tools/perf/boot-timing.sh base-threads 2>&1 | tee ~/qemu-baseline/boot-threads.txt
   ```

4. **Make the current state safe.** All work in `qemu-src/` must be
   exported and committed, and the old series gets a name of its own:

   ```sh
   # from the qemu-wasi root
   make export-patches                   # refuses if qemu-src/ has uncommitted changes
   git status --short                    # nothing to commit
   git checkout -b upgrade-qemu          # the upgrade's qemu-wasi commits go here
   git -C qemu-src rev-parse HEAD        # write it down: the tested old series
   git -C qemu-src branch qemu-wasi-v9.1.0    # safety copy of the old series
   ```

   If `git status` lists changed patches, `qemu-src/` and `patches/`
   disagree: sort that out first
   ([10](10-changing-qemu.md#getting-the-series-onto-another-machine)).
   Untracked logs in `tools/perf/work/` do not matter.

5. **Disk space, network, tools.** On 9.1, `qemu-src/` takes 0.2 GB,
   each wasm build directory 0.16 GB and `qemu-native/` 0.3 GB; the
   clean-room check needs a second set. Configuring downloads meson
   subprojects, so the network must work. Keep wasi-sdk as it is (tested:
   wasi-sdk 29). The native build needs the new release's host
   requirements (its `docs/about/build-platforms.rst`).

## The procedure

Commands use `vX.Y.Z` for the new tag (for example `v9.2.4`) and `v9.1.0`
for the old one, which in later upgrades is whatever `patches/BASE` says.

### 1. Fetch the new tag

`qemu-src/` is a *shallow* clone: it has the `v9.1.0` commit but none of
the history before it, and its remote is set up to fetch only that tag. So
name the new tag explicitly:

```sh
# from qemu-src/
git ls-remote --tags origin 'v9.2*'       # the tags that exist (-rc: release candidates)
git fetch --depth=1 origin tag vX.Y.Z     # the release, without history
git log --oneline -1 vX.Y.Z               # check that it arrived
```

That is enough to rebase, and to compare files between the releases with
`git diff v9.1.0 vX.Y.Z -- <file>`. To read upstream's history in between
(`git log`, `git blame`), fetch all of it once; this takes a while and
several hundred MB *(unverified)*:

```sh
# from qemu-src/
git fetch --unshallow origin tag vX.Y.Z
git rev-parse --is-shallow-repository     # false: the history is complete
```

### 2. Prepare the working branch

```sh
# from the qemu-wasi root
rm -rf qemu-build qemu-build-threads qemu-native
cd qemu-src
git checkout -b upgrade-vX.Y.Z qemu-wasi
git config merge.conflictStyle diff3        # settings of this clone only
git config rerere.enabled true
git status --short --ignored subprojects/   # "!!" lines: downloaded by configure
rm -rf subprojects/berkeley-softfloat-3 subprojects/berkeley-testfloat-3 \
       subprojects/dtc subprojects/keycodemapdb
```

Old build directories would mix the releases. `diff3` adds the *base* to
every conflict (the text before either side changed it), which shows what
each side did. `rerere` ("reuse recorded resolution") fills in your
earlier resolutions if you abort and restart the rebase; check them and
`git add` (`git rerere forget <file>` drops a wrong one). Configure
downloads a meson subproject only when its directory is missing: delete
whatever the `!!` lines list, or the old versions stay.

### 3. Start the rebase

```sh
# from qemu-src/
GIT_SEQUENCE_EDITOR="sed -i -e 's/^pick /edit /'" \
    git rebase -i --onto vX.Y.Z v9.1.0 upgrade-vX.Y.Z
```

`--onto vX.Y.Z v9.1.0 upgrade-vX.Y.Z` means: replay the commits of
`upgrade-vX.Y.Z` after `v9.1.0` (the eight patches) on top of `vX.Y.Z`.
`-i` makes the rebase follow a to-do list, in which `sed` turns every
`pick` into `edit`, so git stops after each patch. Checking out the new
release rewrites nearly every file: the first build is a full one.

**Pitfall:** while the rebase runs, do not run `make reset-qemu` or
`make distclean`, which throw it away, nor `make export-patches`, which
would export half a series. A plain `make` is safe.

### 4. Check each patch as it is applied

At every stop, `git status` says which case you are in:

- *"Unmerged paths"* (files marked "both modified" and so on): the patch
  conflicts. Resolve it ([below](#resolving-a-conflict)), `git add` the
  files, run the check, then `git rebase --continue` (if an editor opens
  with the message, save and close it). What you fix before continuing
  becomes part of this patch.
- *"You are currently editing a commit"*: the patch applied cleanly. Run
  the check; to fix something, edit, `git add`,
  `git commit --amend --no-edit`. Then `git rebase --continue`.

| Stop after | Check, from the qemu-wasi root | Expect |
|---|---|---|
| 0001 | `make libs`, then `scripts/configure.sh` | Configure finishes and `fix_build_ninja.py` lists its changes. Do not compile: there is no TCG backend yet |
| 0002 to 0006 | `make`; to see all errors at once, `ninja -C qemu-build -k 0 qemu-system-x86_64` | Errors in the files this patch touches are fixed now (for 0002: everything in `tcg/`). Errors a later patch fixes are expected, such as `_Noreturn` in `include/hw/core/tcg-cpu-ops.h` until 0003 |
| 0007 | `make`, `make native`, `make test` | The single-threaded flavor is complete: everything compiles, `PASS` |
| 0008 | `make`, `make test`, `make THREADS=1`, `make THREADS=1 test` | Both flavors build and pass |

Only the complete series is tested
([06](06-patch-series.md#how-the-patches-depend-on-each-other)), so an
intermediate failure is a hint, not a verdict: if it lies in code a later
patch changes anyway, note it and go on. When a patch changes a
`meson.build`, ninja re-runs meson by itself, dropping
`fix_build_ninja.py`'s edits, and the link fails: run `make` again
([10](10-changing-qemu.md#building-after-an-edit)).

### Resolving a conflict

1. `git status` lists the files ("both modified"; "deleted by us" when
   upstream removed or renamed the file). Find out what each side wanted:

   ```sh
   # from qemu-src/
   git show REBASE_HEAD -- <file>         # what the patch changed (-w: ignore indentation)
   git diff v9.1.0 vX.Y.Z -- <file>       # what upstream changed between the releases
   git log --oneline v9.1.0..vX.Y.Z -- <file>   # and why (needs the full history)
   ```

   `REBASE_HEAD` is the original commit being replayed.
2. Edit the file. With `diff3`, a conflict looks like this:

   ```text
   <<<<<<< HEAD
   upstream's new code (with the patches applied so far)
   ||||||| parent of 5db9748 (tcg/wasm32: add a WebAssembly backend ...)
   the old code the patch was written against
   =======
   the patch's version of the old code
   >>>>>>> 5db9748 (tcg/wasm32: add a WebAssembly backend ...)
   ```

   Start from the first part and redo in it what the patch changed between
   the second and the third part; delete the markers.
3. `git add <file>`. For a file upstream renamed or removed, apply the
   patch's hunk to the new location by hand, then `git rm <old path>` and
   `git add <new path>`. No conflict markers may be left; this prints
   nothing: `git grep -n -e '^<<<<<<<' -e '^>>>>>>>'`.

In a rebase, *ours* is the new base (upstream plus the patches applied so
far) and *theirs* is the patch, the opposite of a merge: so
`git checkout --ours -- <file>` keeps upstream's version, dropping the
patch's changes to it. `git rebase --abort` undoes the whole rebase.

### 5. After the last patch

```sh
# from qemu-src/
git log --oneline vX.Y.Z..HEAD                 # the eight patches
git grep -n -e '^<<<<<<<' -e '^>>>>>>>'        # no conflict markers anywhere
git diff vX.Y.Z HEAD | grep -n '^+.*sysemu/'   # paths into a renamed directory?
git grep -n CONFIG_TCG_INTERPRETER -- '*.c' '*.h' '*.inc' | grep -v WASI_PLATFORM
```

The last command lists places that treat TCI specially and that the series
did not extend to WASI. The wasm32 backend resembles TCI (no generated
prologue, a C `tcg_qemu_tb_exec()`, `GETPC()` from `tci_tb_ptr`, no
executable code buffer), so a place new in this release may need the
`|| defined(EMSCRIPTEN) || defined(WASI_PLATFORM)` of its neighbours. In
9.1 the list holds only `#endif` comments and TCI-only (libffi, Int128),
Emscripten-only or wasm32-compatible places.

### 6. Update BASE and export

```sh
# from the qemu-wasi root
echo vX.Y.Z > patches/BASE
make export-patches
tail -1 patches/0001-*.patch                   # base-commit: <hash>
git -C qemu-src rev-parse 'vX.Y.Z^{commit}'    # the same hash
```

**Pitfall:** update `patches/BASE` *before* exporting. The export writes
the commits between `BASE` and `HEAD`; with the old tag there, it fails
or writes upstream's changes as patches (`git checkout -- patches/`
restores the files).

Commit `patches/` on the `upgrade-qemu` branch whenever a stage works; the
commits can be squashed before merging.

### 7. Build everything and fix what fails

```sh
# from the qemu-wasi root
make && make THREADS=1 && make native
make test && make THREADS=1 test
```

Fix QEMU problems in `qemu-src/` as fixup commits of the right patch, as
in [10-changing-qemu.md](10-changing-qemu.md#changing-an-existing-patch),
but with the new base:
`GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash vX.Y.Z`. Fixes
outside QEMU are ordinary qemu-wasi commits
([Outside the patches](#outside-the-patches)). Export again afterwards.

### 8. Clean-room check

This proves the committed files suffice, as on a new machine:
`fetch-qemu.sh` clones the new tag and `git am` applies the exported
patches. The work copy moves out of the repository, whose `.gitignore`
does not cover it.

```sh
# from the qemu-wasi root
mv qemu-src ../qemu-src.work
rm -rf qemu-build qemu-build-threads qemu-native
make && make THREADS=1
echo /subprojects/.wraplock >> qemu-src/.git/info/exclude
git -C qemu-src rev-parse 'HEAD^{tree}'
git -C ../qemu-src.work rev-parse 'HEAD^{tree}'    # must print the same hash
make native && make test && make THREADS=1 test
```

A tree hash identifies the content of every file, whatever the commit
dates: equal hashes mean the export lost nothing. Keep the new `qemu-src/`
(branch `qemu-wasi`); delete `../qemu-src.work` after the review below.

### 9. Review the result with range-diff

```sh
# from ../qemu-src.work/ (it has both series)
git range-diff v9.1.0..qemu-wasi-v9.1.0 vX.Y.Z..upgrade-vX.Y.Z
```

`git range-diff` pairs each old patch with its new version: `=` means
unchanged, `!` changed, followed by a "diff of the diffs": its first
column compares the two (`-` old patch only, `+` new only), the second is
the patch's own. Each difference should come from an upstream change.

### Alternative: git am -3 on a fresh checkout

`git am -3` applies the patch files to a new clone of the tag, merging
three-way like the rebase, but only where it finds the file version a
patch was made against (fetch `v9.1.0` for those):

```sh
# from the qemu-wasi root
git clone --depth 1 --branch vX.Y.Z https://gitlab.com/qemu-project/qemu.git ../qemu-am
git -C ../qemu-am fetch --depth=1 origin tag v9.1.0
git -C ../qemu-am checkout -b qemu-wasi
git -C ../qemu-am am -3 "$PWD"/patches/*.patch
```

Resolve as above, then `git am --continue`. A patch changing a file an
earlier patch also changed may stop with "could not build fake ancestor"
and must be applied by hand (`git am --show-current-patch=diff`). So
prefer the rebase; even without a `qemu-src/`, `make` recreates one at
the old tag.

## Porting guide, patch by patch

[06-patch-series.md](06-patch-series.md#the-patches) has each patch's
files, design decisions and "Likely conflicts". This adds what to do.

**0001, build and portability** (configure and meson accept wasm32-wasi;
the portability layer copes without signals, processes, a real `mmap()`
or stack switching):

- `configure`: three small hunks (`check_define __wasi__` giving
  `host_os=wasi`; `wasm32)` in the `case "$cpu"` list; `if false &&`
  before "Unrecognized host OS"). Re-add them wherever the code moved.
- `meson.build`: `'wasm32'` in `supported_cpus`; the compiler-version and
  SafeStack probes indented into `if cpu != 'wasm32'` blocks; the
  coroutine backend list. `git show -w REBASE_HEAD -- meson.build` hides
  the indentation and shows the real change; on conflict, take upstream's
  new block and indent it again. If upstream now has a wasm or Emscripten
  coroutine backend, keep its structure; what matters is that `wasi` is
  the only backend on WASI and is listed in `meson_options.txt`.
- `include/qemu/osdep.h`: `sigsetjmp()`/`siglongjmp()` must stay macros
  ([06](06-patch-series.md#0001-wasi-build-qemu-for-wasm32-wasi) says
  why). `include/qemu/atomic.h`: comment out the `ATOMIC_REG_SIZE`
  assertion in every macro that has one.
- `include/sysemu/os-posix.h`: 10.0 renamed `include/sysemu/` to
  `include/system/` *(unverified)*. git usually follows a rename; if not,
  the file is "deleted by us" ([Resolving a conflict](#resolving-a-conflict),
  step 3). Step 5 finds the `#include "sysemu/runstate.h"` lines that 0003
  and 0007 add, which do not move by themselves.
- `util/coroutine-wasi.c` must still match `include/qemu/coroutine_int.h`;
  to see what changed:
  `git diff v9.1.0 vX.Y.Z -- include/qemu/coroutine_int.h util/coroutine-ucontext.c`.

**0002, TCG backend** (code generator, interpreter, JIT interface, and
hooks in generic TCG code):

- Generic hooks, listed in
  [07](07-tcg-backend.md#generic-qemu-files-changed-for-the-backend):
  mainly `tcg/tcg.c` (the calls into the backend at labels and at the
  start and end of a TB, no prologue) and `tcg/region.c` (the
  `aligned_alloc()` code buffer, `wasi_tb_cache_flush()`). `GETPC()` is
  in `include/exec/exec-all.h` in 9.1; to find its new home:
  `git grep -n 'define GETPC' vX.Y.Z`.
- The backend interface: in 2025 upstream replaced the big `tcg_out_op()`
  switch and the `TCG_TARGET_HAS_*` macros with per-opcode structures
  *(unverified)*. Port with the
  [hook checklist](07-tcg-backend.md#porting-checklist-what-tcgtcgc-needs)
  and the [things to preserve](07-tcg-backend.md#things-a-porter-must-preserve).
  TCI is the closest relative: `git diff v9.1.0 vX.Y.Z -- tcg/tci/` shows
  how such a backend moved, and with the full history,
  `git log --reverse --oneline v9.1.0..vX.Y.Z -- tcg/tci/` lists the
  changes one by one, to repeat in `tcg/wasm32/`.
- Keep the port close to the old structure (same function names, code
  moved rather than rewritten): 0008 edits these files again, and every
  moved line becomes a conflict there.
- `accel/tcg/ldst_common.c.inc`: every hunk is Emscripten-only except an
  `#include "../../tcg/wasm32.h"` that WASI does not appear to need
  (from reading the code). On conflict, `git checkout --ours` the file
  (dropping the hunks) is reasonable; the build will tell.
- Helper calls: `scripts/gen_interp_calls.py` handles up to 8 wasm
  parameters, enough for `MAX_CALL_IARGS` 7 (`include/tcg/helper-info.h`)
  plus a hidden Int128 result pointer. If the new release raises
  `MAX_CALL_IARGS`, raise `MAX_PARAMS` and regenerate
  ([07](07-tcg-backend.md#the-helper-call-dispatcher)).

**0003, vCPU inline** (the main loop runs the vCPU in the single-threaded
build):

- `accel/tcg/cpu-exec.c` conflicts often (`cpu_tb_exec()`,
  `HELPER(lookup_tb_ptr)`, `cpu_handle_interrupt()`, `cpu_exec_loop()`).
  `WASI_LOG_V()` lines and the debugging aids may be dropped on conflict;
  the time-slice exit in `cpu_handle_interrupt()` may not.
- Silent: `wasi_rr_cpu_step()` and the WASI branch of
  `rr_start_vcpu_thread()` copy upstream's `rr_cpu_thread_fn()`. Read
  `git diff v9.1.0 vX.Y.Z -- accel/tcg/tcg-accel-ops-rr.c` and carry each
  change to that function over by hand.
- `accel/tcg/cputlb.c`: the `io_prepare()` bypass of `cpu_io_recompile()`
  must survive, or the vCPU loops forever on an I/O access.
- `include/hw/core/tcg-cpu-ops.h` only works around glib-wasi's
  `#define G_NORETURN _Noreturn` (`glib-wasi/glib/glib.h`); defining it as
  `__attribute__((__noreturn__))` there may make the hunk unnecessary
  (untested).

**0004, main loop and RCU.** `main_loop_wait()` in `util/main-loop.c`
(sleeping, direct `aio_poll()` of both contexts), the BH and coroutine
shortcuts in `util/async.c`, `block/graph-lock.c`, `util/thread-pool.c`.
`rcu_run_callbacks_inline()` in `util/rcu.c` uses `rcu.c` internals
(`try_dequeue()`, `rcu_call_count`, `get_ptr_rcu_reader()`): if they
changed, rewrite it after the new `call_rcu_thread()`.

**0005, text consoles.** `include/ui/pixman-minimal.h` and the
`console-vc.c` line in `ui/meson.build` (always the real console, never
the stubs). Few conflicts expected.

**0006, user network.** The `net_client_init_fun[]` table in `net/net.c`
and its include of `net/wasi-user.c.inc`. Silent:
`wasi_user_unsupported_option()` must name every field of
`NetdevUserOptions` (in `qapi/net.json`) except `hostfwd`, which is read
as a list of `String` (`fwd->value->str`). See what changed with
`git diff v9.1.0 vX.Y.Z -- qapi/net.json`.

**0007, reactor.** Most `system/vl.c` hunks only add `WASI_LOG()` lines
([06](06-patch-series.md#0007-system-run-qemu-as-a-wasi-reactor)): on
conflict, drop them and keep the three real changes (an unreadable
default config file is ignored; `wasi_gfx_display_register_dcl()` in
`qemu_init_displays()`; `user` as the default network).
`qemu_main_loop_step()` in `system/runstate.c` repeats
`qemu_main_loop()`'s body: compare by hand. `system/main.c` (the exports)
and `monitor/monitor.c` (no QMP dispatcher) are small;
`include/sysemu/sysemu.h` may have moved (0001).

**0008, threads.** Where 0002, 0003, 0004 or 0007 conflicted, expect 0008
to conflict again: it turns their `#ifdef WASI_PLATFORM` lines into
`#if defined(WASI_PLATFORM) && !defined(WASI_THREADS)`. Also:

- `wasi_vcpu_step()` in `accel/tcg/tcg-accel-ops-mttcg.c` copies the loop
  body of `mttcg_cpu_thread_fn()`: carry upstream changes over by hand.
- The backend hunks (`atomic.fence` in the `INDEX_op_mb` case of
  `tcg_out_op()`, `TCG_TARGET_DEFAULT_MO` in `tcg-target.h`, the shared
  memory import, `WASI_SLOTS`) go wherever the 0002 port moved that code.
- `apic_timer_update()` in `hw/intc/apic.c`; `qemu_thread_create()` in
  `util/qemu-thread-posix.c` (16 MiB stacks); the main-loop wake
  (`aio_notify()` calls `wasi_main_wake()`, which calls the
  `host_main_wake` import).
- The `CONFIG_ATOMIC64` block in `meson.build`: see what the new release
  does with the flag (`git grep -n CONFIG_ATOMIC64 vX.Y.Z`); if it went
  with 32-bit hosts, the block may become unnecessary.

## Outside the patches

What in qemu-wasi and m-browser depends on QEMU, often silently:

| What | Where | Check |
|---|---|---|
| Configure results | `qemu-build/config-host.h`, `qemu-build-threads/config-host.h` | `diff ~/qemu-baseline/config-host-single.h qemu-build/config-host.h` (and threads): a `CONFIG_` that disappeared may be a meson check that now fails on wasm32, as `CONFIG_ATOMIC64` did ([14-debugging.md](14-debugging.md#the-build-fails)) |
| Display compile flags | `FLAGS_FROM` in `scripts/fix_build_ninja.py` | `grep -c '^build libcommon.a.p/ui_console.c.o:' qemu-build/build.ninja` prints 1; otherwise find `ui_console.c.o` in `build.ninja` and use its new name |
| Other build fix-ups | `DISPLAY_SOURCES`, `DROP_FLAGS`, `TARGET` in the same script | A new link flag wasm-ld rejects goes into `DROP_FLAGS` |
| Configure options | `scripts/configure.sh`; the `native` target in the Makefile | `qemu-src/configure --help` still lists each option they pass |
| GLib version | `glib_req_ver` in `qemu-src/meson.build` (9.1: `>=2.66.0`); glib-wasi claims 2.78.0 in `glib-wasi/pkgconfig/*.pc` but 2.56 in `glib-wasi/glib/glib.h` | Configure fails once QEMU needs more than 2.78.0. The header version decides which fallbacks QEMU's `include/glib-compat.h` compiles (`GLIB_CHECK_VERSION`); raising it means glib-wasi must provide what they did |
| New GLib functions | `glib-wasi/` | A missing declaration fails the compile ("implicit declaration of function"); a declared but missing function does not fail the link (`--allow-undefined`), it becomes an `env` import. Implement it in glib-wasi |
| New POSIX functions, headers | `wasi-stubs/` | The same two symptoms; add the header, declaration or stub there ([05](05-repository-tour.md#wasi-stubs)) |
| Meson subprojects | `qemu-src/subprojects/*.wrap` | Configure clones them: a new one needs network access and must build for wasm32 |
| Display API | `display/wasi_gfx_dcl.c`, `display/wasi_gfx_display.c` | They use `DisplayChangeListenerOps` (`dpy_refresh`, `dpy_gfx_update`, `dpy_gfx_switch`), `register_displaychangelistener()`, `qemu_console_*()`, `qemu_input_*()`, `qemu_text_console_put_*()`. Changes show as compile errors in `wasi_gfx_*.o`; read `git diff v9.1.0 vX.Y.Z -- include/ui/` |
| Host interface | `env` imports, the exports | Compare `wasm-tools print --skeleton` of the new modules with the baseline; any change needs m-browser in step ([reference/host-interface.md](reference/host-interface.md)) |
| Firmware | `m-browser/pc-bios/` | An exact copy of these 9.1 `pc-bios/` files: `bios-256k.bin`, six `vgabios*.bin`, `efi-e1000.rom`, `efi-virtio.rom`, `kvmvapic.bin`, `linuxboot.bin`, `linuxboot_dma.bin`, `multiboot.bin`, `multiboot_dma.bin`, `pvh.bin`, `keymaps/`. Replace them from the new release ([Verification](#verification), step 2) |
| QEMU command line | m-browser's `src/app.rs`, `tools/perf/` scripts | No option they pass was removed or changed |
| Instruction test | `tests/x86-insn-diff/run.sh` | It uses `qemu-src/tests/tcg/minilib/` and `tests/tcg/x86_64/system/boot.S` and `kernel.ld`: follow them if they move |

## Verification

All of these, for both flavors unless noted, compared with the baseline:

1. The instruction test in its three modes: `make test`,
   `QEMU_JIT_HOT=0 make test` (compiled code only),
   `QEMU_JIT_HOT=4000000000 make test` (interpreter only), and the same
   with `THREADS=1` ([13-testing.md](13-testing.md)).
2. The modules' imports and exports equal the baseline's, or differ only
   as intended. Install a module and the new release's firmware into
   m-browser (the loop reports files the new release no longer has):

   ```sh
   # from the qemu-wasi root, with m-browser in ../m-browser (the default)
   make install                      # or make THREADS=1 install: the last one wins
   for f in ../m-browser/pc-bios/*; do
       cp -r "qemu-src/pc-bios/$(basename "$f")" ../m-browser/pc-bios/ || echo "check $f"
   done
   ```

   Boot the guest to the desktop in m-browser's GUI, and once headless
   (`m-browser --qemu-headless`, as `tools/perf/` does).
3. The smoke test ([13-testing.md](13-testing.md#the-manual-smoke-test)):
   keyboard and mouse; Ctrl+Alt+1/2/3 switch between the display, the
   monitor (`info status`; with threads, `info cpus` lists every vCPU)
   and the serial console. In the guest: `ip addr` (10.0.2.15),
   `ping -c 3 10.0.2.2`, `wget -O /dev/null http://example.com`,
   `apk update`. Chromium starts and reacts to clicks. `poweroff`, and
   closing the window, end QEMU with exit status 0.
4. Performance: `boot-timing.sh` with `CLICKS=1` on the new modules, as
   for the baseline. Boot time and click latency should match within the
   run-to-run variation ([15-performance.md](15-performance.md)).
5. m-browser's network tests, `cargo test --release --offline qemu_net`
   in m-browser: required if the network interface changed (0006, or the
   waking code of 0004, 0007, 0008), and cheap enough to run anyway.

## Finishing

1. In qemu-wasi, on the `upgrade-qemu` branch, commit `patches/` (with
   `BASE`) and every changed script, library and `display/` file, then
   merge the branch.
2. Update the documentation: `grep -rn 'v9\.1\.0\|9\.1' README.md docs/`
   finds the old version (README, [05](05-repository-tour.md),
   [10](10-changing-qemu.md)); fix patch file names too if a subject
   changed. Rewrite "Likely conflicts" in [06](06-patch-series.md) and the
   checklist in [07](07-tcg-backend.md) with what you learnt; record the
   upgrade (date, releases, problems, measurements) in
   [17-history.md](17-history.md).
3. In m-browser, commit the new firmware together with any host-side
   change; then install the flavor m-browser should run (`make install`
   or `make THREADS=1 install`).
4. Tell the team that the old `qemu-src/` cannot simply be reset after
   pulling (it lacks the new tag): export and commit unfinished work,
   carry it over ([10](10-changing-qemu.md#carrying-work-over-to-a-newer-series)),
   then `rm -rf qemu-src qemu-build qemu-build-threads qemu-native` and `make`.

## Troubleshooting

| Symptom | Cause and what to do |
|---|---|
| The rebase or `make reset-qemu` does not know `vX.Y.Z` | The tag was not fetched into this shallow clone: [step 1](#1-fetch-the-new-tag) |
| `warning: inexact rename detection was skipped due to too many files` | Do as the next warning says (raise `merge.renameLimit`), then `git rebase --abort` and start again |
| `git am` fails on a fresh clone (the alternative path, or another machine) | On another machine: `patches/BASE` and the patch files disagree (compare `BASE` with `base-commit:` in 0001) or a patch was edited by hand; re-export from the rebased branch. In the alternative path: use the rebase |
| Configure: `Unsupported CPU wasm32, try --enable-tcg-interpreter` | `'wasm32'` is missing from `supported_cpus` (0001), or upstream restructured its list of host CPUs: add it where they are listed now |
| Configure: `Dependency glib-2.0 found: NO found 2.78.0 but need: ...` | QEMU needs a newer GLib: extend glib-wasi as needed, then raise the version in its `.pc` files |
| `error: no 'build libcommon.a.p/ui_console.c.o:' statement in build.ninja` | `ui/console.c` is compiled into another library now: update `FLAGS_FROM` |
| wasm-ld: `unknown argument: --start-group` | ninja re-ran meson and dropped `fix_build_ninja.py`'s edits: `make` again; if meson's re-run fails, delete the build directory's `build.ninja`. Another flag: add it to `DROP_FLAGS` |
| wasm-ld: `undefined symbol` | Usually the same cause (`LINK_ARGS` lacks `-Wl,--allow-undefined`). Otherwise the symbol's QEMU file is no longer compiled for wasm32: `git grep -n` it and read the `meson.build` that should list the file |
| Compile error: implicit declaration, unknown type, missing header | A GLib or POSIX function or header the new release uses: glib-wasi or wasi-stubs |
| Instruction test `FAIL` | Rerun with `QEMU_JIT_HOT=0` and `QEMU_JIT_HOT=4000000000`: failing in every mode points at the code generator or a helper, in one mode at that tier, in the default mode only at the switch between tiers ([14-debugging.md](14-debugging.md#the-instruction-test-fails), [07](07-tcg-backend.md#things-a-porter-must-preserve)) |
| The guest hangs at boot, or behaves differently | Boot a copy of the image with `qemu-native/qemu-system-x86_64`: if it fails too, the cause is upstream or the guest; the default machine `pc` is now the new release's version, and `-M pc-i440fx-9.1` tells whether a changed machine default is responsible. Otherwise suspect 0003, 0004 and 0007 (slices, main loop): `QEMU_STATUS_EVERY=10`, `WASI_DEBUG=1` ([14-debugging.md](14-debugging.md#the-guest-hangs-during-boot), [known-issues.md](known-issues.md)) |
| m-browser: ``unknown import: `env::...` has not been defined`` | QEMU calls a function nothing defines. Compare the imports with the baseline: a GLib or POSIX function needs implementing; a renamed function among the patches' local `extern` declarations needs the new name ([06](06-patch-series.md#how-the-patches-depend-on-each-other)) |
| QEMU exits at start with an error about an option | m-browser passes an option the new release removed: update its `src/app.rs` |
| The native build fails | The new release's host requirements (Python, compiler, GLib development package), or an option of the Makefile's `native` target that no longer exists |

## Upgrading other components

One at a time, never together with QEMU, each followed by the whole
[verification](#verification).

### wasi-sdk

The version is assumed in README.md ("tested with wasi-sdk 29") and in
[05](05-repository-tour.md#hardcoded-paths-and-assumptions). Install the
new one next to the old and select it with `WASI_SDK=<path>` (default
`/opt/wasi-sdk`), so that you can go back. What may change:

- The exception flags in `scripts/configure.sh`, `-mllvm -wasm-enable-sjlj`,
  `-mllvm -wasm-enable-eh` and `-mllvm --wasm-use-legacy-eh=false`, are
  internal LLVM options that can be renamed or become defaults. Without
  them QEMU traps at the first `cpu_loop_exit()`.
- The path of `libsetjmp.a`, `share/wasi-sysroot/lib/<target>/`
  (`link_flags()` in `scripts/fix_build_ninja.py`).
- The targets `wasm32-wasip1` and `wasm32-wasip1-threads`: compiler names
  in `configure.sh`, `build-zlib.sh` and both library Makefiles, and how
  `fix_build_ninja.py` recognises the threads build. glib-wasi's
  single-threaded build uses `wasm32-wasip2-clang`.
- wasi-libc may gain functions wasi-stubs defines (duplicate symbols:
  delete the stub) or change its emulation libraries
  (`-lwasi-emulated-*`).
- 0008's inline assembly in `tcg/wasm32.c` (`wasi_vcpu_enter`) depends on
  LLVM's wasm assembler syntax and the `__stack_pointer` and `__tls_base`
  globals.

Rebuild from scratch: `make clean`, `make THREADS=1 clean`,
`rm -rf zlib-wasi zlib-wasi-threads`, then `make` and `make THREADS=1`.
Not `make distclean`, which also deletes `qemu-src/`.

### zlib

`ZLIB_TAG` (default `v1.3.1`) in `scripts/build-zlib.sh`. Change the
default there, run `rm -rf zlib-src zlib-wasi zlib-wasi-threads`, then
`make` and `make THREADS=1`: the script only clones when `zlib-src/` is
missing, and the Makefile only builds zlib while `libz.a` is missing.

### wasmtime (m-browser)

m-browser's `Cargo.toml` asks for `wasmtime`, `wasmtime-wasi` and
`wasmtime-wasi-io` 41.0.4; the wasi-gfx runtime crates, pinned there to a
git revision, are built on wasmtime too and may need a matching revision.
Re-test what QEMU relies on: wasm exceptions (`wasm_exceptions(true)`;
QEMU does not validate without them), shared memory and wasi-threads (the
threaded flavor), Store migration and its instance limit, the epoch-based
profiler (`QEMU_PROFILE=1`), and the precompiled-module cache (the first
start must compile QEMU's module again, not load an old `.cwasm`). Then
run `cargo test --release --offline` and the verification above. Details:
the m-browser repository's docs/06-qemu-runner.md and docs/08-threads.md.
