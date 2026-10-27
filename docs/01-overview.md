# Overview

**What this covers.** The project from 10,000 feet: what it is, what it
does, how its two repositories fit together, how fast it is, and which
chapters to read for which job.  Everyone should read this first; it
assumes no knowledge of emulators or WebAssembly.  Words in **bold** are
explained in [02-concepts.md](02-concepts.md) and the
[glossary](reference/glossary.md).

## In one paragraph

This project runs a complete PC (an x86-64 computer with a disk, screen,
keyboard, mouse and network card) inside a **WebAssembly** sandbox, and
boots an ordinary Linux desktop on it: Alpine Linux with X, the Fluxbox
window manager and the Chromium web browser.  The PC is **emulated** by
**QEMU**, the standard open-source machine emulator, compiled to
WebAssembly with a set of changes that this repository maintains.  The
WebAssembly program runs inside **m-browser**, a desktop application in
Rust (a separate repository), which shows the screen in a window, passes
keyboard and mouse events in, and provides the network, the files and the
compilation of code at run time.

## The picture

```text
 +----------------------------------------------------------------------+
 | m-browser (desktop app, Rust)                         host computer   |
 |                                                                      |
 |   window: browser UI + child window showing the guest's screen       |
 |                                                                      |
 |   +--------------------------------------------------------------+   |
 |   | wasmtime (WebAssembly runtime)                               |   |
 |   |                                                              |   |
 |   |   +------------------------------------------------------+   |   |
 |   |   | qemu-system-x86_64.wasm   (built by THIS repository) |   |   |
 |   |   |                                                      |   |   |
 |   |   |   emulated PC: CPU(s), RAM, disk, VGA, e1000, USB    |   |   |
 |   |   |   +----------------------------------------------+   |   |   |
 |   |   |   | guest: Alpine Linux, X, Fluxbox, Chromium    |   |   |   |
 |   |   |   +----------------------------------------------+   |   |   |
 |   |   +------------------------------------------------------+   |   |
 |   |        |  host imports: JIT, display, input, network,         |   |
 |   |        |  threads, files, clocks                              |   |
 |   +--------v-----------------------------------------------------+   |
 |   m-browser's services: compiles QEMU's generated code, draws        |
 |   frames, queues input, emulates the network over host sockets       |
 +----------------------------------------------------------------------+
        |               |                  |
     m.qcow2        the screen       the internet
   (disk image)
```

## Why it is hard, and how it is done

QEMU normally relies on things a WebAssembly program cannot do:

| QEMU normally... | In WebAssembly... | So this project... |
|---|---|---|
| Translates guest instructions into host machine code and jumps to it (its **TCG** code generator) | A program cannot create and run new machine code | Translates each block of guest code into a tiny WebAssembly module, interprets it while it is cold, and asks m-browser to compile it when it gets hot ([07-tcg-backend.md](07-tcg-backend.md)) |
| Runs a loop forever in `main()` | The host must keep control | Runs as a **reactor**: m-browser calls one main-loop step at a time, and can stop QEMU or move it to fresh memory between steps |
| Uses threads, signals, sockets, `mmap` tricks, GLib | Not available, or only partly (WASI) | Replaces or works around each: a minimal GLib (`glib-wasi/`), POSIX stubs (`wasi-stubs/`), patches to QEMU, services from the host |
| Draws in a desktop window (GTK, SDL) | No window system | A display backend (`display/`) hands frames to m-browser |
| Uses libslirp for user networking | Does not build for WASI | Passes Ethernet frames to m-browser, which emulates the network |
| Runs one host thread per guest CPU | Threads need the wasi-threads extension | Two builds: single-threaded, and multi-threaded with one thread per virtual CPU ([08-threads.md](08-threads.md)) |

## What is in this repository

```text
qemu-wasi/
  patches/        the changes to QEMU 9.1.0, as 8 patches (0001-0008)
  display/        the display backend (frames out, keyboard/mouse in)
  glib-wasi/      a minimal replacement for the GLib library
  wasi-stubs/     stand-ins for system functions WASI lacks
  scripts/        the build: fetch QEMU, apply patches, configure, fix up
  tests/          the instruction test (wasm QEMU vs native QEMU)
  tools/perf/     boot timing, click latency and benchmark scripts
  docs/           this documentation
  Makefile        make, make THREADS=1, make install, make test, ...
```

The QEMU source itself is not stored here: `make` downloads QEMU 9.1.0
into `qemu-src/` and applies the patches.  The result is one file,
`qemu-build/qemu-system-x86_64` (about 40 MB of WebAssembly), which
`make install` copies into m-browser.  See
[05-repository-tour.md](05-repository-tour.md).

## The two repositories

| | qemu-wasi (this one) | m-browser |
|---|---|---|
| Language | C (QEMU), shell, Python | Rust |
| Produces | `qemu-system-x86_64.wasm` | the `m-browser` desktop application |
| Responsible for | emulating the PC; generating WebAssembly for guest code | running WebAssembly; compiling QEMU's generated modules; window, input, network, threads, files |
| Contract between them | the host interface: [reference/host-interface.md](reference/host-interface.md) | the same, from the host side: its docs/reference/host-imports.md |

They change together whenever the host interface changes: a QEMU build
that calls a function m-browser does not provide will not start.

## The two builds

| | `make` | `make THREADS=1` |
|---|---|---|
| WebAssembly target | `wasm32-wasip1` | `wasm32-wasip1-threads` |
| Virtual CPUs | 1, run inline by the main loop | 1-8, one host thread each (m-browser uses 4) |
| Output | `qemu-build/` | `qemu-build-threads/` |
| Strength | simplest; profilers work | stays responsive while the guest is busy |
| Shipped in m-browser | no | yes |

## How fast is it

On a 20-thread laptop, multi-threaded build: the desktop after about
2.5 minutes, Chromium's window after about 4, and Chromium answers a
click in 6-9 seconds, even while it is still starting.  At the start of
the work described in [17-history.md](17-history.md) the desktop took
7 minutes, Chromium 11-12, and clicks over 2 minutes.  The emulated CPU
is roughly a few hundred times slower than the host's.  Details and
methods: [15-performance.md](15-performance.md).

## Reading paths

| I want to... | Read |
|---|---|
| Understand the concepts first | [02-concepts.md](02-concepts.md), then [04-architecture.md](04-architecture.md) |
| Build and run it | [03-getting-started.md](03-getting-started.md) |
| Find my way around the files | [05-repository-tour.md](05-repository-tour.md) |
| Change QEMU's code | [10-changing-qemu.md](10-changing-qemu.md), [06-patch-series.md](06-patch-series.md) |
| Move to a newer QEMU release | [11-upgrading-qemu.md](11-upgrading-qemu.md) |
| Add a feature | [12-adding-features.md](12-adding-features.md) (and m-browser's docs/13-adding-features.md for the host side) |
| Work on the code generator | [07-tcg-backend.md](07-tcg-backend.md) |
| Work on threads | [08-threads.md](08-threads.md) |
| Work on the screen, input or network | [09-display-input-network.md](09-display-input-network.md) |
| Test a change | [13-testing.md](13-testing.md) |
| Fix a bug | [14-debugging.md](14-debugging.md), [known-issues.md](known-issues.md) |
| Make it faster | [15-performance.md](15-performance.md), [tools/perf/README.md](../tools/perf/README.md) |
| Change the guest (Alpine image) | [16-guest-image.md](16-guest-image.md) |
| Know why something is the way it is | [17-history.md](17-history.md) |
| Look up a term or a setting | [reference/glossary.md](reference/glossary.md), [reference/environment-variables.md](reference/environment-variables.md) |
