#!/bin/bash
# Turn the commits on top of QEMU's base tag in qemu-src/ back into the
# patch series in patches/.
#
# Workflow: edit QEMU in qemu-src/, commit (git commit, or git commit
# --amend / git rebase -i to fold a change into an existing patch), then
# run this and commit the updated patches/ in this repository.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
src=$top/qemu-src
base=$(cat "$top/patches/BASE")

if ! git -C "$src" diff --quiet HEAD; then
    echo "error: qemu-src has uncommitted changes; commit them first" >&2
    git -C "$src" status --short --untracked-files=no >&2
    exit 1
fi
untracked=$(git -C "$src" ls-files --others --exclude-standard)
if [ -n "$untracked" ]; then
    echo "warning: untracked files in qemu-src are not exported:" >&2
    echo "$untracked" | sed 's/^/  /' >&2
fi

rm -f "$top"/patches/*.patch
# --zero-commit: no commit hashes, which change on every rebase.
git -C "$src" format-patch --quiet --no-signature --zero-commit \
    --base="$base" -o "$top/patches" "$base..HEAD"
# qemu-src matches the new series (see scripts/fetch-qemu.sh).
touch "$src/.git/qemu-wasi-patched"
ls "$top"/patches/*.patch
