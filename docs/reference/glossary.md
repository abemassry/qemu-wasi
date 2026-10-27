# Glossary

**What this covers.** Plain-language explanations of the words this
documentation uses: emulation and PC hardware, the guest operating system,
QEMU's internals, WebAssembly, WASI and wasmtime, the build tools,
networking, display and input, and the project's own tools. Each entry
says where the term shows up in this project. Read it whenever a word in
another chapter is unfamiliar; the m-browser repository's documentation
refers here for emulator terms too. Entries are sorted letter by letter,
ignoring case, spaces and punctuation; a "See also" link leads to the
letter of a related entry. QEMU source paths such as `util/main-loop.c`
are relative to `qemu-src/`, and `src/...` paths are in the m-browser
repository. For how the pieces fit together, start with
[../02-concepts.md](../02-concepts.md) and
[../04-architecture.md](../04-architecture.md).

[A](#a) [B](#b) [C](#c) [D](#d) [E](#e) [F](#f) [G](#g) [H](#h) [I](#i) [J](#j) [K](#k) [L](#l) [M](#m) [N](#n) [O](#o) [P](#p) [Q](#q) [R](#r) [S](#s) [T](#t) [U](#u) [V](#v) [W](#w) [X](#x) [Z](#z)

## A

**AioContext**. QEMU's event-loop object: a set of handlers for file
descriptors (open files, sockets), timers and bottom halves, run whenever
the context is polled (`aio_poll()`). The main loop owns two, the main
context and the iohandler context; glib-wasi never dispatches them, so on
WASI `util/main-loop.c` polls both itself on every iteration (patch 0004).
See also [Main loop](#m), [Bottom half](#b).

**Alpine Linux**. A small Linux distribution built on musl libc and
BusyBox, with OpenRC as its service manager and `apk` as its package
manager (`apk update`, `apk add <package>`). The guest runs Alpine 3.23
with Xorg, Fluxbox and Chromium
([../16-guest-image.md](../16-guest-image.md)).

**APIC** (local APIC, local APIC timer, IO-APIC). The x86 interrupt
controllers: each CPU has a local APIC, which receives interrupts for it,
sends IPIs and contains a timer, and the IO-APIC routes device interrupts
to the local APICs. Linux uses each CPU's local APIC timer for its timer
tick; QEMU emulates the local APIC in `hw/intc/apic.c`, where the threaded
build fires the timer on a 30 ms grid (`QEMU_TICK_SLACK_MS`, patch 0008).
See also [Timer tick](#t).

**ARP** (Address Resolution Protocol). How a machine on an Ethernet
network finds the MAC address that belongs to an IPv4 address: it
broadcasts "who has 10.0.2.2?" and the owner answers. m-browser's network
answers for the gateway (10.0.2.2) and the DNS server (10.0.2.3), both as
`52:55:0a:00:02:02`. See also [MAC address](#m).

**Atomic operation**. A memory access, or a combined read and update, that
other CPUs see as indivisible, such as x86's `lock cmpxchg`; locks are
built from them. Wasm has atomic instructions as part of its threads feature,
which the threaded build uses (wasi-libc's mutexes sleep with wasm's
atomic wait and notify). In QEMU code use `qatomic_*()`: glib-wasi's
`g_atomic_*` are plain reads and writes. See also
[CONFIG_ATOMIC64](#c).

## B

**Background compilation**. Compiling hot TB modules on other host
threads while QEMU keeps interpreting them, instead of stopping QEMU for
each compile of about 1 ms. QEMU asks with `wasi_jit_load_async()`, again
every 16 runs until the module is ready, and m-browser compiles on its
`qemu-jit-N` threads (`QEMU_JIT_THREADS`, default a quarter of the host's
CPUs, 1 to 4); this made QEMU about 7% faster, and `QEMU_JIT_ASYNC=0`
turns it off. See also [Hot TB](#h).

**BGRA**. A pixel format: 4 bytes per pixel, in the order blue, green,
red, alpha (the alpha byte is unused here). QEMU's frames reach
`host_gfx_update_framebuffer()` as BGRA rows of `stride` bytes, and
m-browser converts each pixel to softbuffer's `0x00RRGGBB`. See also
[Framebuffer](#f).

**BIOS** (SeaBIOS, firmware). Firmware is the program a computer runs
first at power-on; a PC's is the BIOS, which sets up the hardware and
starts the bootloader from a disk. QEMU's pc machine uses SeaBIOS
(`bios-256k.bin`), an open-source BIOS, plus a VGA BIOS for the graphics
card (`vgabios-*.bin`) and option ROMs (firmware that a device brings
along, such as network-boot code), all found in the `-L` directory:
`/bios` in m-browser. See also [pc-bios](#p).

**Bootloader** (extlinux). The program the BIOS starts from the disk: it
loads the kernel and the initramfs into memory and passes the kernel its
command line. Alpine's disk installs on BIOS PCs use extlinux, from the
Syslinux project, configured in `/boot/extlinux.conf`, which
`update-extlinux` regenerates from `/etc/update-extlinux.conf` (kernel
options in `default_kernel_opts`).

**Bottom half** (BH). A callback that QEMU code schedules to run soon from
an event loop (`qemu_bh_schedule()`, `aio_bh_schedule_oneshot()`) instead
of at once; devices use them to finish work later. On WASI a one-shot
bottom half scheduled from inside a coroutine runs immediately, so that
its result is ready when the coroutine "yields" (`util/async.c`, patch
0004). See also [AioContext](#a), [Coroutine](#c).

**BQL** (big QEMU lock; formerly the iothread lock). QEMU's global mutex
(`bql_lock()`), held by the main loop and by any thread that touches
device state, such as a vCPU thread whose guest accesses a device. In the
single-threaded build every lock is a no-op (wasi-stubs'
`pthread_stubs.c`); in the threaded build it is real, and the main loop
releases it while it sleeps.

**Build flavor** (single-threaded build, multi-threaded or threaded build,
`THREADS=1`, `WASI_THREADS`). The two ways this repository builds QEMU.
`make` targets wasm32-wasip1 and writes `qemu-build/`: no threads at all,
and one vCPU that the main loop runs in time slices. `make THREADS=1`
targets wasm32-wasip1-threads and writes `qemu-build-threads/`, with the C
macro `WASI_THREADS` defined: one host thread per vCPU and a shared memory
(patch 0008, [../08-threads.md](../08-threads.md)); m-browser recognizes
it by its `env.memory` import.

## C

**call_indirect**. The wasm instruction that calls entry N of a function
table, checking while it runs that the function there has exactly the
expected type (a mismatch traps). A C call through a function pointer
compiles to it, as do a TB's calls to helpers (`i32.const <table index>`,
then `call_indirect`) and QEMU's calls into compiled TBs. See also
[Function table](#f).

**Checksum**. A short value computed over data so that the receiver can
detect corruption; a guest silently drops a packet whose checksum is
wrong. IPv4 headers, TCP, UDP and ICMP each carry the 16-bit "Internet
checksum" (RFC 1071), which m-browser's network fills in for every packet
it sends the guest (`checksum()` in `src/qemu_net.rs`, smoltcp for TCP).
It does not check the guest's, as nothing can damage a frame on this
virtual link.

**Chromium flags**. Command-line switches that change how Chromium
behaves, such as `--disable-gpu`; they are given wherever Chromium is
started (a menu entry, a script, a wrapper's configuration file). How the
guest image starts Chromium is described in
[../16-guest-image.md](../16-guest-image.md).

**CONFIG_ATOMIC64**. QEMU's build-time flag saying that the host can
access 8 bytes of memory atomically. Meson's test for it fails on wasm32
because wasm-ld rejects `--start-group`, so `meson.build` forces it on for
wasm32 with atomics (patch 0008); without it, with several vCPUs, every
8-byte guest access that missed the TLB stopped all the other vCPUs. See
also [Exclusive execution](#e).

**configure**. QEMU's top-level script (`qemu-src/configure`), which
checks the compiler and the options and sets up a build directory with
meson. `scripts/configure.sh` runs it with wasi-sdk's clang and options
such as `--target-list=x86_64-softmmu`, `--enable-tcg` and
`--with-coroutine=wasi` ([../05-repository-tour.md](../05-repository-tour.md)).

**Coroutine**. A function that can pause ("yield") and later continue
where it stopped, normally on a stack of its own, taking turns with other
code on one thread; QEMU's block layer (disk images) is written with them.
Here `util/coroutine-wasi.c` (`--with-coroutine=wasi`) runs a coroutine by
simply calling it, without switching stacks, which works because disk I/O
completes synchronously; code that needs a coroutine to really suspend,
such as QMP's dispatcher, cannot work. See also [Thread pool](#t).

**CPU loop** (`cpu_exec()`). QEMU's loop that runs a vCPU
(`accel/tcg/cpu-exec.c`): find or translate the TB for the current guest
pc, run it, and between TBs deliver pending interrupts and exceptions.
Other code makes a vCPU leave generated code ("kicks" it) through a
counter every TB checks at its start (`icount_decr`), and
`cpu_loop_exit()`'s longjmp lands here.

**CPU register**. One of a CPU's few small, fast storage slots that
instructions work on, such as RAX, RSP or RIP (the instruction pointer,
which QEMU calls pc). QEMU keeps the guest's registers in a C structure,
`CPUX86State` (`env` in the code), in its memory; TCG's own registers,
which hold values while a TB runs, are wasm locals in this backend
([../07-tcg-backend.md](../07-tcg-backend.md)).

**CR3**. The x86 control register holding the physical address of the
current top-level page table. The kernel loads it whenever it switches to
another process, as each process has page tables of its own, and that
makes QEMU flush the vCPU's whole softmmu TLB; the monitor's
`info registers` shows it. See also [Page table](#p), [TLB](#t).

**Cranelift**. The compiler wasmtime uses to turn wasm into machine code.
m-browser compiles TB modules with optimizations off (`OptLevel::None`,
about 1 ms each, for hundreds of thousands of them) and QEMU's own module
once with `OptLevel::Speed`, cached as a `.cwasm` file. See also
[Winch](#w).

**Cross-compiling**. Building a program on one kind of machine (here
x86-64 Linux) to run on another (here wasm32 with WASI). Everything QEMU
links must be cross-compiled too, which is why zlib, glib-wasi and
wasi-stubs are built with wasi-sdk, and why `configure.sh` keeps
pkg-config away from the build machine's libraries. See also
[Sysroot](#s).

**Ctrl+Alt+n** (console switching). Hotkeys of the display backend:
Ctrl+Alt+1 to 9 show QEMU's console n-1 and send input to it. With
m-browser's `-monitor vc -serial vc`, 1 is the guest's screen, 2 the QEMU
monitor and 3 the guest's serial port; the guest never sees these keys
(`display/wasi_gfx_display.c`). The guest Linux's own Ctrl+Alt+F1, F2, ...
switch its virtual terminals, which is something else.

## D

**debugfs**. An e2fsprogs tool that reads and changes an ext2/3/4 file
system in a device or image file without mounting it, so it needs no root
rights: `debugfs -R 'cat /etc/fstab' root.img` prints a file, and `-w`
allows writing. It needs the file system itself, so to copy files into or
out of the guest's disk, first convert it to a raw image and cut out the
partition. Not to be confused with the Linux kernel's debugfs
(`/sys/kernel/debug`). See also [Partition](#p).

**DHCP** (Dynamic Host Configuration Protocol). How a machine joining a
network gets its IP address, gateway and DNS server. m-browser's network
has a DHCP server that always gives the guest 10.0.2.15, with gateway
10.0.2.2, DNS server 10.0.2.3 and a 24-hour lease (`dhcp()` in
`src/qemu_net.rs`).

**Display backend**. QEMU's name for a user-interface module that shows
its consoles and passes it input (upstream ones include GTK, SDL and VNC).
`display/` is this project's, the wasi-gfx display: it copies the parts of
the screen that changed, magnifies them (`WASI_GFX_SCALE`), passes frames
to the host and fetches input events from it; `scripts/fix_build_ninja.py`
adds it to QEMU's build
([../09-display-input-network.md](../09-display-input-network.md)).

**DNS** (Domain Name System). Turns names such as example.com into IP
addresses; programs ask a DNS server, usually over UDP port 53. The
guest's DNS server is 10.0.2.3, which m-browser forwards to the first
`nameserver` in the host's `/etc/resolv.conf`.

## E

**e1000**. QEMU's model of the Intel 82540EM gigabit Ethernet card, the
pc machine's default network card and so the one `-nic user` gives the
guest (Linux driver `e1000`). It passes the guest's Ethernet frames to
QEMU's network backend, here the user network provided by the host.

**egui**. A Rust GUI library in "immediate mode": the program describes
the whole interface again on every frame. m-browser's own window (address
bar, page area) is egui, drawn with wgpu; QEMU's screen is shown in a
child window by softbuffer instead.

**Emulator**. A program that imitates a computer in software, so that
software written for that computer runs unchanged. QEMU emulates a whole
x86-64 PC, CPU, memory, disk, graphics and network card included; in this
project the emulator itself is a WebAssembly module. See also
[Virtual machine](#v).

**Epoch interruption**. A wasmtime option: compiled code checks a global
counter, the epoch, at every function entry and loop header, and once a
Store's deadline has passed wasmtime traps or calls a callback.
m-browser's sampling profiler takes its samples in that callback
(`QEMU_PROFILE`); the checks cost speed, so QEMU is compiled without them
otherwise.

**evdev keycode**. The Linux kernel's number for a physical key, from
`linux/input-event-codes.h` (`KEY_A` is 30, `KEY_ENTER` 28), whatever the
keyboard layout. m-browser sends keys to QEMU as evdev codes (event type 3
of `host_gfx_poll_events()`, from `keycode_to_evdev()` in
`src/qemu_keymap.rs`), and the display backend turns them into qcodes. See
also [qcode](#q), [Keysym](#k).

**Event notifier**. QEMU's object for waking a sleeping event loop from
another thread, normally a special file descriptor (an eventfd or a pipe)
that the loop waits on. WASI has neither, so here it is a flag with a
made-up file descriptor number (`util/event_notifier-posix.c`), atomic in
the threaded build. The threaded main loop sleeps inside the host, out of
a notifier's reach, so other threads wake it with the `host_main_wake`
import (`aio_notify()` calls `wasi_main_wake()`).

**Exception** (guest CPU exception). An event the CPU raises because of
the instruction it is running: page fault, division by zero, invalid
opcode, breakpoint. A QEMU helper raises one with `cpu_loop_exit()`, which
longjmps out of the TB back to `cpu_exec()`, where it is delivered to the
guest. Not to be confused with a wasm exception, although in this build
the first is implemented with the second.

**Exclusive execution** (stop-the-world). QEMU's way of doing something
no other vCPU may see half done: `start_exclusive()` waits until every
other vCPU has left generated code and paused, `end_exclusive()` lets them
go. It is used for guest atomic operations the host cannot do atomically
(`cpu_exec_step_atomic()`) and for TB flushes, and is expensive with
several vCPUs. See also [CONFIG_ATOMIC64](#c).

**Export**. Something a wasm module offers the host by name. QEMU exports
`_initialize`, `wasi_qemu_init`, `wasi_qemu_step`, `wasi_qemu_jit_reset`,
`wasi_qemu_quit`, `memory` and `__indirect_function_table`, plus
`wasi_vcpu_enter` and `wasi_thread_start` in the threaded build; a TB
module exports `start` ([host-interface.md](host-interface.md)).

**ext4**. The standard Linux file system, and Alpine's default for disk
installs. The e2fsprogs tools (`mkfs.ext4`, `e2fsck`, `debugfs`) work on
it, also inside image files. See also [Partition](#p).

## F

**FIFO** (named pipe). A special file, made with `mkfifo`, through which
one process writes and another reads. The headless runner reads lines
from one (`QEMU_TYPE_FIFO`): text to type, or the pointer commands
`!move`, `!click`, `!down` and `!up`; a regular file in its place is typed
again and again (the m-browser repository's
docs/11-headless-and-automation.md).

**Fixup commit** (`--fixup`, autosquash). A commit marked as a correction
to an earlier one: `git commit --fixup=<commit>` gives it the subject
`fixup! <original subject>`, and `git rebase -i --autosquash v9.1.0`
moves it next to its target and merges the two. This is how to change one
of the patches in `qemu-src/` before `make export-patches`
([../10-changing-qemu.md](../10-changing-qemu.md)).

**Fluxbox**. A lightweight X window manager: it draws window frames, a
toolbar and a menu that opens on a right-click on the desktop. The guest's
desktop runs it.

**Folded stacks** (flame graph). A text format for profiles, one line per
distinct call stack: the function names from outermost to innermost
separated by `;`, a space, then a number (here microseconds). m-browser's
profiler writes one file per report (`QEMU_PROFILE_DIR`,
`window-NNN.folded`), which tools such as inferno or FlameGraph draw as
flame graphs: each function a bar as wide as its time, stacked on its
caller.

**Fragmentation** (IP fragmentation). Splitting an IP packet larger than
the link's MTU into pieces that the receiver puts back together.
m-browser's network fragments packets of more than 1500 bytes for the
guest, and reassembles the guest's fragments (up to 64 packets at a time,
each dropped if still incomplete after 30 s).

**Framebuffer**. An image of the screen in memory, which the display
hardware shows. The guest draws into the emulated graphics card's
framebuffer (the pc machine's default card is the standard VGA); QEMU
presents it as a `DisplaySurface`, and the display backend copies what
changed and passes frames to `host_gfx_update_framebuffer()`. See also
[BGRA](#b).

**Function table**. A wasm table of references to functions. In C
compiled to wasm, a function pointer is an index into the module's table
(QEMU's `__indirect_function_table`, about 8,900 entries), and calling
through it is a `call_indirect`. QEMU exports its table and links it
growable, so that the host can append each compiled TB's `start`
function; the new index is the TB's JIT handle.

## G

**GETPC** (return address). A macro a helper uses to find its return
address into generated code, from which TCG works out which guest
instruction was running, for example to raise a page fault at the right
one. Wasm code has no addresses, so in this backend, as in TCI, `GETPC()`
reads the thread-local `tci_tb_ptr`, which the TB sets to its own address
plus the call's offset before every helper call.

**git am**. Applies patch files (as `git format-patch` writes them) as
commits, keeping their messages and authors. `scripts/fetch-qemu.sh` uses
it to turn `patches/` into the commits of branch `qemu-wasi` in
`qemu-src/`; if it stops half way, `make reset-qemu` starts over.

**git format-patch**. Writes commits out as patch files, one per commit,
named after their subjects. `make export-patches`
(`scripts/export-patches.sh`) uses it to write the commits since `v9.1.0`
back into `patches/`, with options that keep unchanged patches
byte-identical (`--zero-commit`, `--no-signature`).

**glamor**. Xorg's 2D acceleration, which draws through OpenGL. The guest
has no real GPU, so its OpenGL runs in software on the emulated CPU and
Xorg takes about half an hour to start; turn glamor off in the guest with
`Option "AccelMethod" "none"` in
`/etc/X11/xorg.conf.d/20-modesetting.conf` (top-level README.md).

**GLib**. A general-purpose C utility library from the GNOME project:
data structures, strings, logging, an event loop, threads. QEMU requires
it, and the real one does not build for WASI, hence glib-wasi.

**glib-wasi**. This project's minimal GLib for WASI (`glib-wasi/`), with
only what QEMU uses. Its main loop is a stub that never runs callbacks, so
QEMU's main loop polls its AioContexts itself, and its `g_atomic_*` are
plain reads and writes; it is built once per build flavor
([../05-repository-tour.md](../05-repository-tour.md)).

**Guest**. The emulated machine and everything that runs on it: here an
x86-64 PC running Alpine Linux. "Guest code", "guest memory" and "guest
pc" refer to it; but in wasmtime's terms QEMU itself is m-browser's
guest, so m-browser's `with_guest()` reads QEMU's memory. See also
[Host](#h).

**Guest logger** (`qemu-profile.start`). A shell script that tools/perf
installs in the guest image (as `/etc/local.d/qemu-profile.start`, run by
OpenRC at the end of boot). Every 10 seconds it writes the kernel's
counters and `top`'s output to the serial port, which QEMU saves to a file
on the host; `analyze-serial.py` turns that log into a timeline of the
boot. See [../16-guest-image.md](../16-guest-image.md).

## H

**Headless runner**. m-browser's way of running QEMU without a window:
`m-browser --qemu-headless [--preopen HOST=GUEST]... <module> <args>`.
The screen goes to a PNG file (`QEMU_SNAPSHOT`), typing and clicks come
from `QEMU_TYPE` or a FIFO (`QEMU_TYPE_FIFO`), and `QEMU_QUIT_AFTER` stops
QEMU cleanly; it takes no disk-image lock, so give it a copy of the image
(the m-browser repository's docs/11-headless-and-automation.md).

**Helper** (TCG helper function). A C function in QEMU that generated code
calls for work too complex to emit inline: division, CPUID, x87 and SSE
instructions, interrupts, memory accesses that miss the TLB. This backend
calls helpers with `call_indirect` through QEMU's function table, so a
helper that throws (`cpu_loop_exit()`) unwinds straight through the TB.
See also [GETPC](#g).

**Host**. The real computer and operating system the emulator runs on,
here x86-64 Linux. In this project the word also means the program that
runs QEMU's wasm module and implements its imports, m-browser, as in "host
function" and [host-interface.md](host-interface.md). See also
[Guest](#g).

**Host function** (`func_wrap`). A function the host gives a wasm module
as an import. m-browser's are Rust closures registered with wasmtime's
`Linker::func_wrap()` (module `env`, a name, the closure), which derives
the wasm signature from the closure's types; an optional `Caller` argument
gives access to the Store's data and QEMU's memory. QEMU's are all listed
in [host-interface.md](host-interface.md).

**hostfwd** (port forwarding). A user-network option that forwards a host
port into the guest:
`hostfwd=[tcp]:[hostaddr]:hostport-[guestaddr]:guestport`. For example,
in m-browser's GUI, `QEMU_NIC=user,hostfwd=tcp:127.0.0.1:2222-:22` makes
the guest's ssh server reachable on host port 2222; QEMU passes each rule
to the host (`host_net_hostfwd()`), and only TCP can be forwarded.

**Hot TB** (warm reload). A TB is hot once the interpreter has run it
`QEMU_JIT_HOT` times (default 5000), and QEMU then has the host compile
it. A warm reload comes earlier, at `QEMU_JIT_WARM` runs (default 50):
QEMU asks whether the host still has an identical module compiled, for
example from before a TB flush or a store migration
(`wasi_jit_load_cached()`), and if so uses it at once. See also
[Position independence](#p).

**Hypervisor** (KVM). Software that runs virtual machines by letting guest
code execute directly on the real CPU, helped by the CPU's virtualization
features (Intel VT-x, AMD-V); KVM is Linux's, and QEMU normally uses it
(`-accel kvm`) for near-native speed. A wasm module cannot use one: it can
only run wasm instructions, and WASI has no `ioctl()` to drive `/dev/kvm`.
So this build has only TCG (`-accel tcg`), which is many times slower.

## I

**ICMP** (ping). The Internet Control Message Protocol: error and
diagnostic messages that travel with IP, such as "destination
unreachable", and the echo request and reply that `ping` uses. In the user
network 10.0.2.2 and 10.0.2.3 answer pings themselves; other pings go out
through an unprivileged ICMP socket on the host, which Linux allows only
for groups in `net.ipv4.ping_group_range`.

**icount**. QEMU's instruction-counting mode (`-icount`): guest time
advances with the number of instructions executed rather than with the
host's clock, which makes runs reproducible but slower. This project does
not use it, but its counter, `icount_decr`, is checked at the start of
every TB, and setting its upper half to -1 is how QEMU, and this build's
time slices, make a vCPU leave generated code.

**Import**. Something a wasm module needs from outside, named by a module
and a field. QEMU imports this project's host functions from `env`
(`wasi_jit_*`, `host_gfx_*`, `host_net_*`, ...) and WASI from
`wasi_snapshot_preview1`; the threaded build also imports `env.memory` and
`wasi.thread-spawn`. QEMU is linked with `--allow-undefined`, so a missing
or misspelt C function silently becomes an `env` import, and the host then
refuses to start QEMU with "unknown import".

**init** (`init=/bin/sh`). The first program the kernel starts (process
1, PID 1), which starts everything else; on Alpine it is BusyBox's init,
which runs OpenRC. The kernel option `init=/bin/sh` runs a shell as PID 1
instead: no services, no login, and the root file system as the kernel
mounted it (often read-only), which is handy for repairs and quick
measurements.

**initramfs** (initrd). A small archive of files that the bootloader loads
with the kernel; the kernel unpacks it into memory and runs the init in
it, which loads drivers and finds and mounts the real root file system.
Alpine builds it with `mkinitfs`; QEMU can also load a kernel and an
initramfs itself (`-kernel`, `-initrd`), skipping the bootloader.

**Instance** (wasmtime). A module brought to life: its imports bound, and
its own globals, table and (unless imported) memory created. QEMU runs as
one instance, or in the threaded build as several sharing one memory, and
every TB module m-browser loads becomes an instance in the same Store; TB
modules with identical bytes share one.

**InstancePre** (wasmtime). A module whose imports a Linker has already
resolved, so that it can be instantiated cheaply again and again. The
threaded build makes one before any QEMU code runs and instantiates QEMU
from it on every thread (`QemuThreads::instantiate()` in m-browser).

**Instruction**. One operation of a CPU's machine code: add, load,
compare, jump and so on. x86 instructions are 1 to 15 bytes long, and
most come in several forms (operand sizes, register or memory operands),
which is what the instruction test counts.

**Instruction test** (x86-insn-diff). The differential test in
`tests/x86-insn-diff/` (`make test`): a small bare-metal x86-64 program
runs several hundred instruction forms (351 when last counted; a run
prints 353 lines with its header and closing line) over many inputs and
prints a hash per form, under native QEMU and under the wasm
build (through m-browser's headless runner), and the outputs must match.
`QEMU_JIT_HOT=0` tests compiled code only, `QEMU_JIT_HOT=4000000000` the
interpreter only ([../13-testing.md](../13-testing.md)).

**Interpreter tier**. QEMU's interpreter for TB modules
(`tcg/wasm32/wasi-interp.c.inc`): it decodes the very wasm the JIT would
compile and executes it, so that a new TB runs at once instead of waiting
about 1 ms for a compile. Most TBs run only a few times and never leave
it; those that become hot are compiled. See also [Hot TB](#h).

**Interrupt**. A signal that makes a CPU stop what it is doing and run a
handler, sent by a device (a key press, a network frame, a timer) or by
another CPU. In QEMU a device marks the interrupt as pending on the vCPU
(`interrupt_request`) and kicks it, and the CPU loop delivers it between
TBs, never inside one. See also [IRQ](#i), [APIC](#a).

**IPI** (inter-processor interrupt). An interrupt one CPU sends another
through the local APICs, for example to make it reschedule or flush its
TLB. In the threaded build an IPI from one vCPU to another crosses host
threads.

**IRQ** (interrupt request). A device's interrupt line, known by its
number (`cat /proc/interrupts` in the guest counts them); also used
loosely for "interrupt". In QEMU a device model raises and lowers a
`qemu_irq` (`qemu_set_irq()`), wired to an interrupt controller that
passes it on to a CPU.

## J

**JIT** (just-in-time compilation, JIT handle). Making machine code while
a program runs. Two JITs are stacked here: TCG translates guest x86 code
into wasm TB modules, and m-browser compiles those into host machine code
with Cranelift (`wasi_jit_compile()`, `wasi_jit_load_cached()`,
`wasi_jit_load_async()`). The host returns a JIT handle, the index in
QEMU's function table where it put the TB's `start` function, which QEMU
then calls like any C function pointer
([../07-tcg-backend.md](../07-tcg-backend.md)).

## K

**Kernel**. The core of an operating system, here the guest's Linux: it
manages memory, processes and devices. The bootloader normally loads it
from the disk; QEMU can also be given one directly with `-kernel` (with
`-initrd` and `-append` for the initramfs and the command line), as the
instruction test does with its bare-metal program.

**Keysym** (key symbol). The character or function a key press means once
the keyboard layout is applied (an X11 term: `a`, `A`, `Return`). QEMU's
text consoles take keysyms: the display backend types characters with
`qemu_text_console_put_keysym()` and navigation keys with
`qemu_text_console_put_qcode()`, which looks them up in `qcode_to_keysym`
(`ui/console.c`). See also [evdev keycode](#e).

**kswapd**. The Linux kernel thread that frees memory in the background
when it runs low, by writing pages to swap and dropping caches. A guest
with too little RAM shows it busy in `top`: with 512 MB, starting Chromium
made the guest kernel spend about a sixth of its time reclaiming and
compacting memory, hence m-browser's `-m 1024`.

## L

**LEB128**. A variable-length encoding of integers used throughout the
wasm binary format: 7 bits per byte, the top bit meaning "more bytes
follow". The backend writes values it only learns later (branch targets,
sizes) as 5-byte padded LEB128 placeholders and patches them in place
(`fill_uint32_leb128()`).

**Linear memory**. A wasm module's memory: one flat byte array, grown in
64 KiB pages, into which every pointer is an offset (32 bits in wasm32, so
at most 4 GiB). All of QEMU lives in it: its heap, the TB buffer (256 MiB
with `tb-size=256`) and the guest's RAM (1 GiB with `-m 1024`). Host
functions read and write it by offset (m-browser's `with_guest()`).

**Linker** (wasmtime). wasmtime's table from import names to definitions
(host functions, memories), used to instantiate modules. m-browser fills
one with WASI and its `env` functions (`add_jit_to_linker()`,
`add_gfx_to_linker()`, `add_net_to_linker()`, plus
`add_threads_to_linker()` for the threaded build); TB modules are
instantiated without one, by passing QEMU's memory and table directly,
which is much faster.

## M

**MAC address**. A network card's 6-byte hardware address, written like
`52:54:00:12:34:56` (QEMU's default for the first card); Ethernet frames
are addressed by MAC. In the user network the gateway and the DNS server
answer as `52:55:0a:00:02:02`, and m-browser learns the guest's address
from the frames it sends. See also [ARP](#a).

**Machine type** (`-M`, pc, i440fx). The board QEMU emulates. `pc`, the
default of `qemu-system-x86_64` and an alias of `pc-i440fx-9.1`, is a
1990s-style PC built around Intel's 440FX chipset; `q35` is the newer
alternative. m-browser passes no `-M`, so the guest is a `pc`; the
instruction test asks for one explicitly.

**Main loop**. QEMU's central event loop: it runs timers, bottom halves
and device callbacks, refreshes the display, polls the network, and sleeps
when there is nothing to do. QEMU is a reactor here, so each
`wasi_qemu_step()` runs one iteration (`qemu_main_loop_step()` in
`system/runstate.c`), which in the single-threaded build also runs a vCPU
time slice and pending RCU callbacks. An idle guest makes it sleep up to
10 ms (30 ms with threads), in `host_net_wait()` when there is a user
network.

**m-browser**. The Rust desktop browser (winit, egui, wgpu) that runs
WebAssembly with wasmtime and hosts the QEMU module: it compiles TB
modules, shows the screen (softbuffer), provides the virtual network
(smoltcp, mio) and threads (wasi-threads), and has a headless runner and a
sampling profiler. `make install` copies QEMU into it (`M_BROWSER`,
default `../m-browser`) as `qemu-system-x86_64.wasm`; its own
documentation is in its `docs/` directory.

**Memory barrier** (fence). An instruction that keeps a CPU, or a
compiler, from moving memory accesses across it, such as x86's `mfence`.
TCG's `mb` op becomes wasm's `atomic.fence` in the threaded build and
nothing in the single-threaded one. See also [Memory ordering](#m).

**Memory mapping** (`vm.max_map_count`). A region of a process's address
space, one line of `/proc/self/maps`. Every compiled TB module holds about
two, and Linux caps a process at `vm.max_map_count` (65,530 by default),
past which m-browser aborts with ENOMEM although RAM is free; store
migration and a bounded module cache (18,000 modules) keep the count down,
and `sudo sysctl -w vm.max_map_count=262144` raises the cap.

**Memory ordering** (TSO). The rules for when one CPU's memory writes
become visible to other CPUs, and in which order. x86 is strict ("total
store order": only a later load may overtake an earlier store), ARM much
less so. The threaded build relies on TB code keeping accesses in program
order and on an x86-64 host keeping x86's order, so on other hosts guest
code that depends on it may break (m-browser warns).

**meson**. The build-file generator QEMU uses: `meson.build` files
describe what to build, and meson writes `build.ninja` for ninja to
execute. QEMU's configure runs it and downloads the subprojects QEMU needs
(`dtc`, `keycodemapdb`, `berkeley-softfloat-3`, `berkeley-testfloat-3`);
patches 0001 and 0008 change QEMU's `meson.build`.

**mio**. A Rust library for non-blocking I/O: one thread watches many
sockets and wakes when one of them is ready (epoll on Linux). m-browser's
network thread, `qemu-net`, uses it for all its host sockets.

**MMU** (memory management unit). The part of a CPU that translates the
virtual addresses programs use into physical addresses, by way of page
tables, and enforces access rights, raising page faults. QEMU's softmmu
emulates the guest's MMU in software. See also [TLB](#t).

**Module** (wasmtime). wasmtime's compiled form of a wasm module. It
belongs to an Engine (the compiler and its settings), not to a Store, so
it can be instantiated in any Store: m-browser compiles each distinct TB
module once (`JitCache` in `src/qemu_jit.rs`) and reuses it after store
migrations and on every vCPU.

**MTTCG** (multi-threaded TCG). QEMU's mode with one host thread per vCPU,
so that several run guest code at the same time
(`accel/tcg/tcg-accel-ops-mttcg.c`). The threaded build uses it, with each
vCPU's loop driven by the host through `host_vcpu_run` and
`wasi_vcpu_enter` (patch 0008); the single-threaded build turns it off.
See also [Round-robin TCG](#r).

**MTU** (maximum transmission unit). The largest IP packet a link carries
in one piece: 1500 bytes on Ethernet, and in m-browser's network. Larger
packets are fragmented. See also [Fragmentation](#f).

## N

**NAT** (network address translation). Rewriting addresses so that
machines on a private network reach the outside through one shared
address; replies come back in, but unsolicited connections from outside
do not. User-mode networking behaves like NAT: the guest's 10.0.2.15 never
appears outside, as its traffic leaves through the host's own sockets,
and only hostfwd lets connections in.

**Native QEMU**. QEMU built the usual way for the host. `make native`
builds one from the same patched tree into `qemu-native/`, where most of
the changes do not apply (those that do are listed in
[../06-patch-series.md](../06-patch-series.md)), and the instruction test
uses it as its reference. It needs QEMU's usual build dependencies, such
as GLib's development package.

**ninja**. A small, fast build tool that runs the commands in a
`build.ninja` file, redoing only what changed:
`ninja -C qemu-build qemu-system-x86_64`. `scripts/fix_build_ninja.py`
edits QEMU's `build.ninja` (adding `display/` and link flags); if ninja
regenerates the file after a `meson.build` change, the next `make` applies
the edits again.

## O

**OpenRC**. Alpine's service manager: started by init, it starts the
services of each runlevel (`sysinit`, `boot`, `default`) from the scripts
in `/etc/init.d/`. `rc-update add <service> default` enables a service,
and `rc-service <service> start` starts it now.

**Overlay** (backing file). A qcow2 image that stores only the changes
made on top of another image, its backing file, which QEMU reads but never
writes: `qemu-img create -f qcow2 -b base.qcow2 -F qcow2 overlay.qcow2`.
It gives each experiment a throw-away disk without copying gigabytes, as
long as the base does not change. Inside the WASI sandbox QEMU can only
open the backing file through a preopened directory, so give it a
relative name, which is resolved next to the overlay, or its path inside
the sandbox (tools/perf uses `/out/...`, made with `qemu-img create -u`),
which the host's `qemu-img` then cannot follow.

## P

**Page**. The unit in which memory is mapped and protected: 4 KiB on x86
(with 2 MiB and 1 GiB large pages). Not to be confused with a wasm page,
the 64 KiB unit in which linear memory grows; the memory sizes in
[host-interface.md](host-interface.md) count wasm pages.

**Page table**. The tree of tables (four levels on x86-64) in which an
operating system records, for each process, which physical page backs
each virtual page; the MMU walks it, starting from CR3, when its TLB has
no entry. The guest's page tables are Linux's, in guest RAM, and QEMU's
softmmu walks them on a TLB miss.

**Partition**. A part of a disk that holds one file system or swap space,
listed in the partition table (MBR or GPT) at the start of the disk. Tools
that work on the files inside a raw disk image need to know where the
partition starts; `fdisk -l disk.raw` prints it in 512-byte sectors.

**Patch series**. An ordered list of patches, each a diff with a commit
message, applied one on top of the other. The changes to QEMU are the
series in `patches/`, `0001` to `0008` against tag `v9.1.0`
(`patches/BASE`), kept in `qemu-src/` as one commit per patch; they only
build as a whole ([../06-patch-series.md](../06-patch-series.md)).

**pc-bios**. QEMU's directory of firmware files (`qemu-src/pc-bios/`), and
m-browser's copy of the ones a PC needs (`pc-bios/` in the m-browser
repository: SeaBIOS, VGA BIOSes, option ROMs, keymaps), which m-browser
preopens as `/bios` for `-L /bios`. The wasm build installs no firmware
(`--disable-install-blobs`).

**pixman**. A pixel-manipulation library (fills, compositing, drawing
glyphs) used by QEMU's display code. It does not build for WASI, so patch
0005 adds `include/ui/pixman-minimal.h`, with just enough of it for QEMU's
text consoles.

**pkg-config**. A tool that tells a build where a library's headers and
archives are (`pkg-config --cflags --libs glib-2.0`), from `.pc` files.
`configure.sh` writes `.pc` files that make glib-wasi pass for GLib, and
limits `PKG_CONFIG_LIBDIR` to them and zlib's, so that no library of the
build machine slips into the wasm build.

**Position independence** (TB code). A property of this backend's
generated code: addresses in and near the TB are computed from
`ctx->tb_ptr` while it runs instead of being written into the code, so the
same guest code always produces the same module bytes. That lets
m-browser cache compiled modules by their bytes, reuse them after a TB
flush or a store migration, and share one instance between identical TBs;
never embed an absolute code-buffer address in generated code
([../07-tcg-backend.md](../07-tcg-backend.md)).

**Precompiled module** (`.cwasm`). Machine code that wasmtime compiled and
saved, loadable later without compiling. m-browser compiles QEMU's module
once with optimizations and caches it as `qemu-<hash>.cwasm` in
`$XDG_CACHE_HOME/m-browser/` (else `~/.cache/m-browser/`), so only the
first start after a new QEMU build or wasmtime update is slow. Loading
runs the file's code without checks, so keep the directory private.

**Preopened directory** (preopen). A host directory that the host opens
for a WASI program before it starts, under a name of its choosing; the
program can open no other files. m-browser preopens its `pc-bios/` as
`/bios` and its working directory as `/data` (hence
`-drive file=/data/m.qcow2`), and the headless runner adds more with
`--preopen HOST=GUEST`; paths in QEMU's options and variables, such as
`QEMU_TB_DUMP`, are paths in this sandbox.

## Q

**qcode** (`QKeyCode`). QEMU's own numbering of keys (`Q_KEY_CODE_A`,
...) inside its input layer. `qemu_input_linux_to_qcode()` converts evdev
codes with a table generated during the build (from the keycodemapdb
subproject), and the emulated keyboard turns qcodes into what a real
keyboard would send. See also [evdev keycode](#e).

**qcow2**. QEMU's own disk-image format ("QEMU copy-on-write", version 2):
the file grows only as the guest writes, and it supports backing files,
snapshots and compression. The guest's disk is `m.qcow2` in the m-browser
directory, opened as `-drive file=/data/m.qcow2,format=qcow2`; two QEMUs
must never use one image at once (the GUI locks it, the headless runner
does not).

**QEMU**. An open-source machine emulator and virtualizer;
`qemu-system-x86_64` emulates a PC. This project builds QEMU 9.1.0 for
WASI from its patch series: the patched source is `qemu-src/`, and the
result is `qemu-build/qemu-system-x86_64` (or the same in
`qemu-build-threads/`), a wasm module of about 40 MB.

**qemu-img**. QEMU's disk-image tool: `info` (format, size, backing file),
`create`, `convert` (for example from qcow2 to raw), `resize`, `commit`
(merge an overlay into its base) and `check`. This repository does not
build it (`make native` passes `--disable-tools`), so use your
distribution's package, and never change an image a QEMU is running on
(`qemu-img info -U` can still read it).

**QEMU monitor** (HMP, human monitor protocol). QEMU's command prompt for
inspecting and controlling the machine: `info status`, `info registers`,
`info cpus`, `info network`, `system_powerdown`, `quit`. m-browser starts
QEMU with `-monitor vc`, which makes it text console 2 (Ctrl+Alt+2).

**QMP** (QEMU Machine Protocol). QEMU's JSON interface for programs such
as libvirt. This build does not have it: its dispatcher is a coroutine
that must really suspend, which WASI coroutines cannot (patch 0007). Use
the HMP monitor.

## R

**Raw image**. A disk image that is a plain byte-for-byte copy of a disk,
without metadata: easy for other tools (`fdisk`, `mount`, `debugfs`) but
as large as the disk, unless the file is sparse (unwritten parts then
take no space). `qemu-img convert -f qcow2 -O raw m.qcow2 disk.raw` makes
one, and QEMU opens it with `format=raw`.

**RCU** (read-copy-update). A way to share data between threads in which
readers take no lock: a writer publishes a new copy, and the old one is
freed only once no reader can still be using it (`call_rcu()` defers the
free). The single-threaded build has no `call_rcu` thread, so the main
loop runs those deferred frees itself (`rcu_run_callbacks_inline()`, patch
0004); otherwise old TB hash tables and memory maps would never be freed.

**Reactor** (command module, `_initialize`). The two kinds of WASI
program: a command exports `_start` (like `main()`) and is finished when
it returns; a reactor exports `_initialize`, which the host calls first,
and functions the host then calls whenever it wants. QEMU is linked as a
reactor (`-mexec-model=reactor`, patch 0007) so that m-browser can run one
main-loop iteration per `wasi_qemu_step()` call, with no wasm code running
in between.

**Rebase**. Rewriting a git branch's commits: replaying them onto a newer
base, as when moving the patches to a new QEMU release, or editing them in
place (`git rebase -i`). Because each patch is a commit in `qemu-src/`,
changing an existing patch means a rebase
([../10-changing-qemu.md](../10-changing-qemu.md),
[../11-upgrading-qemu.md](../11-upgrading-qemu.md)). See also
[Fixup commit](#f).

**Root filesystem** (root file system). The file system mounted at `/`,
which holds the operating system. The kernel learns which one it is from
its `root=` option, and usually mounts it with the initramfs's help.

**Round-robin TCG** (RR). QEMU's mode in which one thread runs all the
vCPUs, taking turns (`accel/tcg/tcg-accel-ops-rr.c`). The single-threaded
build uses it without even that thread: the main loop runs the vCPU for
one time slice per iteration (`wasi_rr_cpu_step()`, patch 0003).

## S

**Sampling profiler**. A profiler that looks many times a second at what
a program is doing and adds up where it finds it. m-browser has one for
QEMU (`QEMU_PROFILE=1`, `src/qemu_profile.rs`): every millisecond, epoch
interruption lets it capture QEMU's wasm call stack; it reports the
busiest functions every 30 s and can write folded stacks. It only follows
the single-threaded build ([../15-performance.md](../15-performance.md),
and the m-browser repository's docs/12-profiling.md).

**Screen blanking** (DPMS). After 10 minutes without input the guest's X
server blanks the screen (its screen saver) and can ask the monitor to
power down through DPMS (VESA Display Power Management Signaling); QEMU's
window then just turns black. Moving the mouse or pressing a key brings
the picture back, and `xset s off -dpms` in the guest turns both off.

**Serial console**. The guest's first serial port (COM1, `ttyS0` in
Linux), emulated as a 16550A UART, the classic PC serial chip. With
m-browser's `-serial vc` it is text console 3 (Ctrl+Alt+3), and
`-serial file:/out/serial.log` writes it to a file instead. A Linux guest
prints its kernel messages there when booted with `console=ttyS0`, and can
run a login prompt on it.

**Shared memory**. A linear memory that several instances, on several
threads, use at once (wasm threads; wasmtime's `SharedMemory`). The
threaded build imports its memory as shared (`env.memory`, up to 65,536
pages, 4 GiB), and the instances of every QEMU thread and every TB module
use it.

**smoltcp**. A small TCP/IP stack written in Rust. m-browser ends each of
the guest's TCP connections in a smoltcp socket that poses as the
destination, and relays the data through a real host socket
(`src/qemu_net/tcp.rs`), which is also how QEMU's slirp works.

**SMP** (`-smp`, symmetric multiprocessing). Several identical CPUs
sharing one memory; `-smp N` gives the guest N vCPUs. The single-threaded
build runs one. For the threaded build m-browser adds `-smp` when QEMU's
arguments have none: `QEMU_SMP`, else 4 on hosts with at least 8 CPUs and
2 on smaller ones, at most 8.

**softbuffer**. A Rust library that shows a pixel buffer kept in ordinary
memory in a window, without GPU code. m-browser uses it to show QEMU's
frames in a child window (`src/qemu_gfx.rs`); the headless runner writes
them to a PNG file instead.

**Softmmu** (system emulation). QEMU's mode that emulates a complete
machine, CPU with its MMU, memory and devices, on which a whole operating
system boots, as opposed to user-mode emulation (`qemu-x86_64`), which
runs a single Linux program. The build target `x86_64-softmmu` produces
`qemu-system-x86_64`; the name comes from the software MMU that translates
every guest memory access. See also [TLB](#t).

**Stamp file**. An empty file whose existence and date record that a build
step was done, so that make can skip it. `qemu-src/.git/qemu-wasi-patched`
says the patches are applied; when a patch file is newer, `make` only
warns, and `make reset-qemu` applies the series again (discarding
unexported changes in `qemu-src/`).

**Store** (wasmtime). wasmtime's container for instances, memories and
tables, plus the host's own data (m-browser's `QemuHostState`); one thread
uses it at a time. Nothing in it is freed until the whole Store is
dropped, so every TB instance stays until then. See also
[Store migration](#s).

**Store migration**. m-browser's way of freeing TB instances. Between two
`wasi_qemu_step()` calls, once the Store holds 100,000 TB instances
(`QEMU_MIGRATE_AFTER`) or the process has more than 45,000 memory
mappings, it instantiates QEMU in a fresh Store, copies QEMU's memory
over, calls `wasi_qemu_jit_reset()` and drops the old Store; this only
works because QEMU is a reactor. The threaded build replaces each vCPU's
Store instead, with nothing to copy.

**Structured control flow**. Wasm has no jumps to arbitrary places:
`block`, `loop` and `if` nest, and a branch can only leave an enclosing
block or restart a loop. So TCG's labels and branches have to be rebuilt:
this backend numbers the code between labels as blocks inside one loop
and branches by setting a block index (`BLOCK_PTR`)
([../07-tcg-backend.md](../07-tcg-backend.md)).

**Swap**. Disk space the kernel uses as overflow for RAM, writing out
memory that has not been used for a while and reading it back when
needed. Disk access is slow under emulation, so a guest that swaps a lot
crawls and can lose keystrokes; that is why m-browser gives it 1 GiB
(`-m 1024`). See also [kswapd](#k).

**Sysroot**. The directory holding the target's C headers and libraries,
used instead of the build machine's `/usr/include` and `/usr/lib`;
wasi-sdk's is `share/wasi-sysroot/`, with wasi-libc and `libsetjmp.a`.
wasi-stubs' headers come before it on QEMU's include path.

## T

**TB buffer** (code buffer, `code_gen_buffer`, `tb-size`). The memory
where TCG keeps translated code and its metadata, sized with
`-accel tcg,tb-size=N` (in MiB); m-browser passes 256, as with 64 it
filled up every few seconds while the desktop started. In this build it
is ordinary memory inside QEMU's linear memory and holds each TB's wasm
bytes, not executable code. See also [TB flush](#t).

**TB chaining** (`exit_tb`, `goto_tb`, `goto_ptr`). Going from one TB
straight into the next without returning to the CPU loop. A TB ends with
`exit_tb` (return to `cpu_exec()`), `goto_tb` (continue into a successor
TB, once `cpu_exec()` has linked the two) or `goto_ptr` (continue into the
TB a lookup found for the new guest pc). In this backend a TB "jumps" by
storing the next TB's address in `ctx->tb_ptr` and returning, and
`tcg_qemu_tb_exec()` keeps calling TBs until that address is 0.

**TB flush**. Throwing away every translation at once when the TB buffer
is full; code is then translated again as it runs. Here each flush also
discards the interpreter's decoded programs and the TBs' JIT handles and
prints `[qemu-wasi] TB buffer flush #n`; m-browser's module cache and warm
reloads soon make hot code fast again, but frequent flushes mean
`tb-size` is too small.

**TCG** (Tiny Code Generator). QEMU's dynamic binary translator: while the
guest runs, it translates guest machine code, one translation block at a
time, into code the host can run, by way of a machine-independent
intermediate form (TCG ops). It is the only accelerator in this build
(`-accel tcg`); see [../07-tcg-backend.md](../07-tcg-backend.md).

**TCG backend** (host side, `tcg/wasm32`). The part of TCG that turns TCG
ops into host code, one per host CPU under `qemu-src/tcg/`. This project
adds `tcg/wasm32`, which emits each TB as a small wasm module (patch
0002). It began as Kohei Tokunaga's backend for Emscripten (a toolchain
that compiles C to wasm for web browsers), whose code paths are still in
the files but not maintained.

**TCG frontend** (`target/i386`). The part of TCG that decodes guest
instructions and emits TCG ops for them, one per guest architecture; for
x86 it is in `qemu-src/target/i386/tcg/`. This project does not change
it.

**TCG op**. One instruction of TCG's intermediate language, such as
`add_i64`, `brcond_i32` (a conditional branch), `qemu_ld_a64_i64` (a guest
memory load), `call` or `goto_tb`. The frontend produces them, an
optimizer simplifies them and the backend turns each into host code;
`-d op_opt` logs them for every TB.

**TCI** (Tiny Code Interpreter). QEMU's portable TCG backend
(`tcg/tci/`), which interprets TCG ops instead of generating host code.
The wasm backend copies its structure (`tcg_qemu_tb_exec()` written in C,
`GETPC()` read from `tci_tb_ptr`); it is not this backend's interpreter
tier, which interprets the generated wasm.

**TCP** (Transmission Control Protocol). Reliable, ordered byte streams
over connections (opened with SYN, closed with FIN, aborted with RST), as
used by the web, ssh and apk. m-browser ends every guest connection in
smoltcp and relays its data through a host TCP socket to the real
destination; a refused connection gets a reset and an unreachable one an
ICMP error, as on a real network.

**Thread pool**. QEMU's worker threads for blocking jobs, such as reads
and writes of a disk image (`util/thread-pool.c`). On WASI a job
submitted from a coroutine, which is how disk I/O arrives, runs at once
on the calling thread instead (patch 0004), because the coroutine could
not wait for a worker.

**Timer tick** (HZ). The periodic timer interrupt a kernel uses to keep
time and to switch between processes; Linux ticks up to HZ times a second
(a kernel build setting, commonly 100 to 1000) and skips ticks on idle
CPUs. Under emulation each tick costs a busy guest thousands of
instructions, about half its time at 1000 a second, so the
single-threaded build only lets ticks in between time slices and the
threaded build fires the local APIC timer on a 30 ms grid
(`QEMU_TICK_SLACK_MS`); the guest just sees late ticks.

**Time slice**. In the single-threaded build, how long the vCPU runs
before it returns to the main loop: `QEMU_TB_SLICE_MS`, default 30 ms,
checked every 1024 TBs (`QEMU_TB_SLICE` counts TBs instead). A busy
guest's timer tick is due at nearly every return and costs it a couple of
thousand TBs, so short slices waste guest time, while long ones delay
devices, the display and input.

**TLB** (translation lookaside buffer, softmmu TLB). A CPU's cache of
recent virtual-to-physical page translations. QEMU keeps a software TLB
per vCPU that maps guest virtual pages to host addresses: generated code
checks it inline (the fast path) and calls a helper on a miss or for
device memory (the slow path), which walks the guest's page tables.
Writing CR3 empties it.

**tools/perf**. This repository's scripts for measuring QEMU with the real
guest: boot timing (when Xorg, Fluxbox and Chromium appear), click latency
(how long Chromium takes to open its menu after a click) and a guest
benchmark. They run m-browser's headless runner on throw-away overlays of
a prepared copy of the disk image. See
[../../tools/perf/README.md](../../tools/perf/README.md) and
[../15-performance.md](../15-performance.md).

**Translation block** (TB, TB module). A run of guest instructions, at
most 512 and ending at a jump or similar, that TCG translates as one unit
and keeps. In this project each TB becomes a small wasm module, a TB
module, whose one function `start(ctx)` uses QEMU's memory and function
table (imported as `env.buffer` and `env.table`); it is interpreted at
first and compiled by the host once hot. See also [TB chaining](#t),
[JIT](#j).

**Trap**. WebAssembly's crash: an error that stops the running code, such
as an out-of-bounds memory access, `unreachable` (which `abort()` and
failed assertions execute) or a `call_indirect` type mismatch. wasmtime
hands it to m-browser as an error with a wasm backtrace (`step failed:`),
and in the threaded build a trap on a vCPU or other QEMU thread ends the
whole process. QEMU's `exit()` arrives the same way, as an error carrying
the exit status.

## U

**UDP** (User Datagram Protocol). Single messages (datagrams), without
connections or delivery guarantees, used for example by DNS and DHCP.
m-browser relays the guest's UDP through host UDP sockets, one per
conversation, closed after 60 s of silence (10 s for DNS); UDP ports
cannot be forwarded into the guest.

**usb-tablet**. An emulated USB pointing device that reports absolute
positions, like a graphics tablet, so that the guest's pointer follows the
host's exactly; a mouse only reports movements. m-browser adds
`-device usb-tablet` (on an xhci controller); without it, pointer
positions are dropped.

**User-mode networking** (slirp, `-nic user`). QEMU's network that needs
no host configuration or privileges: the guest gets a private network
(10.0.2.15 by DHCP, gateway 10.0.2.2, DNS server 10.0.2.3), and its
traffic leaves through ordinary host sockets. Normally the libslirp
library provides it; here QEMU passes Ethernet frames to m-browser
(`host_net_*`, patch 0006), which emulates the same network, without IPv6,
with smoltcp and mio (the m-browser repository's docs/10-networking.md).

## V

**vCPU** (virtual CPU). One of the guest's emulated processors, QEMU's
`CPUState`. The single-threaded build has one, which the main loop runs in
time slices; the threaded build runs each on a host thread of its own, up
to 8 (`WASI_JIT_SLOTS`). See also [SMP](#s), [MTTCG](#m).

**Virtual console** (vc). A text terminal that QEMU draws itself, made
with the `vc` character device: m-browser's `-monitor vc -serial vc` turn
the monitor and the guest's serial port into text consoles 2 and 3, of
80x24 characters. They draw with pixman functions, hence patch 0005. Not
to be confused with the guest Linux's own virtual terminals (`tty1`,
`tty2`, ...).

**Virtual machine** (VM). A computer that exists only in software: the
emulated hardware plus the operating system and programs running on it.
Here, QEMU running the Alpine disk image.

## W

**WASI** (WebAssembly System Interface). A standard set of imports that
give wasm programs outside web browsers files, clocks, random numbers,
arguments, environment variables and exit, while keeping them in their
sandbox: a WASI program sees only the directories the host preopens. It
offers no way to compile code, open network connections or draw on a
screen, and threads only through wasi-threads, which is why this project
adds host functions of its own ([host-interface.md](host-interface.md)).

**wasi-gfx**. A set of proposed WASI interfaces for graphics (WebGPU,
windows and surfaces, graphics contexts) for wasm components, and the
project that implements them. m-browser runs wasi-gfx demos with its
runtime crates and shows QEMU's screen on the wasi-gfx surface of a child
window, but QEMU itself imports no wasi-gfx interface: its display
backend, named `wasi_gfx_*` after the project, passes frames through the
`env.host_gfx_*` imports.

**wasi-libc**. The C standard library for WASI, based on musl, part of
wasi-sdk's sysroot. It implements POSIX functions on top of WASI, and
emulates signals, process clocks and `mmap()` on request
(`-D_WASI_EMULATED_*`, `-lwasi-emulated-*`); what it still lacks comes
from wasi-stubs.

**WASI preview 1** (`wasi_snapshot_preview1`). The older, function-based
version of WASI: plain functions imported from the module
`wasi_snapshot_preview1` (`fd_read`, `path_open`, `clock_time_get`,
`poll_oneoff`, `proc_exit`, ...), which wasi-libc calls. QEMU uses it, as
implemented in m-browser by the wasmtime-wasi crate; the newer preview 2
builds on the component model, and m-browser uses it only for wasi-gfx
components.

**wasi-sdk**. The WASI toolchain: clang and the LLVM tools set up for
wasm, plus a sysroot with wasi-libc. The build expects it in
`/opt/wasi-sdk` (or `WASI_SDK`), tested with wasi-sdk 29, and compiles
QEMU, zlib, glib-wasi and wasi-stubs with it.

**wasi-stubs**. This project's stand-ins for what wasi-libc lacks
(`wasi-stubs/`): pthreads that do nothing, for the single-threaded build;
BSD sockets that fail with ENOSYS, as the network goes through the host;
`fork()` and `exec()` that fail; empty user and group databases; terminal
definitions. Its headers come before the sysroot's on QEMU's include path
([../05-repository-tour.md](../05-repository-tour.md)).

**wasi-threads** (`thread-spawn`, `wasi_thread_start`). The WebAssembly
convention for threads: each thread is a separate instance of the same
module, all sharing one memory. `pthread_create()` calls the import
`wasi.thread-spawn(start_arg)`; the host starts an OS thread, instantiates
the module again and calls its export `wasi_thread_start(tid, start_arg)`,
which sets the new instance's stack pointer and thread-local storage. The
threaded build uses it (`QemuThreads::spawn()` in m-browser), and a trap
or `exit()` on any thread but the main one ends the whole process.

**wasm32-wasip1** (wasm32-wasip1-threads). Compiler target names: wasm
with 32-bit pointers and WASI preview 1; the `-threads` variant adds
atomics, shared memory and wasi-threads. wasi-sdk has a clang for each
(`wasm32-wasip1-clang`, `wasm32-wasip1-threads-clang`): the
single-threaded build uses the first, `THREADS=1` the second.

**Wasm component** (component model). A newer packaging of WebAssembly:
core modules wrapped behind typed interfaces described in WIT, using WASI
preview 2. QEMU is not a component; the wasi-gfx demos m-browser runs
are, and m-browser tells the two kinds apart by their first bytes.

**Wasm exception handling** (setjmp/longjmp, siglongjmp). A wasm feature
for unwinding the stack to a handler (`throw`, `try_table`). C's `setjmp`
and `longjmp`, and the `sigsetjmp`/`siglongjmp` variants QEMU uses, which
`osdep.h` maps to them, are compiled to it (`-mllvm -wasm-enable-sjlj` and
related flags in `scripts/configure.sh`); this is how `cpu_loop_exit()`
gets from a helper, through TB code, back to `cpu_exec()`. Exceptions
cannot cross host (Rust) frames, and the engine must enable them
(`wasm_exceptions(true)` in m-browser).

**wasm-ld**. LLVM's linker for WebAssembly, run by wasi-sdk's clang.
`scripts/fix_build_ninja.py` adds QEMU's important link options:
`--allow-undefined` (undefined functions become `env` imports),
`--export-table` and `--growable-table` (the host appends TBs), an 8 MiB
stack, and for threads `--import-memory`, `--export-memory` and
`--max-memory=4294967296`. It rejects `--start-group`, which meson emits
and the script removes.

**Wasm module** (core module). The basic unit of WebAssembly: functions,
one linear memory, tables, globals, imports and exports, in a `.wasm`
file. QEMU is one core module of about 40 MB (`qemu-system-x86_64`,
installed as `qemu-system-x86_64.wasm`), and every TB becomes a tiny
module of its own.

**wasmtime**. The WebAssembly runtime, a Rust library from the Bytecode
Alliance, that m-browser embeds (version 41): it compiles modules with
Cranelift and runs them, and its wasmtime-wasi crate implements WASI. See
also [Store](#s), [Instance](#i), [Module](#m), [Linker](#l).

**wasm-tools**. A command-line tool for WebAssembly files:
`wasm-tools print` shows a module as text (with `--skeleton`, without
function bodies), and `wasm-tools validate` checks it. Use it to compare
QEMU's imports with [host-interface.md](host-interface.md) and to read
dumped TB modules (`QEMU_TB_DUMP`, `QEMU_JIT_DUMP`);
`cargo install wasm-tools` installs it.

**WebAssembly** (wasm). A portable binary instruction format: a compiler
(clang, here) produces a module, and a runtime (wasmtime) compiles it to
the real CPU's code and runs it in a sandbox, where it can only touch its
own memory and the functions the host gives it. QEMU is compiled to
32-bit wasm (wasm32).

**wgpu**. A portable Rust graphics API over Vulkan, Metal, DirectX and
OpenGL, implementing WebGPU. m-browser draws its egui interface with it,
and wasi-gfx components reach it through wasi-webgpu; QEMU's screen does
not use it.

**Winch**. wasmtime's baseline compiler, which compiles faster than
Cranelift but produces slower code. m-browser does not use it: TB modules
are compiled by Cranelift with optimizations off.

**winit**. A Rust library that creates native windows and delivers their
events (keys, mouse, resizes). m-browser's event loop is winit's; keyboard
and mouse events for QEMU come from it (`App::window_event()` in
`src/app.rs`) and wait in a queue for `host_gfx_poll_events()`.

## X

**X** (X11, Xorg). The X Window System, the classic display server on
Linux: it owns the screen, keyboard and mouse and draws windows for
programs; Xorg is its usual implementation. In the guest Xorg drives the
emulated graphics card with the `modesetting` driver and reads its
configuration from `/etc/X11/xorg.conf.d/`. See also [glamor](#g).

**x86-64** (AMD64, x64). The 64-bit version of the x86 processor
architecture: the guest's, and the development host's. The threaded
build's memory ordering is only right on x86-64 hosts.

**xhci** (`qemu-xhci`). An emulated USB 3 host controller, the bus the
usb-tablet plugs into. m-browser adds `-device qemu-xhci`; unlike the
older UHCI controller, it needs no 1 kHz polling timer.

## Z

**zlib**. The standard compression library (deflate). QEMU requires it,
for compressed qcow2 clusters among other things; `scripts/build-zlib.sh`
builds version 1.3.1 for each build flavor into `zlib-wasi/` and
`zlib-wasi-threads/`.
