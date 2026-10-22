#!/bin/bash
# Build zlib (QEMU requires it) for wasm32-wasi: fetch it into zlib-src/
# and install it, static, into zlib-wasi/.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
WASI_SDK=${WASI_SDK:-/opt/wasi-sdk}
ZLIB_GIT=${ZLIB_GIT:-https://github.com/madler/zlib.git}
ZLIB_TAG=${ZLIB_TAG:-v1.3.1}

if [ ! -d "$top/zlib-src" ]; then
    git clone --quiet --depth 1 --branch "$ZLIB_TAG" "$ZLIB_GIT" "$top/zlib-src"
fi
# THREADS=1: for QEMU's multi-threaded build, into zlib-wasi-threads/.
if [ "${THREADS:-0}" = 1 ]; then
    cc=$WASI_SDK/bin/wasm32-wasip1-threads-clang
    cflags="-O2 -g -pthread"
    prefix=$top/zlib-wasi-threads
else
    cc=$WASI_SDK/bin/wasm32-wasip1-clang
    cflags="-O2 -g"
    prefix=$top/zlib-wasi
fi

cd "$top/zlib-src"
# Both variants build in this tree: start from a clean one.
make distclean > /dev/null 2>&1 || true
CC=$cc \
AR=$WASI_SDK/bin/llvm-ar \
RANLIB=$WASI_SDK/bin/llvm-ranlib \
CFLAGS=$cflags \
    ./configure --static --prefix="$prefix"
make libz.a
make install
