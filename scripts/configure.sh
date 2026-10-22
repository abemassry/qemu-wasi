#!/bin/bash
# Configure qemu-src/ for wasm32-wasi in qemu-build/.
#
# Needs wasi-sdk (WASI_SDK, default /opt/wasi-sdk), and glib-wasi/,
# wasi-stubs/ and zlib-wasi/ built first (the Makefile does that): meson
# links test programs against them.  QEMU's configure downloads the meson
# subprojects it needs (dtc, keycodemapdb, ...) with git.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
src=$top/qemu-src
WASI_SDK=${WASI_SDK:-/opt/wasi-sdk}

# THREADS=1: the multi-threaded build (wasm32-wasip1-threads, one thread per
# vCPU), in qemu-build-threads/, with its own variants of the libraries.
if [ "${THREADS:-0}" = 1 ]; then
    build=$top/qemu-build-threads
    variant=-threads
    target=wasm32-wasip1-threads
else
    build=$top/qemu-build
    variant=
    target=wasm32-wasip1
fi
zlib=$top/zlib-wasi$variant

for lib in "$top/glib-wasi/libglib-wasi$variant.a" \
           "$top/wasi-stubs/libwasi-stubs$variant.a" "$zlib/lib/libz.a"; do
    [ -f "$lib" ] || { echo "error: $lib is missing; run make" >&2; exit 1; }
done

# pkg-config files for QEMU's two required libraries: glib-wasi/, a
# minimal GLib for WASI, and zlib (built by scripts/build-zlib.sh).
mkdir -p "$build/pkgconfig"
for pc in glib-2.0 gmodule-2.0 gthread-2.0; do
    sed "s|@GLIB_WASI_PREFIX@|$top/glib-wasi|g" \
        "$top/glib-wasi/pkgconfig/$pc.pc" > "$build/pkgconfig/$pc.pc"
done
export PKG_CONFIG_LIBDIR=$build/pkgconfig:$zlib/lib/pkgconfig
export PKG_CONFIG_PATH=$PKG_CONFIG_LIBDIR

export CC=$WASI_SDK/bin/$target-clang
export CXX=$WASI_SDK/bin/$target-clang++
export AR=$WASI_SDK/bin/llvm-ar
export NM=$WASI_SDK/bin/llvm-nm
export RANLIB=$WASI_SDK/bin/llvm-ranlib
export STRIP=$WASI_SDK/bin/llvm-strip
export OBJCOPY=$WASI_SDK/bin/llvm-objcopy

# WASI_PLATFORM selects the WASI code paths; _WASI_EMULATED_* enable
# wasi-libc's signal, clock and mmap emulation.  setjmp/longjmp become
# wasm exception handling (QEMU's cpu_loop_exit() relies on them).
defines="-D__wasi__ -DWASI_PLATFORM -D_WASI_EMULATED_SIGNAL"
defines+=" -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN"
if [ "${THREADS:-0}" = 1 ]; then
    # WASI_THREADS selects real threads and multi-threaded TCG.
    defines+=" -DWASI_THREADS -pthread"
fi
sjlj="-mllvm -wasm-enable-sjlj -mllvm -wasm-enable-eh -mllvm --wasm-use-legacy-eh=false"
# zlib.h is on the global include path, as a system zlib would be: some
# QEMU headers (migration/qemu-file.h) include it without the dependency.
includes="-I$top/glib-wasi/glib -I$top/glib-wasi -I$top/wasi-stubs -I$zlib/include"
libs="-L$top/glib-wasi -L$top/wasi-stubs -lglib-wasi$variant -lwasi-stubs$variant"
libs+=" -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-mman"

mkdir -p "$build"
cd "$build"
"$src/configure" \
    --cross-prefix= \
    --cpu=wasm32 \
    --target-list=x86_64-softmmu \
    --static \
    --without-default-features \
    --enable-tcg \
    --enable-system \
    --disable-pie \
    --disable-docs \
    --disable-install-blobs \
    --with-coroutine=wasi \
    --prefix="$build/install" \
    --extra-cflags="$defines $sjlj $includes" \
    --extra-ldflags="$libs"

python3 "$top/scripts/fix_build_ninja.py" "$build/build.ninja"
