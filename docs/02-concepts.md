# Concepts: emulators, QEMU, WebAssembly and WASI

**What this covers.** The ideas the rest of this documentation takes for
granted: what an emulator is, how a PC gets from power-on to a desktop, how
QEMU imitates one, what WebAssembly and WASI can and cannot do, and how the
pieces cooperate while the guest runs. It assumes general programming
experience but no background in emulators, operating systems, compilers or
WebAssembly. For short definitions use the [glossary](reference/glossary.md);
this chapter is its narrative counterpart.

## 1. The big picture

### Emulators, hosts and guests

An **emulator** is a program that imitates a computer closely enough that
software written for that computer runs on it unchanged. QEMU's
`qemu-system-x86_64` imitates a whole 64-bit PC: processor, memory, disk,
network card, graphics card, keyboard and mouse. The software inside cannot
tell that its hardware is made of C code.

Two words appear everywhere:

- The **host** is what really runs: your computer, running m-browser, which
  runs QEMU. The word also means m-browser itself, as in "host function".
- The **guest** is the imitated machine and everything inside it: an Alpine
  Linux system with the Linux kernel, the X window system, the Fluxbox
  window manager and Chromium.

An analogy: the guest is a play, QEMU is the stage and its props, and the
host is the theatre. The actors only ever touch the props; when they need
water or electricity, the stage crew gets it from the building.

In this project: the guest boots from m-browser's disk image `m.qcow2`
([16-guest-image.md](16-guest-image.md)).

### Why QEMU in WebAssembly is unusual

On a normal Linux machine QEMU relies on three things it does not have here:

- **Hardware virtualization.** When guest and host have the same kind of
  CPU, QEMU normally lets the real CPU run guest code directly (with KVM, a
  *hypervisor*), at nearly native speed. A WebAssembly module can only run
  wasm instructions, so this build emulates every guest instruction in
  software with TCG (`-accel tcg`), which is many times slower.
- **Generating machine code.** TCG normally translates guest instructions
  into host machine code, writes it into memory and jumps to it (a
  just-in-time compiler, or **JIT**). In WebAssembly code and data live in
  separate worlds, and a module cannot create functions for itself. Here
  QEMU produces small WebAssembly modules instead and asks the host to
  compile and load them.
- **Direct access to the operating system.** Native QEMU opens files,
  sockets and windows, starts threads and handles signals itself. A wasm
  module can only call the functions its host gives it, so every outside
  service (the screen, the network, the JIT compiler, threads) comes from
  m-browser.

```text
+-----------------------------------------------------------------+
| Guest software (Alpine Linux)                                   |
|   Linux kernel, OpenRC, Xorg, Fluxbox, Chromium                 |
+-----------------------------------------------------------------+
        |  x86-64 instructions; reads and writes of device registers
        v
+-----------------------------------------------------------------+
| Emulated PC: QEMU (qemu-system-x86_64, a WebAssembly module)    |
|   CPU (TCG), RAM, IDE disk, e1000, VGA, USB tablet, serial      |
+-----------------------------------------------------------------+
        |  WASI calls and "env" imports: JIT, screen, network, threads
        v
+-----------------------------------------------------------------+
| WebAssembly runtime: wasmtime, embedded in m-browser (Rust)     |
|   runs QEMU; m-browser adds JIT, window, network, threads       |
+-----------------------------------------------------------------+
        |  ordinary system calls
        v
+-----------------------------------------------------------------+
| Host operating system (e.g. x86-64 Linux) and hardware          |
+-----------------------------------------------------------------+
```

In this project: the boundary between QEMU and wasmtime is the *host
interface*, listed function by function in
[reference/host-interface.md](reference/host-interface.md). The host's
side, with a primer on Rust and wasmtime, is in the m-browser repository's
docs/02-concepts.md.

## 2. A computer in 10 minutes

A rough model of a real PC is enough to follow what QEMU imitates. This one
is deliberately simplified.

### The CPU, instructions and registers

The **CPU** (processor) does one thing over and over: it fetches an
**instruction** from memory, executes it and moves on to the next. An
instruction is a few bytes meaning "add these two numbers" or "if the last
result was zero, continue at address X". x86-64 instructions are 1 to 15
bytes long, which makes them tedious to decode.

**Registers** are the CPU's own small, fast variables: sixteen 64-bit
general-purpose registers (`rax`, `rbx`, ..., `r15`), a flags register that
records properties of the last result (zero, negative, carry), and the
**instruction pointer** (`rip`, which QEMU calls *pc*), the address of the
next instruction. The CPU also has a privilege level: the kernel runs in
**kernel mode** and may touch hardware, while applications run in **user
mode** and ask the kernel for everything else through **system calls**. A
PC starts in a 16-bit mode from 1978 ("real mode"); the kernel switches it
to 64-bit "long mode" while booting.

In this project: QEMU keeps the guest CPU's registers in an ordinary C
struct, `CPUX86State`; the monitor's `info registers` prints them.

### Memory, the MMU and page tables

RAM is a big array of bytes numbered from 0: **physical addresses**.
Programs do not use them directly. Each sees its own **virtual address
space**, and the **MMU** (memory management unit), part of the CPU,
translates every virtual address into a physical one.

The translation is described by **page tables**: a tree of tables in RAM,
written by the kernel, that maps 4 KiB **pages** of virtual memory onto
physical pages, with permission bits (writable, executable, kernel only).
Walking that tree on every access would be slow, so the CPU caches recent
translations in a **TLB** (translation lookaside buffer). When no valid
mapping exists, the CPU raises a **page fault**, and the kernel decides what
to do: read the data from disk, grow a stack, or stop the program.

Not every physical address leads to RAM. Some ranges are wired to devices,
and reading or writing them talks to the hardware: **memory-mapped I/O**
(MMIO). x86 also has a small, separate **I/O port** space, used by older
devices through the `in` and `out` instructions.

In this project: the guest's page tables are Linux's, in guest RAM, and
QEMU walks them in software (the softmmu TLB, below); the monitor's
`info mtree` shows QEMU's map of guest physical memory.

### Interrupts and timers

A CPU busy in a loop would never notice a key press unless something
interrupted it. An **interrupt** makes the CPU save what it was doing, run
a kernel function (an interrupt handler) and then resume. Devices raise
interrupts when they need attention: a frame arrived, a disk read finished,
a key was pressed. Errors the CPU detects itself (page fault, division by
zero) work the same way and are called **exceptions**.

A **timer** is a device that raises an interrupt after a delay. The kernel
uses one to interrupt itself regularly, the **timer tick**, up to HZ times
a second (a kernel build setting, commonly 100 to 1000), to keep time and
to switch between programs. Under emulation each tick costs the guest
thousands of instructions.

In this project: QEMU deliberately delivers the guest's ticks late, on a
30 ms grid, because at 1000 a second they took half of a busy guest's time
(patches 0003 and 0008; see [15-performance.md](15-performance.md)).

### Devices

- A **disk controller** moves blocks (sectors) between the disk and RAM,
  usually by **DMA** (direct memory access: the device writes into RAM
  itself, without the CPU copying each byte), then raises an interrupt.
- A **network card** (NIC) sends and receives Ethernet **frames**, the
  packets of the local network, also by DMA and interrupts.
- A **display adapter** shows a **framebuffer**, a region of memory holding
  one value per pixel.
- The **keyboard** reports key presses as **scancodes**, on a PC through
  the PS/2 controller.
- **USB** connects further devices through a host controller. A USB
  **tablet** reports absolute positions, so the guest's pointer can follow
  the host's exactly; a mouse only reports movements.
- A **serial port** is a simple character channel, much used for debugging.

The kernel talks to each device through a **driver** that knows which
registers to read and write.

In this project: m-browser's `src/app.rs` builds QEMU's command line. The
disk image `/data/m.qcow2` sits on the PC's IDE controller, the NIC is an
Intel e1000 (`-nic user`), the display the standard VGA, and the tablet a
`usb-tablet` on a `qemu-xhci` controller; RAM is `-m 1024`.

### From power-on to desktop

```text
 power on
    |
    v
 firmware (SeaBIOS)       in the emulated PC's ROM: sets up the hardware,
    |                     finds a bootable disk
    v
 bootloader (extlinux)    at the start of the disk: loads the kernel and
    |                     the initramfs, passes the kernel command line
    v
 Linux kernel             switches to 64-bit mode, sets up page tables,
    |                     starts drivers, unpacks the initramfs
    v
 initramfs                small temporary root filesystem in RAM: loads
    |                     drivers, finds and mounts the real root
    v
 init + OpenRC            process 1: starts the services in order
    |                     (devices, network, ...)
    v
 user space               Xorg, Fluxbox, Chromium
```

- **Firmware** is the program built into the machine. A PC's is the BIOS;
  QEMU uses the open-source **SeaBIOS**.
- The **bootloader** is a small program on the disk whose only job is to
  load the kernel. Alpine uses **extlinux** (from Syslinux), configured in
  `/boot/extlinux.conf`.
- The **kernel** is the core of the operating system: it manages memory,
  processes, devices and files.
- The **initramfs** is a compressed archive unpacked into RAM, with just
  enough tools and drivers to find the real disk.
- **init** is the first program, process number 1. On Alpine it is
  BusyBox's init, which runs **OpenRC**, the service manager.
- **User space** is everything else: the X server (Xorg) drives the
  framebuffer, the window manager (Fluxbox) draws window frames and menus,
  and applications such as Chromium draw their content through X.

In this project: SeaBIOS comes from m-browser's `pc-bios/` (`-L /bios`).
The scripts in `tools/perf/` skip the first two stages by handing QEMU the
kernel and initramfs directly (`-kernel`, `-initrd`); see
[16-guest-image.md](16-guest-image.md) and [13-testing.md](13-testing.md).

## 3. How QEMU emulates a PC

### The machine and device models

QEMU assembles a virtual PC from parts. The **machine type** names the
motherboard; `pc` (`pc-i440fx-9.1`) is a 1990s-style PC built around
Intel's 440FX chipset, old but simple and supported by every operating
system. (`q35` is the modern alternative.)

Each piece of hardware is a **device model**, C code under `qemu-src/hw/`
(such as `hw/net/e1000.c` or `hw/display/vga.c`) that behaves like the real
device as the guest sees it. A model claims address ranges in QEMU's map of
guest physical memory, a tree of `MemoryRegion` objects. When the guest
reads or writes there, QEMU calls the model's read or write callback, much
as a web framework calls a route handler. The model reacts, perhaps reads
the disk image, and may raise an interrupt.

In this project: m-browser passes no `-M`, so the guest is a `pc`. Device
models are almost untouched; what changes is how QEMU reaches the outside
world, its **backends** for display, input and network
([09-display-input-network.md](09-display-input-network.md)).

### CPU emulation with TCG

QEMU's CPU emulator is **TCG**, the Tiny Code Generator. Interpreting guest
instructions one at a time would be very slow, so TCG is a compiler that
works while the guest runs (a *dynamic binary translator*):

```text
 guest x86-64 machine code, in guest RAM
          |
          |  front end (target/i386/tcg/): decode one translation block
          v
 TCG ops: portable intermediate code (add_i64, brcond_i32, qemu_ld, call)
          |
          |  optimizer (tcg/optimize.c)
          v
 TCG ops, simplified
          |
          |  back end (tcg/<host>/, here tcg/wasm32/)
          v
 host code for the TB (here: the bytes of a small wasm module)
          |
          v
 kept in the TB cache --> run --> next TB (chained, or looked up)
```

- A **translation block** (TB) is a run of guest instructions (at most 512)
  that ends at a jump or similar. TCG translates and runs one TB at a time.
- **TCG ops** are a small, portable intermediate language. A **front end**,
  one per guest architecture, turns guest instructions into ops; a **back
  end**, one per host architecture, turns ops into host code.
- The **TB cache** keeps translations, found by guest address, so a TB is
  translated once and run many times. They live in the **TB buffer**; when
  it is full, QEMU **flushes** it (discards every translation) and
  translates again as code runs.
- **Chaining** lets a TB continue straight into the next one without going
  back to the vCPU loop, `cpu_exec()`.
- **Helpers** are C functions that translated code calls for work too
  complex to emit inline: division, `cpuid`, floating point, interrupts,
  memory accesses that miss the TLB.
- The **softmmu TLB** is QEMU's software version of the MMU's cache. Every
  guest memory access must turn a guest virtual address into a place in
  QEMU's memory. Translated code checks the TLB inline (the fast path); on
  a miss, or for device memory, it calls a helper (the slow path), which
  walks the guest's page tables and may raise a page fault in the guest.

In this project: patch 0002 adds the back end `tcg/wasm32/` (with
`tcg/wasm32.c`), and the x86 front end is unchanged. A wasm function cannot
jump into another, so a TB "chains" by returning the next TB's address to a
loop, `tcg_qemu_tb_exec()`, that calls it. See
[07-tcg-backend.md](07-tcg-backend.md).

### The main loop: timers and bottom halves

Apart from the CPUs, QEMU is event-driven, like Node.js. The **main loop**
waits for something to happen (a file descriptor becomes readable, a timer
expires) and runs the callbacks registered for it in an `AioContext`:

- **Timers** (`QEMUTimer`) call a function at a time on a clock.
  `QEMU_CLOCK_VIRTUAL` is guest time, which stops while the guest is
  paused; `QEMU_CLOCK_REALTIME` and `QEMU_CLOCK_HOST` follow the host.
  Emulated timer chips are built on them.
- **Bottom halves** (BHs) are "run this soon, from the main loop" callbacks,
  comparable to `setTimeout(fn, 0)`: code that cannot finish its work right
  now schedules one.

In this project: native QEMU's `main()` enters the main loop and never
returns. Here the host calls the export `wasi_qemu_step()`, which runs one
iteration (`qemu_main_loop_step()` in `system/runstate.c`, patch 0007).
glib-wasi's own main loop never runs anything, so QEMU polls its
AioContexts itself (patch 0004).

### Threads, the BQL and vCPUs

Native QEMU runs the main loop on one thread, the virtual CPUs (**vCPUs**)
on others, and blocking jobs such as disk reads on a **thread pool**.
Shared state is protected by one global mutex, the **big QEMU lock** (BQL,
`bql_lock()`): the main loop and the device models run with it held, and a
vCPU thread takes it only to touch a device. A vCPU busy in translated code
does not look around; other code **kicks** it (sets a counter that every TB
checks on entry) when it must come back for an interrupt or other work.

With several vCPUs (`-smp N`), TCG has two modes:

- **MTTCG** (multi-threaded TCG): one host thread per vCPU, all running in
  parallel. Fast on a multi-core host, but the guest's atomic operations
  and memory ordering must be reproduced faithfully.
- **Round-robin** (RR): one thread runs all the vCPUs, taking turns.

In this project: the single-threaded build (`make`) has no threads at all.
Its one vCPU runs round-robin without even that thread: the main loop runs
it for a **time slice** of about 30 ms per iteration (`wasi_rr_cpu_step()`,
patch 0003), and every lock is a no-op. The threaded build
(`make THREADS=1`, C macro `WASI_THREADS`, patch 0008) runs MTTCG with up
to 8 vCPUs, each on a thread of its own; see [08-threads.md](08-threads.md).

### RCU

**RCU** (read-copy-update) lets threads share data without readers taking
locks. A writer never changes the data in place: it publishes a new copy,
and the old one is freed (`call_rcu()`) only once no reader can still be
using it. QEMU uses RCU for its memory map, its TB hash table and more, and
a `call_rcu` thread normally does the freeing.

In this project: the single-threaded build has no such thread, so the main
loop runs the deferred frees itself (`rcu_run_callbacks_inline()`, patch
0004); otherwise old copies would never be freed.

### Coroutines

A **coroutine** is a function that can pause in the middle and resume
later where it stopped, like a JavaScript `async` function at an `await`.
QEMU's block layer (disk emulation and image formats such as qcow2) is
written with them, so that a disk request can wait for I/O without blocking
the emulator. Natively each coroutine has its own stack and QEMU switches
between stacks, which WebAssembly cannot do.

In this project: `util/coroutine-wasi.c` (patch 0001) runs a coroutine by
simply calling it, and a "yield" returns at once. That works because disk
I/O completes synchronously here: thread-pool jobs run at once on the
calling thread (patch 0004). Code that needs a coroutine to really wait
cannot work, which is why this build has no QMP (below).

### The monitor, the serial port and consoles

- The **monitor** is QEMU's own command prompt, separate from the guest.
  The human monitor, **HMP**, takes commands such as `info status`,
  `info registers` and `quit`. **QMP**, the JSON protocol for programs, is
  missing from this build.
- The emulated **serial port** (`ttyS0` in Linux, a 16550A UART) receives
  the guest kernel's messages when it boots with `console=ttyS0`, which is
  invaluable when the screen stays black.
- A **virtual console** (`vc`) is a text terminal that QEMU draws itself.

In this project: m-browser passes `-monitor vc -serial vc`, so Ctrl+Alt+2
shows the monitor, Ctrl+Alt+3 the serial port and Ctrl+Alt+1 the guest's
screen again. Patch 0005 draws these consoles without pixman, a graphics
library that does not build for WASI. See [14-debugging.md](14-debugging.md).

### User-mode networking

The guest's network card sends Ethernet frames, and something must turn
them into real network traffic. QEMU's **user-mode networking**
(`-nic user`, normally the libslirp library) emulates a small private
network: the guest gets 10.0.2.15 by DHCP, the gateway is 10.0.2.2 and the
DNS server 10.0.2.3, and the guest's connections leave through ordinary
host sockets, as behind a home router. It needs no administrator rights.

In this project: WASI cannot open network connections, so patch 0006 moves
that network into m-browser. QEMU passes each outgoing frame to
`host_net_send()` and fetches incoming ones with `host_net_recv()`;
m-browser emulates the network with the smoltcp and mio libraries. See
[09-display-input-network.md](09-display-input-network.md).

## 4. WebAssembly in 10 minutes

**WebAssembly** (wasm) is a portable binary format for programs, designed
to run in a sandbox at near-native speed. C compilers can target it like
any CPU, and a **runtime** (a web browser's engine, or wasmtime) compiles
the wasm into real machine code before running it.

### Modules, imports and exports

The unit of code is a **module** (a `.wasm` file): functions, globals,
memories and tables, plus two lists that connect it to the outside.

- **Imports** are what the module needs, each named by a module and a
  field, such as `wasi_snapshot_preview1.fd_write` or `env.host_net_send`.
  The host must supply every import when it **instantiates** the module.
  For a C programmer: `extern` functions that are defined at load time.
- **Exports** are what the module offers the host by name, such as
  `wasi_qemu_step`, its `memory` and its function table.

An **instance** is a loaded module with its own state, as an object is an
instance of a class.

In this project: QEMU is linked with `--allow-undefined`, so every C
function it calls but does not define becomes an `env` import.

**Pitfall:** a misspelt or missing function therefore still links, and the
host then refuses to start QEMU with "unknown import".

### Linear memory

A module's data lives in a **linear memory**: one flat, growable array of
bytes, in which a C pointer is just an offset. In **wasm32**, the variant
used here, offsets are 32 bits, so a memory holds at most 4 GiB. It grows in
64 KiB wasm pages (not the guest's 4 KiB pages), never shrinks, and has no
permission bits or guard pages.

**Pitfall:** address 0 is a valid address. Dereferencing a NULL pointer
does not crash; it quietly reads or overwrites the first bytes of memory.

In this project: all of QEMU lives in that one array: its globals, heap and
stacks, the TB buffer (256 MiB with m-browser's `tb-size=256`) and the
guest's RAM (1 GiB with `-m 1024`). That is why guest RAM must stay well
under 4 GiB.

### Code is not data: the function table

Wasm code is not stored in linear memory and cannot be read, written or
jumped into. A C function pointer is an index into a **function table**,
and a call through it compiles to **`call_indirect`**, which checks at run
time that the function's type matches the call.

**Pitfall:** calling a function through a pointer of the wrong type (say,
an `int` function through a pointer to a `void` one) usually works natively
but **traps** in wasm.

In this project: QEMU exports its table, `__indirect_function_table`
(about 8,900 entries), and links it growable, so that the host can append
each compiled TB. The new index, the TB's **JIT handle**, is called like
any C function pointer, and TBs call helpers through the same table.

### Structured control flow

Wasm has no `goto`. Control flow is structured: `block`, `loop` and `if`
nest, and a branch can only leave an enclosing block or restart an
enclosing loop. TCG ops, however, jump to labels in either direction.

In this project: the back end numbers the code between labels as blocks,
each guarded by an `if`, inside one `loop`. A jump stores the target's
number in a local (`BLOCK_PTR`) and leaves its `if` (forward) or restarts
the loop (backward), and the guards skip every block before the target:
the block dispatch of [07-tcg-backend.md](07-tcg-backend.md).

### Traps and exceptions

A **trap** is wasm's crash: an out-of-bounds memory access, an integer
division by zero, an `unreachable` instruction (which `abort()` and failed
assertions execute), a `call_indirect` type mismatch, a stack overflow. It
ends the current call into the module, and the module cannot catch it.

**Exception handling** is a wasm feature for throwing and catching
(`throw`, `try_table`). wasi-sdk can compile C's `setjmp` and `longjmp` to
it, and QEMU depends on them: when a guest instruction faults inside a
helper, `cpu_loop_exit()` longjmps out of the helper and the TB, back to
`cpu_exec()`, which delivers the exception to the guest.

In this project: `osdep.h` maps `sigsetjmp()` to `setjmp()` (patch 0001),
`scripts/configure.sh` passes `-mllvm -wasm-enable-sjlj`, and m-browser
enables exceptions in wasmtime. QEMU's `exit()` reaches the host much like
a trap: the call into QEMU fails with an error carrying the exit status
(wasmtime's `I32Exit`), which m-browser checks for.

**Pitfall:** a wasm exception cannot unwind through host (Rust) functions,
which is one reason TB modules call helpers through QEMU's table rather
than through the host.

### Shared memory and atomics

The **threads** feature lets several instances, each on its own host
thread, use one **shared memory**, and adds atomic instructions,
`atomic.fence` and `wait`/`notify` (from which wasi-libc builds mutexes).

In this project: only the threaded build uses them; its memory is created
by the host and imported as `env.memory`.

### Core modules and components

The **component model** (WASI 0.2, "preview 2") wraps core modules behind
typed interfaces described in WIT. QEMU does not use it: it is a plain
**core module** of about 40 MB using WASI preview 1, plus the `env`
imports from m-browser.

In this project: the module is `qemu-build/qemu-system-x86_64` (or the same
in `qemu-build-threads/`), installed into m-browser as
`qemu-system-x86_64.wasm` by `make install`.

## 5. WASI

### A system interface for WebAssembly

On its own, wasm can compute but cannot even print. **WASI** (the
WebAssembly System Interface) is a standard set of imports for
operating-system services:

- **Files**, through **preopened directories**: the host opens chosen
  directories before the module starts, under names of its choosing, and
  the module can open nothing else, like volumes mounted into a container.
- **Clocks** (`clock_time_get`), **random numbers** (`random_get`),
  **arguments and environment** (`args_get`, `environ_get`), **waiting**
  (`poll_oneoff`) and **exiting** (`proc_exit`).

The version used here is **preview 1**, whose functions come from the
import module `wasi_snapshot_preview1`. C code does not call them directly:
**wasi-libc**, the C library, implements `open()`, `printf()`,
`clock_gettime()` and the rest on top of them.

In this project: m-browser preopens its `pc-bios/` directory as `/bios` and
its working directory as `/data` (hence `-drive file=/data/m.qcow2`), and
passes QEMU only the `QEMU_*` and `WASI_*` environment variables.

### Commands and reactors

A WASI module comes in one of two shapes:

- A **command** exports `_start`, which runs `main()` once, like an
  ordinary program.
- A **reactor** exports `_initialize`, which runs C constructors, and then
  stays alive while the host calls its other exports, like a library.

QEMU is linked as a reactor (`-mexec-model=reactor`, patch 0007).
m-browser calls `_initialize`, then `wasi_qemu_init()` once (parse the
command line, build the machine), then `wasi_qemu_step()` again and again
until it returns an exit status instead of -1, like a game loop that the
host owns. Between two calls no wasm code is running, so the host can, for
example, move QEMU into a fresh wasmtime *Store* by copying its memory,
the only way to free compiled TBs, and then call `wasi_qemu_jit_reset()`.

In this project: the exports are defined in `system/main.c`;
[reference/host-interface.md](reference/host-interface.md) lists them.

### wasi-threads

Preview 1 has no threads. The **wasi-threads** convention adds one import,
`wasi.thread-spawn`, which wasi-libc's `pthread_create()` calls. The host
starts an OS thread, instantiates the same module again on it, sharing the
memory, and calls the new instance's export
`wasi_thread_start(tid, start_arg)`, which runs the thread's function.

In this project: only the threaded build (`wasm32-wasip1-threads`) uses
it; see [08-threads.md](08-threads.md).

**Pitfall:** in the single-threaded build `pthread_create()` succeeds but
never runs the thread (`wasi-stubs/pthread_stubs.c`), so code that waits
for a thread it started waits forever; patches 0003 and 0004 remove such
waits.

### What WASI lacks, and how the project copes

QEMU was written for POSIX systems such as Linux. What it misses here:

| QEMU expects | What it gets here |
|---|---|
| Hardware virtualization (KVM) | Nothing: TCG only |
| To run the machine code it generates | Wasm modules compiled by the host (`wasi_jit_*`) |
| Threads | None (`make`), or wasi-threads (`make THREADS=1`) |
| Signals | None; QEMU installs no handlers (patch 0001) |
| Sockets | Stubs that fail; the network is `host_net_*` |
| `fork()` and `exec()` | Stubs that fail |
| `mmap()` and `mprotect()` tricks | Limited emulation; guest RAM from `aligned_alloc()` |
| eventfd or pipes, to wake the main loop | None; an event notifier is a flag |
| A window | `host_gfx_*` imports |

The project fills the gaps in four places:

- **wasi-stubs/**: stand-ins for what wasi-libc lacks: pthreads that do
  nothing (single-threaded build), BSD sockets that fail with `ENOSYS`,
  `fork()` and `exec()` that fail, empty user and group databases, terminal
  definitions. Its headers come before wasi-libc's.
- **glib-wasi/**: a minimal GLib, the C utility library QEMU requires
  everywhere (strings, lists, hash tables, logging), with only what QEMU
  uses; the real GLib does not build for WASI.
- **Host imports** from `env`, implemented by m-browser: compiling TBs
  (`wasi_jit_compile`, `wasi_jit_load_cached`, `wasi_jit_load_async`), the
  screen and input (`host_gfx_update_framebuffer`, `host_gfx_poll_events`),
  the network (`host_net_send`, ...) and threads (`host_vcpu_run`, ...).
- **Patches** to QEMU itself, `patches/0001` to `0008`, applied to QEMU's
  `v9.1.0` tag ([06-patch-series.md](06-patch-series.md)).

In this project: [reference/host-interface.md](reference/host-interface.md)
gives every import and export with its exact signature, and
[05-repository-tour.md](05-repository-tour.md) describes wasi-stubs and
glib-wasi.

## 6. The toolchain

- **wasi-sdk** is a ready-made LLVM toolchain for WASI, expected in
  `/opt/wasi-sdk` (or set `WASI_SDK`): `clang` for the targets
  `wasm32-wasip1` (single-threaded build) and `wasm32-wasip1-threads`
  (threaded build), the **wasm-ld** linker, and a *sysroot* holding
  wasi-libc and its headers.
- **Cross-compiling** means building on one machine (yours) for another
  target (wasm32). Everything QEMU links must be cross-compiled too, once
  per build flavor: zlib (from `zlib-src/` into `zlib-wasi/` and
  `zlib-wasi-threads/`), glib-wasi and wasi-stubs.
- QEMU's own build uses **configure**, which sets up **meson**, which
  writes a `build.ninja` file that **ninja** executes.
  `scripts/configure.sh` runs configure with wasi-sdk's clang and options
  such as `--target-list=x86_64-softmmu` and `--with-coroutine=wasi`, and
  restricts **pkg-config** (which tells a build where a library's headers
  and archives are) to glib-wasi and zlib, so that no library of your own
  machine slips in. `scripts/fix_build_ninja.py` then adds the display
  backend (`display/`) and the wasm link options (`-mexec-model=reactor`,
  `--allow-undefined`, `--export-table`, `--growable-table`, ...).
- **wasm-tools** inspects the result. To list QEMU's imports and exports:

```sh
wasm-tools print --skeleton qemu-build/qemu-system-x86_64 | grep -E '\((import|export)'
```

**Note:** wasmtime compiles QEMU's 40 MB module once, with optimizations,
and m-browser caches the result as a `.cwasm` file, so only the first start
after a new build is slow. TB modules are compiled with optimizations off,
in about 1 ms each.

In this project: the top-level `Makefile` drives all of this; see
[03-getting-started.md](03-getting-started.md) and
[05-repository-tour.md](05-repository-tour.md).

## 7. How the pieces cooperate at run time

### One guest instruction, end to end

```text
 m-browser (host)                    QEMU (wasm module)
 ----------------                    ------------------
 calls wasi_qemu_step() -----------> one main-loop iteration; the vCPU
                                     looks up the TB for the guest pc
                                       miss: x86 -> TCG ops -> module
                                       cold: interpret the module
                                       hot:  ask the host to compile it
 compiles it on a     <------------- wasi_jit_load_async(bytes, ...)
 qemu-jit thread, and                returns 0: keep interpreting
 adds its start
 function to the table -----------> a later ask returns the handle;
                                     from then on: call_indirect
                      <------------- wasi_qemu_step() returns -1
```

1. m-browser calls `wasi_qemu_step()`. QEMU runs due timers and bottom
   halves, then gives the vCPU a time slice (in the threaded build, vCPU
   threads run on their own instead).
2. `cpu_exec()` looks up the TB for the guest's current pc. On a miss, the
   x86 front end turns the guest instructions into TCG ops, and
   `tcg/wasm32` turns those into the bytes of a small wasm module, kept in
   the TB buffer.
3. Compiling a module takes about 1 ms, far longer than running it a few
   times, and most TBs only ever run a few times. So a new TB is **cold**:
   QEMU's own interpreter (`tcg/wasm32/wasi-interp.c.inc`) executes the
   module's wasm code directly.
4. Once a TB has run 5000 times (`QEMU_JIT_HOT`) it is **hot**, and QEMU
   passes its module to `wasi_jit_load_async()`. m-browser compiles it in
   the background with wasmtime, instantiates it with QEMU's memory and
   function table as its imports, and appends its `start` function to the
   table. Meanwhile QEMU keeps interpreting and asks again every 16 runs.
5. From then on the TB runs as compiled code, through `call_indirect`.

Details: [07-tcg-backend.md](07-tcg-backend.md).

### One keystroke

1. The host window gets a key event, and m-browser queues it as a Linux
   evdev key code.
2. On its refresh timer, QEMU's display backend
   (`display/wasi_gfx_display.c`) fetches the queued events with
   `host_gfx_poll_events()`, converts the key to QEMU's own code
   (`qemu_input_linux_to_qcode()`) and passes it to QEMU's input layer.
3. The emulated PS/2 keyboard turns it into scancodes and raises an
   interrupt; the guest kernel's driver reads them, and Xorg delivers the
   key to the focused window, say Chromium.
4. Chromium draws, through X, into the VGA framebuffer. On a later refresh
   the display backend copies what changed and passes the frame to
   `host_gfx_update_framebuffer()`, and m-browser shows it.

Details: [09-display-input-network.md](09-display-input-network.md).

### One network packet

1. Chromium sends a request; the guest kernel wraps it in an Ethernet frame
   and hands it to its e1000 driver.
2. The e1000 model reads the frame from guest RAM and passes it to QEMU's
   `user` network backend (`net/wasi-user.c.inc`), which calls
   `host_net_send()`.
3. m-browser's network thread ends the guest's TCP connection in smoltcp
   and relays the data through a real host socket.
4. Replies wait in m-browser. On every main-loop iteration QEMU fetches
   them with `host_net_recv()`; the e1000 model writes each into guest RAM
   and raises an interrupt. An idle QEMU sleeps in `host_net_wait()`, which
   returns as soon as a frame arrives.

Details: [09-display-input-network.md](09-display-input-network.md) and the
m-browser repository's docs/10-networking.md.

## 8. Where to go next

1. [03-getting-started.md](03-getting-started.md): build QEMU and boot the
   guest.
2. [04-architecture.md](04-architecture.md): how the parts fit together.
3. [05-repository-tour.md](05-repository-tour.md) and
   [06-patch-series.md](06-patch-series.md): what lives where, and what
   each patch changes in QEMU.
4. [07-tcg-backend.md](07-tcg-backend.md), [08-threads.md](08-threads.md)
   and [09-display-input-network.md](09-display-input-network.md): the main
   subsystems in depth.
5. [10-changing-qemu.md](10-changing-qemu.md) to
   [16-guest-image.md](16-guest-image.md): practical guides, for when you
   need them; [17-history.md](17-history.md) and
   [known-issues.md](known-issues.md) for background.

Keep the [glossary](reference/glossary.md) at hand while reading, and the
m-browser repository's docs/02-concepts.md for the host's side.
