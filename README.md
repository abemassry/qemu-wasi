# qemu-wasi

QEMU 9.1 compiled to WebAssembly for WASI, running a full x86-64 Linux
desktop inside [m-browser](https://github.com/abemassry/m-browser), a
wasmtime-based browser, with the display shown on a
[wasi-gfx](https://github.com/wasi-gfx) surface.

QEMU itself is a single WASI module, built either single-threaded
(`wasm32-wasip1`) or with one thread per virtual CPU (`wasm32-wasip1-threads`).
Everything it needs from the outside, such as a JIT, a screen, input and a
network, comes through a small set of host functions that m-browser
implements.

**Documentation:** start with [docs/README.md](docs/README.md), which
leads from a high-level overview down to the details: building, the patch
series, the WebAssembly code generator, threads, upgrading QEMU, adding
features, testing, debugging and performance.  Timing and benchmark
scripts are in [tools/perf](tools/perf/README.md).

## Status

Tested with an Alpine Linux 3.23 disk image (Xorg and Fluxbox, auto-login),
on a 20-thread x86-64 Linux host:

- Boots from a qcow2 disk to the desktop in about 2-2.5 minutes; Chromium's
  window appears after about 4 (see [docs/15-performance.md](docs/15-performance.md)).
- Keyboard and mouse (absolute pointer through a USB tablet). The QEMU
  monitor and the guest's serial port are text consoles: Ctrl+Alt+1/2/3
  switch between the guest display, the monitor and the serial console.
- User-mode networking, as with QEMU's usual `-nic user`: DHCP, DNS, TCP,
  UDP and ping. The guest browses the web, and installs packages with
  apk. Host ports can be forwarded into the guest (`hostfwd`), e.g. for
  ssh.
- A differential test of 351 x86 instruction forms gives the same results
  as native QEMU (`make test`, `make THREADS=1 test`).

Limitations: one vCPU in the single-threaded build, at most 8 in the
multi-threaded one (whose guest memory ordering is right only on x86-64
hosts); no IPv6 networking, and no UDP port forwarding; no QMP (the HMP
monitor works); no audio. See [docs/known-issues.md](docs/known-issues.md).
Under
Xorg, disable glamor acceleration in the guest (see [Guest
setup](#guest-setup)), otherwise X takes half an hour to start.

## How it works

**The JIT.** QEMU's TCG translates guest code into translation blocks
(TBs). A new TCG backend (`tcg/wasm32`) turns each TB into a small
WebAssembly module. WASI has no API for compiling wasm at run time, so
QEMU asks the host: `wasi_jit_compile()` makes the host compile and
instantiate the module. TB modules import QEMU's own memory and function
table, and the host puts each TB's entry function into QEMU's table, so
QEMU calls a TB like any C function pointer. Compiling costs about 1 ms
per TB, so TBs are first run by an interpreter that executes their wasm
code directly, and are compiled once they have run 5000 times. The
backend is based on Kohei Tokunaga's Emscripten backend, [TCG: add wasm
backend](https://github.com/qemu/qemu/commit/bf5378b605d46548c0387e925fc9683903580487).

**Threads.** In the single-threaded build the vCPU runs inline in the
main loop for 30 ms time slices, coroutines run without stack switching,
and work that QEMU would hand to other threads (RCU callbacks, the thread
pool) runs in the main loop too. The multi-threaded build (`make
THREADS=1`) uses wasi-threads: each vCPU runs on a thread of its own
(QEMU's MTTCG), and QEMU's memory is shared between the threads; see
[docs/08-threads.md](docs/08-threads.md).

**A reactor.** QEMU is linked as a WASI reactor: the host calls
`wasi_qemu_step()` to run one main loop iteration at a time. Between
calls no wasm frames are live, which lets the host move QEMU into a fresh
wasmtime Store by copying its memory. That is the only way to free the
instances of compiled TBs, which a Store keeps until it is dropped.

**The display.** `display/` is a QEMU display backend that hands each
frame to the host (`host_gfx_update_framebuffer()`), and fetches keyboard
and pointer events from it (`host_gfx_poll_events()`). m-browser shows
the frames on a wasi-gfx surface (`wasi-surface-wasmtime`), currently by
blitting them with softbuffer.

**The network.** QEMU's `user` network backend passes Ethernet frames to
the host (`host_net_*()`), which emulates the network that libslirp
normally provides: the guest gets 10.0.2.15 by DHCP, 10.0.2.2 is the
gateway and 10.0.2.3 the DNS server, and traffic goes out through host
sockets. The guest's TCP connections end in a TCP/IP stack on the host
([smoltcp](https://github.com/smoltcp-rs/smoltcp)) that answers for any
address, and their data is relayed through host TCP sockets; slirp
works the same way.

### Host interface

Imports, all from module `env`:

| Function | Purpose |
|---|---|
| `wasi_jit_compile(wasm, len, helpers, n) -> handle` | compile and instantiate a TB module; returns its function table index |
| `wasi_jit_load_cached(wasm, len, helpers, n) -> handle` | the same, but only if the host still has the module compiled |
| `wasi_jit_load_async(wasm, len, helpers, n) -> handle` | the same, compiling on another host thread if needed: 0 until ready |
| `host_gfx_init(width, height)` | set up the display |
| `host_gfx_resize(width, height)` | change the display size |
| `host_gfx_update_framebuffer(data, len, width, height, stride)` | show a frame (BGRA) |
| `host_gfx_poll_events(buf, max) -> count` | fetch input events (`[type, code, value]` triples) |
| `host_net_send(frame, len)` | send an Ethernet frame from the guest |
| `host_net_recv(buf, size) -> len` | receive the next frame for the guest (0: none) |
| `host_net_wait(timeout_ns)` | sleep until a frame arrives, or the timeout |
| `host_net_hostfwd(rule, len) -> 0 or -1` | forward a host port into the guest, for a `hostfwd` rule |
| `host_main_wake()` | (threads build) end the main loop's sleep in `host_net_wait` |
| `host_vcpu_run(cpu, stack_top, tls_base)` | (threads build) run a vCPU's loop in the host until it is unplugged |

Plus `wasi_snapshot_preview1` (files, clocks, random, args, environment),
and in the threads build `wasi.thread-spawn` and the shared memory
`env.memory`. The complete contract is in
[docs/reference/host-interface.md](docs/reference/host-interface.md).

Exports: `_initialize` (the WASI reactor's initialization, called first),
`wasi_qemu_init()` (start QEMU with its command line),
`wasi_qemu_step()` (one main loop iteration; returns -1 to continue, or
the exit status), `wasi_qemu_jit_reset()` (forget all compiled TBs, after
a Store move), `wasi_qemu_quit()` (power off and close the disk images
cleanly), and `memory` and `__indirect_function_table`; in the threads
build also `wasi_vcpu_enter()` (one step of a vCPU's loop, called by the
host) and `wasi_thread_start()` (wasi-threads' thread entry).

The host side is in m-browser: `src/qemu_jit.rs`, `src/qemu_gfx.rs`,
`src/qemu_net.rs` (and `src/qemu_net/tcp.rs`), `src/qemu_threads.rs` and
`run_qemu_reactor()` in `src/wasm.rs`.

## Repository layout

| Path | Contents |
|---|---|
| `patches/` | The changes to QEMU, as a patch series against QEMU v9.1.0 (`BASE`) |
| `display/` | The wasi-gfx display backend, compiled into QEMU by the build |
| `glib-wasi/` | A minimal GLib for WASI (QEMU depends on GLib) |
| `wasi-stubs/` | Stand-ins for what wasi-libc lacks: pthreads for a single thread, sockets, ... |
| `scripts/` | Build helpers (the Makefile runs them) |
| `tests/x86-insn-diff/` | The instruction test against native QEMU |
| `tools/perf/` | Boot timing, click latency and guest benchmark scripts |
| `docs/` | The documentation |

The patches, by area:

1. `wasi: build QEMU for wasm32-wasi`: configure and meson, portability layer, coroutine backend
2. `tcg/wasm32: add a WebAssembly backend for WASI hosts`: the TCG backend, JIT interface and interpreter
3. `accel/tcg: run the vCPU inline from the main loop on WASI`
4. `util: main loop, coroutines, RCU and thread pool without threads`
5. `ui: draw text consoles without pixman`
6. `net: user-mode networking provided by the WASI host`
7. `system: run QEMU as a WASI reactor`
8. `wasi: run each vCPU on a thread of its own (WASI_THREADS)`: the multi-threaded build

They are grouped by area for reading, and only build as a whole series.

## Building

Requirements:

- [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) in `/opt/wasi-sdk`
  (or set `WASI_SDK`); tested with wasi-sdk 29
- git, make, python3 (with venv), ninja and pkg-config
- network access: the build clones QEMU, its meson subprojects and zlib

```sh
make            # fetch QEMU into qemu-src/, apply patches/, build
make install    # copy qemu-build/qemu-system-x86_64 into ../m-browser
```

The result is `qemu-build/qemu-system-x86_64`, a ~40 MB wasm module.
`M_BROWSER=/path/to/m-browser make install` installs it elsewhere.

### The multi-threaded build

```sh
make THREADS=1           # build qemu-build-threads/qemu-system-x86_64
make THREADS=1 install   # install it instead of the single-threaded one
make THREADS=1 test
```

It is built for `wasm32-wasip1-threads` and runs each vCPU on a thread of
its own (multi-threaded TCG, patch 0008), so a guest with several vCPUs
runs that many things at once: Chromium's processes, X, the kernel. m-browser
recognizes it (it imports its memory) and gives it 4 vCPUs (2 on hosts with
fewer than 8 CPUs; `QEMU_SMP` overrides) unless QEMU's arguments set `-smp`;
the threads share QEMU's memory, a WASI
context, the compiled TBs and the display (see m-browser's
`src/qemu_threads.rs`). Booting is a little slower than with the
single-threaded build, but the guest stays responsive while it is busy: with
Chromium starting, clicks are handled within seconds instead of being lost.

Guest memory accesses from different vCPUs are ordered as x86's are only on
x86-64 hosts.

## Running

Build m-browser (`cargo build --release`), and put the disk image at
`m-browser/m.qcow2`. Then run `target/release/m-browser` and load
`file:///<path to m-browser>/qemu-system-x86_64.wasm`. Closing the window
or the browser shuts QEMU down cleanly; the guest sees a power cut.

m-browser starts QEMU with:

```
-L /bios -drive file=/data/m.qcow2,format=qcow2 -m 1024
-accel tcg,tb-size=256 -device qemu-xhci -device usb-tablet
-monitor vc -serial vc -nic user
```

where `/bios` is `m-browser/pc-bios` and `/data` is m-browser's working
directory. For the multi-threaded build it adds `-smp 4` (see above).

To forward host ports into the guest, replace `-nic user` by setting
`QEMU_NIC`, with rules in QEMU's
`hostfwd=[tcp]:[hostaddr]:hostport-[guestaddr]:guestport` form. For
example, to reach the guest's ssh server:

```sh
QEMU_NIC=user,hostfwd=tcp:127.0.0.1:2222-:22 target/release/m-browser
ssh -p 2222 user@127.0.0.1
```

Without a host address, a forwarded port listens on all of the host's
network interfaces.

There is also a headless runner for testing and debugging, which can type,
click, and save the screen to a PNG (see `src/headless.rs` in m-browser):

```sh
m-browser --qemu-headless [--preopen HOST=GUEST]... qemu-system-x86_64.wasm <QEMU arguments>
```

### Guest setup

With glamor (OpenGL on the CPU), Xorg takes about 30 minutes to start.
Turn it off with `/etc/X11/xorg.conf.d/20-modesetting.conf` in the guest:

```
Section "Device"
	Identifier "modesetting"
	Driver "modesetting"
	Option "AccelMethod" "none"
EndSection
```

Give the guest at least 512 MB: with less, a desktop swaps constantly, and
the guest kernel drops keystrokes. Starting Chromium needs about 1 GB: with
512 MB it pushes ~100 MB to swap, and the guest kernel spends a sixth of its
time reclaiming and compacting memory.

### Environment variables

m-browser passes `QEMU_*` and `WASI_*` variables through to QEMU.

| Variable | Effect |
|---|---|
| `QEMU_JIT_HOT=<n>` | interpreted runs before a TB is compiled (default 5000; 0 compiles at once) |
| `QEMU_JIT_WARM=<n>` | runs before a TB whose module the host still has is reloaded (default 50) |
| `QEMU_JIT_ASYNC=0` | compile hot TBs synchronously, instead of in the background while they stay interpreted |
| `QEMU_TB_SLICE_MS=<ms>` | how long the vCPU runs between main loop iterations (default 30); each one also costs a busy guest a timer tick |
| `QEMU_TB_SLICE=<n>` | count slices in TBs instead (formerly fixed at 4000) |
| `QEMU_TICK_SLACK_MS=<ms>` | multi-threaded build: the guest's timer interrupts come on this grid (default 30; 0 = on time), for the same reason |
| `QEMU_PROF_EVERY=<s>` | print a time profile every s seconds (single-threaded build only) |
| `QEMU_STATUS_EVERY=<s>` | print the vCPU state and recent TBs every s seconds |
| `QEMU_TB_DUMP=<pc>,<dir>` | save the wasm module of the TB at guest pc |
| `QEMU_DUMP_AT=<pc>,<n>,<addr>,<len>,<file>` | dump guest memory on the n-th run of the TB at pc |
| `QEMU_LOOP_DIAG=1` | log TCG details when the guest runs one TB 20 million times in a row |
| `QEMU_DISPLAY_STATS=1` | print the display refresh cost |
| `WASI_DEBUG=1`, `2` | trace startup (1), and every TB (2: very slow) |
| `WASI_GFX_SCALE=<n>`, `WASI_GFX_CONSOLE=<n>`, `WASI_GFX_FULL_REFRESH=1` | display: zoom factor, initial console, redraw every frame |

Read by m-browser itself: `QEMU_NIC` (the `-nic` option, default `user`;
see [Running](#running)), `QEMU_NET_DEBUG=1` (log the network traffic),
`QEMU_JIT_STATS=1` (JIT statistics every minute), `QEMU_JIT_THREADS=<n>`
(background compiler threads, default a quarter of the CPUs, at most 4),
`QEMU_SMP=<n>` (vCPUs of the multi-threaded build, default 4, or 2 on hosts
with fewer than 8 CPUs; at most 8),
`QEMU_MIGRATE_AFTER=<n>` (TB modules a Store may hold before it is
replaced, default 100000),
`QEMU_JIT_DUMP=<dir>[,<n>]` (save every n-th compiled TB module),
`QEMU_WASM_OPT=speed` (optimize compiled code more), `QEMU_PROFILE=1` (sample
where QEMU spends its time), and for the headless runner `QEMU_SNAPSHOT`,
`QEMU_TYPE`, `QEMU_TYPE_FIFO` and `QEMU_QUIT_AFTER`.

## Changing QEMU

`qemu-src/` is a git repository on branch `qemu-wasi`, with one commit
per patch on top of the `v9.1.0` tag:

1. Edit and commit in `qemu-src/`. To change an existing patch, use
   `git commit --fixup` and `git rebase -i --autosquash`, or amend.
2. `make` rebuilds.
3. `make export-patches` writes the commits back to `patches/`; commit
   those here.

After pulling a changed series, `make reset-qemu` re-applies `patches/`
to a pristine tree. This discards any changes in `qemu-src/`, so export
them first.

`tcg/wasm32/wasi-interp-calls.c.inc` is generated:
`scripts/gen_interp_calls.py > qemu-src/tcg/wasm32/wasi-interp-calls.c.inc`.

## Testing

```sh
make native     # build a native QEMU from the same tree (needs QEMU's usual
                # build dependencies, e.g. the glib development package)
make test       # run the instruction test with both QEMUs (needs gcc and m-browser)
```

`QEMU_JIT_HOT=0 make test` tests the JIT alone, and
`QEMU_JIT_HOT=4000000000 make test` the interpreter alone; add `THREADS=1`
for the multi-threaded build.

## Credits

- [QEMU](https://www.qemu.org/), GPL-2.0.
- Kohei Tokunaga's Emscripten TCG backend, the starting point of
  `tcg/wasm32`.
- [wasi-gfx](https://github.com/wasi-gfx) and its runtime, which
  m-browser uses for the window and surface.
