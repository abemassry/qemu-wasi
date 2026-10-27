# Changing QEMU

**What this covers.** The day-to-day workflow for changing QEMU in this
project: where to edit, how to build, how to choose the patch a change
belongs to, how to fold a change into that patch or add a new one, how to
export the series and commit it, and what to do when git stops with a
conflict. Read it before your first QEMU change, with
[06-patch-series.md](06-patch-series.md) (what each patch contains) at
hand. Upgrading to a newer QEMU release is a separate procedure:
[11-upgrading-qemu.md](11-upgrading-qemu.md).

## The loop

```text
qemu-src/  (branch qemu-wasi)                 qemu-wasi root
 1 edit files
 2                                            make, make THREADS=1, tests
 3 git add; git commit --fixup=<patch>
 4 git rebase -i --autosquash v9.1.0
 5 git diff <old tip> HEAD    (must be empty)
 6                                            make export-patches
 7                                            git diff patches/; git commit
```

## Where to edit

Edit QEMU in `qemu-src/`, never in `patches/*.patch`. The patch files are
generated: the next `make export-patches` overwrites them, silently
undoing a hand edit, and wrong `@@` line counts make `git am` fail on
every other machine. qemu-wasi's `.gitignore` excludes `qemu-src/`, so
your QEMU edits exist only there until exported. Everything else
(`display/`, `glib-wasi/`, `wasi-stubs/`, `scripts/`, `tests/`) is
ordinary qemu-wasi code, edited and committed there.

## Building after an edit

`make` builds the working tree as it is; there is no need to commit
first. Build both flavors: they compile different `#ifdef` paths of the
same source (only the second defines `WASI_THREADS`).

```sh
# from the qemu-wasi root
make                  # qemu-build/qemu-system-x86_64
make THREADS=1        # qemu-build-threads/qemu-system-x86_64
```

| What happened | What the next `make` does |
|---|---|
| A `.c` or `.h` file changed | ninja recompiles what depends on it |
| A `meson.build` changed (edited, or rewritten by a rebase: 0001, 0002, 0005 and 0008 touch one) | ninja re-runs meson itself, which drops the edits of `scripts/fix_build_ninja.py`, and the link fails (wasm-ld rejects `--start-group`). Run `make` again: the Makefile re-applies them. If meson's re-run fails, delete `build.ninja` from the build directory; `make` then runs `scripts/configure.sh` |
| `make export-patches` or `make reset-qemu` ran | Re-runs `scripts/configure.sh`, once per flavor: the stamp `qemu-src/.git/qemu-wasi-patched` is newer than `build.ninja` |
| `scripts/configure.sh` changed | Re-runs it |
| A rebase in `qemu-src/` | Recompiles the files of every rewritten patch: their timestamps change even when their content does not |

The instruction test compares with a native QEMU built from the same
tree. After changing code that native builds compile too (anything
outside a WASI `#ifdef`), run `make native` again before `make test`.

## Choosing the patch

Every change goes into exactly one patch, chosen by topic:

| Topic | Patch |
|---|---|
| configure, meson, portability (`osdep.h`, signals, `mmap`, random), the coroutine backend, `WASI_LOG()` | 0001 |
| TCG backend, JIT interface, interpreter, TB execution (`tcg/wasm32*`, `tcg/wasi_jit.h`, `tcg/region.c`, `tcg/tcg.c`) | 0002 |
| Running the vCPU from the main loop, time slices, `cpu-exec.c` debugging aids, `cputlb.c` | 0003 |
| Main loop sleeping and polling, BHs from coroutines, graph lock, RCU, thread pool | 0004 |
| Text consoles, `pixman-minimal.h` | 0005 |
| The `user` network backend | 0006 |
| Reactor exports, `qemu_main_loop_step()`, start-up in `system/vl.c`, monitor | 0007 |
| Anything only the threaded build needs (`WASI_THREADS`) | 0008 |

To see which patches touch a file, and which commit last changed each
line of a function:

```sh
# from qemu-src/
git log --oneline v9.1.0..HEAD -- accel/tcg/cpu-exec.c
git blame -L :cpu_handle_interrupt accel/tcg/cpu-exec.c
```

Upstream lines show `^fd1952d` (the base; `^` marks the edge of the
shallow history), the others one of the patch commits. Then:

1. A change to lines a patch added goes into that patch. Fixing them in
   an earlier patch makes the rebase stop with a conflict.
2. Anything that only matters with `WASI_THREADS` goes into 0008.
3. Otherwise the patch of its topic, even in a file several patches change
   (`system/runstate.c`: 0007 and 0008).
4. A change spanning two topics becomes two commits: `git add -p <file>`
   offers each hunk (block of changed lines) for staging: `y`, `n`, or
   `s` to split it.

## Changing an existing patch

Each patch is one commit in `qemu-src/`. To change patch N without adding
a commit to the series, make a *fixup commit* ("fold me into commit X")
and let an *interactive rebase* fold it in: `git rebase -i` rewrites the
commits after a base one by one, following a to-do list, and
`--autosquash` writes that list for you.

1. Edit, build and test ([above](#building-after-an-edit)).
2. Find the patch's commit and commit your change as a fixup of it:

   ```sh
   # from qemu-src/
   git log --oneline v9.1.0..HEAD    # newest first; here 0003 is ab9680c
   git add accel/tcg/cpu-exec.c      # only what belongs to this patch
   git status --short                # nothing else staged?
   git commit --fixup=ab9680c        # "fixup! accel/tcg: run the vCPU ..."
   ```

   The subject `fixup! <subject of the patch>` is how the rebase finds the
   target. Hashes change with every rebase: look them up each time. For a
   split change, repeat `git add` and `git commit --fixup` per patch.
3. Note the tip (the tree you built and tested), then rebase:

   ```sh
   # still in qemu-src/
   old=$(git rev-parse HEAD)
   GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash v9.1.0
   ```

   `GIT_SEQUENCE_EDITOR=true` accepts the generated to-do list without
   opening an editor. Without it you would see the list; a `fixup` line
   melds that commit into the one above and keeps the latter's message:

   ```text
   pick ab9680c accel/tcg: run the vCPU inline from the main loop on WASI
   fixup 1a2b3c4 fixup! accel/tcg: run the vCPU inline from the main ...
   pick 45edcc8 util: main loop, coroutines, RCU and thread pool ...
   ```

4. Check the result:

   ```sh
   # still in qemu-src/
   git diff --stat "$old" HEAD          # must print nothing
   git log --oneline v9.1.0..HEAD       # the 8 patches, no fixup! left
   git range-diff v9.1.0 "$old" HEAD    # optional: what changed, per patch
   ```

   Folding only moves changes between commits, so the tree must equal the
   one you tested; a difference means a conflict was resolved wrongly
   (`git reset --hard "$old"` goes back). In the `range-diff` output, `=`
   marks an unchanged patch, `!` a changed one, followed by the change,
   and `<` the fixup commit that disappeared.
5. Export and commit ([below](#exporting-and-committing)).

Variants of step 2 (`amend:` and `reword:` need git 2.32 or newer):

| To change | Commit with |
|---|---|
| Code and the patch's commit message | `git commit --fixup=amend:ab9680c`: the editor shows `amend! <subject>`, a blank line and the current message; edit the message below the first line |
| Only the message | `git commit --fixup=reword:ab9680c` (ignores staged changes) |
| The last patch (0008) | `git commit --amend`, which needs no rebase |

Leave the first line `amend! <old subject>` alone even when you change
the subject: the rebase finds the patch by it, and everything after the
blank line becomes the new message (the to-do list shows `fixup -C`). In
a script, write the message to a file and make `cp` the editor (git
appends the message file's path):

```sh
# from qemu-src/; /tmp/msg.txt holds the new subject, a blank line, the body
{ echo "amend! $(git log -1 --format=%s ab9680c)"; echo; cat /tmp/msg.txt; } \
    > /tmp/amend.txt
GIT_EDITOR="cp /tmp/amend.txt" git commit --fixup=amend:ab9680c
```

## Adding a new patch

Make a new patch for a topic of its own: one that would make an existing
patch's description wrong or much broader, that may be dropped or offered
upstream separately, or that is large; 0008 (the threaded build) was
added that way. Bug fixes and extensions of a topic are fixups. In
`qemu-src/`, `git add` the files and `git commit` as usual.

Write the message like the existing ones, since it travels with the patch
as its description: a subject `area: what it does` (`net:`,
`accel/tcg:`) of about 70 characters at most, a blank line, and a body
wrapped at 72 columns saying what problem it solves, how and why, file by
file. Give the patch a section in [06-patch-series.md](06-patch-series.md)
too. Add patches at the end: one inserted in the middle renumbers, and so
renames, every later file. After the export the new file is
`0009-...patch`, and the others change only in `Subject: [PATCH n/8]`,
now `[PATCH n/9]`.

## Exporting and committing

```sh
# from the qemu-wasi root
make export-patches
git status --short patches/
git diff patches/
```

The export refuses to run while tracked files in `qemu-src/` have
uncommitted or staged changes, and warns about untracked files, which it
does not export (commit new files first). It rewrites every
`patches/*.patch` and touches the stamp, so the next `make` reconfigures.
Then read the diff:

| You | `git diff patches/` should show |
|---|---|
| Changed patch N | Patch N's new diff, file summary and `index` lines; in later patches that touch the same files, only new `index` lines and shifted `@@` line numbers |
| Added a patch | That file; in every other file only the `[PATCH n/N]` line |
| Reworded a subject | One file deleted, one added |
| (anything else, such as content changes in patches you did not touch) | `qemu-src/` did not hold the committed series, usually after a pull without `make reset-qemu`: do not commit ([Troubleshooting](#troubleshooting)) |

Commit together with any qemu-wasi files the change needs, so that every
qemu-wasi commit builds:

```sh
# still in the qemu-wasi root
git add patches/
git commit -m "patches: <what changed and why>"
```

A new or changed host function also needs a change in m-browser
([reference/host-interface.md](reference/host-interface.md)).

**Pitfall:** never export while a rebase is unfinished (`git status` in
`qemu-src/` says "rebase in progress"): the export writes only the
commits applied so far and deletes the other patch files.
`git checkout -- patches/` in qemu-wasi restores them.

## Getting the series onto another machine

- **New checkout:** clone qemu-wasi and run `make`: `fetch-qemu.sh`
  clones QEMU at `patches/BASE` and applies the series with `git am`, one
  commit per patch. Then exclude `.wraplock` ([below](#the-wraplock-file)).
- **After a `git pull` that changed `patches/`:** `make` only warns
  ("patches/ changed since qemu-src was patched") and builds the old tree.
  Run `make reset-qemu` and `make`. If `qemu-src/` held unexported work,
  carry it over ([below](#carrying-work-over-to-a-newer-series)).
- **From scratch:** `rm -rf qemu-src`, then `make` (downloads QEMU and its
  meson subprojects again).

`make reset-qemu` resets branch `qemu-wasi` to the tag, deletes untracked
files and re-applies `patches/`; expect a reconfigure and a long rebuild.
Unexported work cannot travel any other way: export and commit it (on a
qemu-wasi branch if unfinished), and pull before starting a change.

**Pitfall:** the warning says to export your changes first, which is only
safe before the pull: exporting from a `qemu-src/` older than `patches/`
writes your older series over the files you pulled.

## When git stops with a conflict

**`git am` fails** in `make` or `make reset-qemu` (`Patch failed at
000N ...`). An exported series always applies to its base, so a patch
file was edited by hand or merged badly, or `patches/BASE` changed
without the series. Find the commit with `git log -p -- patches/` in
qemu-wasi, restore the files from the last good commit
(`git checkout <commit> -- patches/`) and run `make reset-qemu`, which
also clears the half-finished `git am`. Then redo the intended change
properly in `qemu-src/`.

**The rebase stops** with `CONFLICT (content): Merge conflict in <file>`
and `could not apply <hash>... <subject>`: a commit no longer fits,
usually because the fixup touches lines a later patch also changes. The
file has conflict markers, as after a conflicting merge. Either:

- `git rebase --abort`, which returns to exactly where you were before
  the rebase, fixup commits included; then reconsider which patch the
  change belongs to; or
- resolve: edit the file to its intended content without the markers,
  `git add <file>`, `git rebase --continue` (if an editor opens, save and
  close it), and repeat until it finishes. The check of step 4 is then
  essential.

### Carrying work over to a newer series

When `patches/` changed under unexported work, or a pull conflicts inside
a `.patch` file (which cannot be fixed by editing it), redo your change
on top of the newer series:

1. In `qemu-src/`, commit any new files, then save everything you changed
   since the series you started from. `git reflog` lists the positions of
   HEAD, newest first; `<before>` is the line below your first own
   `commit:` line. `git diff <before> > /tmp/mine.diff` (with one commit
   named, uncommitted edits are included).
2. In qemu-wasi, get the newer series. If a `.patch` file conflicts, take
   the other side's patch files and finish the merge:
   `git checkout --theirs -- patches/`, `git add patches/`, `git commit`
   (after `git pull --rebase` the sides are swapped: use `--ours`).
3. `make reset-qemu`; then in `qemu-src/`, `git apply --3way
   /tmp/mine.diff` (it also stages the change; `git reset` unstages it if
   it belongs to several patches), resolve any conflict markers, and go on
   as in [Changing an existing patch](#changing-an-existing-patch): build,
   test, commit fixups, rebase, export.

## The .wraplock file

Configuring runs meson, which creates `qemu-src/subprojects/.wraplock`, an
empty lock file that QEMU's `.gitignore` files do not cover. git in
`qemu-src/` sees it as untracked: `make export-patches` warns about it
every time, and `git add -A` or `git add .` would put it into a patch.
Exclude it once per clone of QEMU (`fetch-qemu.sh` does not do it yet);
`.git/info/exclude` is a local, unversioned `.gitignore`, so this
survives `make reset-qemu` but not deleting `qemu-src/`:

```sh
# from the qemu-wasi root
echo /subprojects/.wraplock >> qemu-src/.git/info/exclude
```

## Regenerating wasi-interp-calls.c.inc

`tcg/wasm32/wasi-interp-calls.c.inc` (patch 0002) is the output of
`scripts/gen_interp_calls.py` and matches it today. Only after changing
the script (for example `MAX_PARAMS`, if helpers get more than 8 wasm
parameters; see
[07-tcg-backend.md](07-tcg-backend.md#the-helper-call-dispatcher)), run
`scripts/gen_interp_calls.py > qemu-src/tcg/wasm32/wasi-interp-calls.c.inc`
from the qemu-wasi root, commit the result as a fixup of 0002, export,
and commit the script with `patches/`.

## Installing into m-browser

`make install` (single-threaded) and `make THREADS=1 install` (threaded),
from the qemu-wasi root, build and then copy to the same file,
`$(M_BROWSER)/qemu-system-x86_64.wasm` (`M_BROWSER` defaults to
`../m-browser`), so the last one installed wins. m-browser recognises the
threaded flavor (it imports its memory) and logs `Multi-threaded QEMU:
one thread per vCPU` for it. m-browser needs no rebuild unless the host
interface changed.

## Checklist before committing

1. `git -C qemu-src log --oneline v9.1.0..HEAD` lists only the patch
   commits (no `fixup!`, `amend!` or work-in-progress commits), and
   `git -C qemu-src status --short` shows nothing forgotten.
2. Both flavors build: `make` and `make THREADS=1`.
3. The instruction test passes for both: `make test` and
   `make THREADS=1 test` (after `make native` if native code changed).
   For TCG backend changes, also with `QEMU_JIT_HOT=0` and
   `QEMU_JIT_HOT=4000000000` ([13-testing.md](13-testing.md)).
4. A boot test of each flavor the change affects: install it, boot the
   guest in m-browser to the desktop, type, check the monitor (Ctrl+Alt+2,
   `info status`) and the serial console (Ctrl+Alt+3), and `poweroff`:
   QEMU must exit with status 0.
5. If networking changed (0006, or the sleeping and waking code of 0004,
   0007 or 0008): `cargo test --release --offline qemu_net` in m-browser
   (tests of the host side your change talks to), and in the booted guest
   `ip addr` (10.0.2.15), `ping -c 3 10.0.2.2`,
   `wget -O /dev/null http://example.com` and `apk update`.
6. A function added or renamed across files or for the host: compare the
   module's imports
   ([reference/host-interface.md](reference/host-interface.md)).
7. After `make export-patches`, `git diff patches/` shows only what you
   expect.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `make` warns "patches/ changed since qemu-src was patched" and builds old code | `qemu-src/` still holds the previous series: `make reset-qemu` and `make`, after [carrying](#carrying-work-over-to-a-newer-series) any unexported work over |
| `make export-patches`: "qemu-src has uncommitted changes" | Commit the listed files (as a fixup or a new commit), or undo them with `git -C qemu-src checkout -- <file>` |
| `make export-patches` warns about untracked files | `subprojects/.wraplock`: exclude it ([above](#the-wraplock-file)). A file of yours: `git add` and commit it, then export again |
| `git rebase` refuses: "You have unstaged changes" | Commit (or `git stash`) them first |
| The rebase stopped with a conflict | Resolve and `git rebase --continue`, or `git rebase --abort` ([above](#when-git-stops-with-a-conflict)) |
| `fixup!` or `amend!` commits remain after the rebase | `--autosquash` was missing, or the text after `fixup!` does not match the patch's subject. Rebase again with `--autosquash` |
| `git diff "$old" HEAD` is not empty after the rebase | A conflict was resolved differently from the tested tree: `git reset --hard "$old"` and redo |
| Link fails after a rebase or a `meson.build` edit | ninja regenerated `build.ninja`: run `make` again; if that fails, delete `qemu-build/build.ninja` (or `qemu-build-threads/build.ninja`) and run `make` |
| `git am` fails in `make` or `make reset-qemu` | A broken patch file ([above](#when-git-stops-with-a-conflict)) |
| `git diff patches/` changes patches you did not touch | `qemu-src/` held an older series than `patches/`. Do not commit: `git checkout -- patches/`, then [carry your work over](#carrying-work-over-to-a-newer-series) |
| Work lost after `make reset-qemu` | Commits survive for a while: find the old tip with `git -C qemu-src reflog`, keep it with `git -C qemu-src branch rescue <hash>`. Uncommitted and untracked files are gone |
| m-browser: "unknown import" | A misspelt or missing function became a host import ([06-patch-series.md](06-patch-series.md#how-the-patches-depend-on-each-other)) |
| m-browser runs the other flavor | Both install to the same file: install the one you want again |
