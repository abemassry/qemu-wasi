#!/bin/sh
# Differential x86-64 instruction test: native QEMU vs the wasm build.
#
# insn-test.c runs several hundred instruction forms on many inputs and
# prints a hash of the results per form, under both QEMUs; any difference
# is a bug in the wasm TCG backend.
#
#   tests/x86-insn-diff/run.sh                            (or: make test)
#   QEMU_JIT_HOT=0 tests/x86-insn-diff/run.sh             (wasm: JIT only)
#   QEMU_JIT_HOT=4000000000 tests/x86-insn-diff/run.sh    (wasm: interpreter only)
#
# Needs gcc for x86_64, a native QEMU (NATIVE_QEMU, default the one from
# make native) and m-browser (M_BROWSER, default ../m-browser) for its
# --qemu-headless runner.  Output goes to OUT (default
# ${TMPDIR:-/tmp}/qemu-wasi-insn).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
src=$top/qemu-src
native=${NATIVE_QEMU:-$top/qemu-native/qemu-system-x86_64}
wasm=${QEMU_WASM:-$top/qemu-build/qemu-system-x86_64}
mbrowser=${M_BROWSER:-$top/../m-browser}
out=${OUT:-${TMPDIR:-/tmp}/qemu-wasi-insn}
mkdir -p "$out"

for f in "$native" "$wasm" "$mbrowser/target/release/m-browser"; do
    [ -x "$f" ] || { echo "error: $f not found (see the comment at the top)" >&2; exit 1; }
done

# boot.S's __sys_outc writes %al although the character arrives in %dil;
# that only works for callers built at -O0, as upstream builds minilib.
gcc -O0 -ffreestanding -fno-pic -nostdlib -mno-red-zone \
    -I "$src/tests/tcg/minilib" -c "$src/tests/tcg/minilib/printf.c" \
    -o "$out/printf.o"
gcc -O1 -fno-tree-vectorize -ffreestanding -fno-pic -no-pie -nostdlib -static \
    -mno-red-zone -Wa,--noexecstack -I "$src/tests/tcg/minilib" \
    -Wl,-T"$src/tests/tcg/x86_64/system/kernel.ld" -Wl,-melf_x86_64 \
    -Wl,--build-id=none \
    "$src/tests/tcg/x86_64/system/boot.S" "$here/insn-test.c" \
    "$out/printf.o" -lgcc -o "$out/insn-test.elf"

common="-M pc -cpu max -m 64 -smp 1 -no-reboot -monitor none -serial none"

rm -f "$out/native.txt" "$out/wasm.txt"
timeout 120 "$native" $common -display none \
    -L "$src/pc-bios" -kernel "$out/insn-test.elf" \
    -chardev file,id=out,path="$out/native.txt" \
    -device isa-debugcon,iobase=0xe9,chardev=out

cp "$out/insn-test.elf" "$out/insn-test-wasm.elf"
( cd "$mbrowser" && timeout "${WASM_TIMEOUT:-900}" ./target/release/m-browser \
    --qemu-headless --preopen "$out=/out" --preopen "$src/pc-bios=/bios2" \
    "$wasm" $common -L /bios2 \
    -kernel /out/insn-test-wasm.elf \
    -chardev file,id=out,path=/out/wasm.txt \
    -device isa-debugcon,iobase=0xe9,chardev=out ) > "$out/wasm-run.log" 2>&1 || true

echo "native: $(wc -l < "$out/native.txt") lines, wasm: $(wc -l < "$out/wasm.txt") lines"
if diff "$out/native.txt" "$out/wasm.txt" > "$out/diff.txt"; then
    echo "PASS: all instruction hashes match"
else
    echo "FAIL: mismatching instruction forms:"
    grep '^<' "$out/diff.txt" | sed 's/^< /  /; s/ [0-9a-f]*$//'
    exit 1
fi
