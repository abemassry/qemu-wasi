#!/usr/bin/env python3
"""
Adapt the meson-generated qemu-build/build.ninja for wasm32-wasi.

Meson assumes a POSIX linker, so the link of qemu-system-x86_64 that it
generates has flags wasm-ld rejects, and lacks the ones a WASI reactor
needs.  And the wasi-gfx display backend in display/ lives outside the
QEMU tree, so meson does not know about it.  This script fixes up
build.ninja after every (re)configure; it is idempotent, and the Makefile
runs it before each build.

It:
  * adds build statements that compile display/*.c with the flags of
    QEMU's own ui/console.c (plus -DQEMU_BUILD), and links the objects
    into qemu-system-x86_64
  * removes --start-group / --end-group / -pthread from the link
  * appends libsetjmp.a and the wasm-ld flags QEMU needs

Usage: fix_build_ninja.py [path/to/build.ninja]
"""

import os
import sys
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
DEFAULT_BUILD_NINJA = TOP / "qemu-build" / "build.ninja"
WASI_SDK = Path(os.environ.get("WASI_SDK", "/opt/wasi-sdk"))

TARGET = "qemu-system-x86_64"

# The wasi-gfx display backend: sources in display/, and the QEMU object
# whose compile flags they share.
DISPLAY_SOURCES = ["wasi_gfx_display.c", "wasi_gfx_dcl.c"]
FLAGS_FROM = "libcommon.a.p/ui_console.c.o"
DISPLAY_DEFINES = ["-DQEMU_BUILD"]

# Flags wasm-ld does not understand.
DROP_FLAGS = ["-Wl,--start-group", "-Wl,--end-group", "-pthread"]

# Flags the wasm build needs but meson does not emit.  libsetjmp.a is
# added per target (see link_flags()).
EXTRA_LINK_FLAGS = [
    # The host adds each compiled TB's entry function to QEMU's function
    # table, so the table must be exported and growable.
    "-Wl,--export-table",
    "-Wl,--growable-table",
    "-Wl,--error-limit=0",
    # Host functions (wasi_jit_*, host_gfx_*, host_net_*) are imports.
    "-Wl,--allow-undefined",
    "-Wl,-z,stack-size=8388608",
    # Link as a WASI reactor: the host drives QEMU through the exported
    # wasi_qemu_init/wasi_qemu_step functions (see system/main.c), so it
    # regains control between main loop iterations.
    "-mexec-model=reactor",
]


# The multi-threaded build (wasm32-wasip1-threads; THREADS=1 in the
# Makefile) also needs: its memory imported from the host, which creates it
# shared between the threads, and exported, for the host's WASI functions;
# and room for the threads' stacks and the guest's RAM.
THREADS_LINK_FLAGS = [
    "-Wl,--import-memory",
    "-Wl,--export-memory",
    "-Wl,--max-memory=4294967296",
]


def link_flags(text: str):
    """The extra link flags for the target build.ninja compiles for."""
    threads = "wasm32-wasip1-threads-clang" in text
    target = "wasm32-wasip1-threads" if threads else "wasm32-wasip1"
    # setjmp/longjmp support for -mllvm -wasm-enable-sjlj.
    flags = [str(WASI_SDK / f"share/wasi-sysroot/lib/{target}/libsetjmp.a")]
    flags += EXTRA_LINK_FLAGS
    if threads:
        flags += THREADS_LINK_FLAGS
    return flags


def ninja_path(path) -> str:
    """Escape a path for use in a ninja build statement."""
    return str(path).replace("$", "$$").replace(" ", "$ ").replace(":", "$:")


def statement(lines, start):
    """The indices of the variable lines that belong to the build
    statement at lines[start]."""
    end = start + 1
    while end < len(lines) and lines[end].startswith(" "):
        end += 1
    return range(start + 1, end)


def find_statement(lines, output):
    prefix = f"build {output}: "
    for i, line in enumerate(lines):
        if line.startswith(prefix):
            return i
    sys.exit(f"error: no 'build {output}:' statement in build.ninja; "
             "has QEMU been configured?")


def display_statements(lines):
    """Build statements compiling display/*.c like ui/console.c."""
    src = find_statement(lines, FLAGS_FROM)
    head = lines[src]
    order_only = head.partition(" || ")[2]
    args = None
    for i in statement(lines, src):
        key, _, value = lines[i].strip().partition(" = ")
        if key == "ARGS":
            args = value
    if args is None:
        sys.exit(f"error: no ARGS for {FLAGS_FROM} in build.ninja")

    out = ["# qemu-wasi: the wasi-gfx display backend (scripts/fix_build_ninja.py)"]
    for name in DISPLAY_SOURCES:
        obj = name[:-2] + ".o"
        line = f"build {obj}: c_COMPILER {ninja_path(TOP / 'display' / name)}"
        if order_only:
            line += f" || {order_only}"
        out += [
            line,
            f" DEPFILE = {obj}.d",
            f" DEPFILE_UNQUOTED = {obj}.d",
            f" ARGS = {args} {' '.join(DISPLAY_DEFINES)}",
            "",
        ]
    return out


def patch(text: str):
    notes = []
    lines = text.split("\n")
    objects = [name[:-2] + ".o" for name in DISPLAY_SOURCES]

    # 1. Compile the display backend.
    if not any(line.startswith(f"build {objects[0]}: ") for line in lines):
        link = find_statement(lines, TARGET)
        lines[link:link] = display_statements(lines)
        notes.append("added build statements for display/: "
                     + ", ".join(DISPLAY_SOURCES))

    # 2. Link its objects.
    link = find_statement(lines, TARGET)
    head, sep, tail = lines[link].partition(": ")
    rule, _, inputs = tail.partition(" ")
    missing = [o for o in objects if o not in inputs.split()]
    if missing:
        lines[link] = f"{head}{sep}{rule} {' '.join(missing)} {inputs}"
        notes.append(f"linked display objects: {', '.join(missing)}")

    # 3. Fix the link flags.
    for i in statement(lines, link):
        if not lines[i].lstrip().startswith("LINK_ARGS = "):
            continue
        indent = lines[i][: len(lines[i]) - len(lines[i].lstrip())]
        args = lines[i].lstrip()[len("LINK_ARGS = "):].split()

        removed = sorted({a for a in args if a in DROP_FLAGS})
        if removed:
            args = [a for a in args if a not in DROP_FLAGS]
            notes.append(f"removed unsupported link flags: {', '.join(removed)}")

        added = [f for f in link_flags(text) if f not in args]
        if added:
            args += added
            notes.append(f"appended wasm link flags: {', '.join(added)}")

        lines[i] = f"{indent}LINK_ARGS = {' '.join(args)}"
        break
    else:
        sys.exit(f"error: no LINK_ARGS for {TARGET} in build.ninja")

    return "\n".join(lines), notes


def main() -> None:
    build_ninja = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BUILD_NINJA
    if not build_ninja.exists():
        sys.exit(f"error: {build_ninja} not found; configure QEMU first")

    patched, notes = patch(build_ninja.read_text())
    if not notes:
        print(f"{build_ninja}: already adapted for wasm32-wasi")
        return
    build_ninja.write_text(patched)
    print(f"{build_ninja}: adapted for wasm32-wasi:")
    for note in notes:
        print(f"  - {note}")


if __name__ == "__main__":
    main()
