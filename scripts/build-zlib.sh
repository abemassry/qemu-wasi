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
cd "$top/zlib-src"
CC=$WASI_SDK/bin/wasm32-wasip1-clang \
AR=$WASI_SDK/bin/llvm-ar \
RANLIB=$WASI_SDK/bin/llvm-ranlib \
CFLAGS="-O2 -g" \
    ./configure --static --prefix="$top/zlib-wasi"
make libz.a
make install
