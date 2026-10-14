#!/bin/bash
# Fetch QEMU into qemu-src/ and apply the patch series from patches/.
#
#   scripts/fetch-qemu.sh          clone and patch, unless already done
#   scripts/fetch-qemu.sh --reset  discard all changes in qemu-src/ and
#                                  apply patches/ afresh (after updating
#                                  the series, e.g. with git pull)
#
# Each patch becomes a commit in qemu-src/ (on branch "qemu-wasi"), so
# changes made there can be turned back into the series with
# scripts/export-patches.sh.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
src=$top/qemu-src
QEMU_GIT=${QEMU_GIT:-https://gitlab.com/qemu-project/qemu.git}
QEMU_TAG=$(cat "$top/patches/BASE")
stamp=$src/.git/qemu-wasi-patched

# git am records a committer; use a placeholder if git has no identity.
git_am() {
    local id=()
    git -C "$src" config user.email >/dev/null || id+=(-c user.email=qemu-wasi@localhost)
    git -C "$src" config user.name >/dev/null || id+=(-c user.name=qemu-wasi)
    git -C "$src" "${id[@]}" am --quiet --whitespace=nowarn "$top"/patches/*.patch
}

if [ "${1:-}" = --reset ]; then
    [ -d "$src/.git" ] || { echo "error: no $src to reset" >&2; exit 1; }
    git -C "$src" am --abort 2>/dev/null || true
    git -C "$src" checkout --quiet --force -B qemu-wasi "$QEMU_TAG"
    git -C "$src" clean --quiet -fd
    rm -f "$stamp"
elif [ -f "$stamp" ]; then
    if [ -n "$(find "$top/patches" -name '*.patch' -newer "$stamp")" ]; then
        echo "warning: patches/ changed since qemu-src was patched; to build" >&2
        echo "the new series, run: make reset-qemu (this discards changes" >&2
        echo "made in qemu-src, so export them first if you want them)" >&2
    else
        echo "qemu-src is already patched"
    fi
    exit 0
elif [ ! -d "$src" ]; then
    git clone --depth 1 --branch "$QEMU_TAG" "$QEMU_GIT" "$src"
    git -C "$src" checkout --quiet -b qemu-wasi
fi

echo "Applying $(ls "$top"/patches/*.patch | wc -l) patches to QEMU $QEMU_TAG"
git_am
touch "$stamp"
