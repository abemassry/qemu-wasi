# qemu-wasi documentation

QEMU, the machine emulator, compiled to WebAssembly and running a Linux
desktop inside the m-browser application.  These documents start with a
high-level overview and go down to the details.  They are written for
developers who know general programming but not emulators, compilers or
WebAssembly: every term is explained where it first matters, and in the
[glossary](reference/glossary.md).

Start with [01-overview.md](01-overview.md).

## Start here

| Chapter | What it covers |
|---|---|
| [01-overview.md](01-overview.md) | The project from 10,000 feet; reading paths for each job |
| [02-concepts.md](02-concepts.md) | Primer: computers, emulation, QEMU, WebAssembly, WASI, the toolchain |
| [03-getting-started.md](03-getting-started.md) | From a fresh machine to a running desktop; daily commands |
| [04-architecture.md](04-architecture.md) | The map: layers, control flow, memory, threads, design decisions |
| [05-repository-tour.md](05-repository-tour.md) | Every file and directory; each build script step by step |

## How it works

| Chapter | What it covers |
|---|---|
| [06-patch-series.md](06-patch-series.md) | The 8 patches to QEMU, one section each, with conflict hot spots |
| [07-tcg-backend.md](07-tcg-backend.md) | The WebAssembly code generator, its interpreter tier and its JIT tiers |
| [08-threads.md](08-threads.md) | The multi-threaded build: one thread per virtual CPU |
| [09-display-input-network.md](09-display-input-network.md) | The QEMU side of the screen, keyboard and mouse, and the network |

## Changing it

| Chapter | What it covers |
|---|---|
| [10-changing-qemu.md](10-changing-qemu.md) | Day-to-day workflow for changing the patches |
| [11-upgrading-qemu.md](11-upgrading-qemu.md) | Moving to a newer QEMU release, step by step |
| [12-adding-features.md](12-adding-features.md) | Recipes: devices, host functions, settings, stubs, and more |
| [13-testing.md](13-testing.md) | The instruction test, boot tests, smoke tests; what to run before committing |
| [14-debugging.md](14-debugging.md) | A debugging method, the tools, and case studies |
| [15-performance.md](15-performance.md) | Measurements, where the time goes, the knobs, ideas |
| [16-guest-image.md](16-guest-image.md) | The Alpine disk image: contents, settings, inspecting and changing it |
| [17-history.md](17-history.md) | What was done, in what order, and why |
| [known-issues.md](known-issues.md) | Known bugs, limitations and stale comments |

## Reference

| Document | What it covers |
|---|---|
| [reference/host-interface.md](reference/host-interface.md) | Every function QEMU imports from and exports to its host |
| [reference/environment-variables.md](reference/environment-variables.md) | Every environment variable, its default and effect |
| [reference/glossary.md](reference/glossary.md) | Terms, A to Z |
| [../tools/perf/README.md](../tools/perf/README.md) | The timing and benchmark scripts |

The host side (m-browser) has its own documentation in the m-browser
repository's `docs/` directory.
