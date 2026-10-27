# Host interface reference

**What this covers.** The exact contract between the QEMU wasm module (both
flavors) and the program that runs it: every import QEMU needs, every export
the host calls, the memory and function table, and the interface of the
small modules QEMU generates for translated code. Read it when you change a
host function on either side, add a new one, or write another host. The host
side is the m-browser repository; its docs/reference/host-imports.md
describes the same functions from there.

## Conventions

- QEMU is a core wasm module for WASI preview 1, not a component. Its
  imports come from three modules: `env` (this project's functions),
  `wasi_snapshot_preview1` (standard WASI) and, in the threaded build,
  `wasi` (`thread-spawn`).
- The `env` functions are plain C `extern` declarations. QEMU is linked
  with `-Wl,--allow-undefined`, which turns every undefined function into
  an import from `env` under its C name.
- A pointer is an i32 byte offset into QEMU's linear memory. The host must
  check bounds itself (m-browser's `with_guest()` and `with_guest_mut()` in
  `src/qemu_jit.rs`) and must copy any data it keeps: QEMU reuses its
  buffers after the call returns.
- Buffers and strings are passed as (pointer, length) pairs. Strings are
  not NUL-terminated.
- `int32_t` and `uint32_t` are both wasm i32; `int64_t` is i64.
- The meaning of a result is per function (see the tables). Handles use 0
  for "none".
- In the threaded build an import can be called from several threads at
  once, each through its own instance and Store (wasmtime's container of
  instances, which frees them only all together), and the memory is shared.
- "Flavor": *single* is `qemu-build/qemu-system-x86_64` (`wasm32-wasip1`),
  *threads* is `qemu-build-threads/qemu-system-x86_64`
  (`wasm32-wasip1-threads`, `WASI_THREADS`).

## Checking a build's interface

From the qemu-wasi root:

```sh
wasm-tools print --skeleton qemu-build/qemu-system-x86_64 | grep -E '\((import|export)'
wasm-tools print --skeleton qemu-build-threads/qemu-system-x86_64 | grep -E '\((import|export)'
```

The import lines show the type index; find the signature with
`grep -E '\(type \(;N;\)'` on the same output. Compare the list before and
after a change: a new `env` import that is not in the tables below is
usually a missing stub or a misspelt function, and the host will refuse to
instantiate the module with "unknown import".

## Imports from `env`

### JIT (both flavors)

QEMU translates guest code in translation blocks (TBs) and turns each into
a small wasm module, which it asks the host to compile and load through
these functions. Declared in `qemu-src/tcg/wasi_jit.h`, called from
`compile_and_cache_tb()` in `qemu-src/tcg/wasm32.c`, implemented by
`add_jit_to_linker()` and `jit_load()` in m-browser's `src/qemu_jit.rs`.
All three have the C prototype

```c
uint32_t f(const uint8_t *wasm_bytes, uint32_t wasm_bytes_len,
           const uint32_t *helper_func_indices, uint32_t helper_func_count);
```

and the wasm type `(i32 i32 i32 i32) -> i32`.

| Import | Returns |
|---|---|
| `wasi_jit_compile` | Compiles the module if the host has no identical one (synchronously), instantiates it, returns its handle; 0 on failure |
| `wasi_jit_load_cached` | The handle if the host already has an identical module compiled; otherwise 0, without compiling |
| `wasi_jit_load_async` | The handle if compiled already; otherwise queues a background compile and returns 0 (QEMU interprets the TB meanwhile and asks again 16 runs later); `WASI_JIT_FAILED` (0xffffffff) once that compile has failed |

- `wasm_bytes` is a complete TB module (see [TB modules](#tb-modules)),
  assembled in a temporary buffer that QEMU frees after the call.
- `helper_func_indices` lists the table indices of the helpers the module
  calls. Current modules call helpers through `env.table` themselves, so
  the host only needs the list for the `helper.N` imports of older builds.
- A *handle* is an index into the function table of the calling instance
  (`__indirect_function_table`) where the host has stored the module's
  `start` function, typically by growing the table by one. QEMU calls it as
  a C function pointer, `uint32_t (*)(struct wasmContext *)`. It is never 0,
  because table index 0 is not a valid function pointer.
- Handles are valid only for the instance (and so the Store) that received
  them. After moving QEMU to a new Store the host must call
  `wasi_qemu_jit_reset()`, or pass `WASI_VCPU_JIT_RESET` to
  `wasi_vcpu_enter()` for a vCPU (virtual CPU) of the threaded build.
- Identical bytes may get the same handle again: m-browser keeps one
  instance per distinct module per Store. QEMU works either way.

Details of QEMU's side: [../07-tcg-backend.md](../07-tcg-backend.md). Of the
host's: the m-browser repository's docs/07-jit-host.md.

### Display (both flavors)

Declared at the top of `display/wasi_gfx_display.c`, implemented by
`add_gfx_to_linker()` in m-browser's `src/qemu_gfx.rs`. See
[../09-display-input-network.md](../09-display-input-network.md).

| Import | C prototype | Wasm type |
|---|---|---|
| `host_gfx_init` | `void host_gfx_init(int32_t width, int32_t height)` | `(i32 i32) -> ()` |
| `host_gfx_resize` | `void host_gfx_resize(int32_t width, int32_t height)` | `(i32 i32) -> ()` |
| `host_gfx_update_framebuffer` | `void host_gfx_update_framebuffer(const uint8_t *data_ptr, int32_t data_len, int32_t width, int32_t height, int32_t stride)` | `(i32 i32 i32 i32 i32) -> ()` |
| `host_gfx_poll_events` | `int32_t host_gfx_poll_events(int32_t *event_buf, int32_t max_events)` | `(i32 i32) -> i32` |

- `host_gfx_init`: set up the window surface for a frame of this size,
  called once when the display is registered. Sizes passed to all three
  frame functions are after QEMU's magnification (`WASI_GFX_SCALE`).
- `host_gfx_resize`: the frame size changed.
- `host_gfx_update_framebuffer`: show a frame of `width` x `height` pixels,
  4 bytes each in the order B, G, R, A (alpha unused), `stride` bytes per
  row, `data_len` bytes in all. QEMU sends a frame when it changed and
  otherwise about once a second.
- `host_gfx_poll_events`: write up to `max_events` input events into
  `event_buf`, three `int32_t` each, `[type, code, value]`, and return how
  many. Events that do not fit stay queued in the host.

| Type | Event | `code` | `value` |
|---|---|---|---|
| 1 | Pointer moved | 0 = x, 1 = y | Position in pixels of the displayed (scaled) frame |
| 2 | Button | QEMU's `InputButton`: 0 left, 1 middle, 2 right, 3 wheel up, 4 wheel down, 5 side, 6 extra | 1 pressed, 0 released |
| 3 | Key | Linux evdev key code | 1 pressed, 0 released |
| 4 | Host window resized | Width | Height (QEMU logs it and ignores it) |
| 5 | Text | Unicode code point typed | Unused |

**Note:** the comment above the declarations in `wasi_gfx_display.c` lists
only types 1 to 4; type 5 is handled by the same function.

### Network (both flavors)

Declared at the top of `qemu-src/net/wasi-user.c.inc` (QEMU's `user`
network backend), implemented by `add_net_to_linker()` in m-browser's
`src/qemu_net.rs`. Only called when the command line has a `user` network.

| Import | C prototype | Wasm type |
|---|---|---|
| `host_net_send` | `int32_t host_net_send(const void *frame, int32_t len)` | `(i32 i32) -> i32` |
| `host_net_recv` | `int32_t host_net_recv(void *buf, int32_t size)` | `(i32 i32) -> i32` |
| `host_net_wait` | `void host_net_wait(int64_t timeout_ns)` | `(i64) -> ()` |
| `host_net_hostfwd` | `int32_t host_net_hostfwd(const char *rule, int32_t len)` | `(i32 i32) -> i32` |

- `host_net_send`: one Ethernet frame from the guest. QEMU ignores the
  result (m-browser returns `len`, or -1 if the pointer is out of bounds);
  like a wire, the host may drop a frame.
- `host_net_recv`: copy the next frame for the guest into `buf` and return
  its length, or return 0 if there is none. Called on every main-loop
  iteration, so it must return at once. (m-browser drops a frame larger
  than `size` and returns 0.)
- `host_net_wait`: sleep up to `timeout_ns` nanoseconds, returning early
  when a frame for the guest arrives or, in the threaded build, when
  `host_main_wake` is called. QEMU sleeps in it while the guest is idle.
- `host_net_hostfwd`: forward a host port into the guest for one
  `hostfwd=` option, `"[tcp]:[hostaddr]:hostport-[guestaddr]:guestport"`.
  Returns 0, or -1 after the host has printed why; QEMU then fails to
  start with "could not set up host forwarding rule".

### Threads (threaded flavor only)

| Import | C prototype | Wasm type | Declared in / implemented in |
|---|---|---|---|
| `env.host_vcpu_run` | `void host_vcpu_run(int32_t cpu_index, uint32_t stack_top, uint32_t tls_base)` | `(i32 i32 i32) -> ()` | `qemu-src/accel/tcg/tcg-accel-ops-mttcg.c` / `QemuThreads::run_vcpu()` in `src/qemu_threads.rs` |
| `env.host_main_wake` | `void host_main_wake(void)` | `() -> ()` | `qemu-src/util/main-loop.c` / `add_net_to_linker()` in `src/qemu_net.rs` |
| `wasi.thread-spawn` | called by wasi-libc's `pthread_create()` | `(i32) -> i32` | wasi-libc / `QemuThreads::spawn()` in `src/qemu_threads.rs` |

- `host_vcpu_run`: called once by each vCPU thread with its index, the top
  of the stack to use and the thread's TLS (thread-local storage) block.
  The host runs that vCPU on the calling OS thread in an instance and Store
  of its own, calling `wasi_vcpu_enter(stack_top, tls_base, flags)` again
  and again until it returns non-zero, and replaces the Store from time to
  time to free compiled TB modules. Returns when the vCPU has been
  unplugged. Details in [../08-threads.md](../08-threads.md).
- `host_main_wake`: ends the main thread's current or next
  `host_net_wait()`. QEMU calls it (`wasi_main_wake()`, from
  `aio_notify()`) from threads other than the main loop's, because the main
  loop sleeps in the host where QEMU's own notifiers cannot reach it. It
  does not interrupt a `nanosleep()`, which the main loop uses instead when
  there is no `user` network or the network card's queue is full.
- `wasi.thread-spawn` (the wasi-threads ABI): start an OS thread that
  instantiates QEMU again, sharing the memory, and calls its
  `wasi_thread_start(tid, start_arg)`. Returns the new thread id (> 0), or
  a negative value on failure.

### Legacy declarations

`qemu-src/tcg/wasi_jit.h` also declares three functions that nothing calls,
so neither current build imports them (checked with the commands above):

| Declaration | What it was for |
|---|---|
| `uint32_t wasi_jit_call(uint32_t module_handle, uint32_t ctx_ptr)` | The host called a TB's `start`; replaced by calling the handle directly through the table |
| `void wasi_jit_drop(uint32_t module_handle)` | Releasing a handle; the host can only free modules by replacing the Store |
| `int wasi_jit_take_pending_exception(void)` | Re-raising an exception that a host trampoline for a helper had swallowed; obsolete since helpers are called through the table |

m-browser still provides all three, for older QEMU builds. The top-level
README.md's import table still lists `wasi_jit_drop` (and does not list
`wasi_jit_load_async` or the threaded build's imports).

## Imports from `wasi_snapshot_preview1`

Implemented by wasmtime-wasi (`wasmtime_wasi::p1::add_to_linker_sync()` in
m-browser's `run_wasm_module()`, `src/wasm.rs`). Current builds import:
`args_get`, `args_sizes_get`, `environ_get`, `environ_sizes_get`,
`clock_time_get`, `fd_close`, `fd_fdstat_get`, `fd_fdstat_set_flags`,
`fd_filestat_get`, `fd_filestat_set_size`, `fd_pread`, `fd_prestat_get`,
`fd_prestat_dir_name`, `fd_pwrite`, `fd_read`, `fd_readdir`, `fd_seek`,
`fd_sync`, `fd_tell`, `fd_write`, `path_create_directory`,
`path_filestat_get`, `path_open`, `path_readlink`, `path_remove_directory`,
`path_unlink_file`, `poll_oneoff`, `proc_exit`, `random_get`, and with
threads also `sched_yield`. The list changes when QEMU starts using another
libc function; it is not a contract to maintain by hand.

The host decides what QEMU sees: m-browser passes QEMU's command line as
the WASI arguments, only the `QEMU_*` and `WASI_*` environment variables,
and the directories it preopens (firmware, disk image). In the threaded
build all threads share one WASI context, and m-browser replaces
`clock_time_get`, `clock_res_get`, `random_get`, `sched_yield` and
`poll_oneoff` (`add_threads_to_linker()`) so that they do not wait for it.
`exit()` in QEMU becomes `proc_exit`, which the host sees as a trap carrying
the exit status.

## Exports

Defined in `qemu-src/system/main.c` unless noted; called by m-browser's
`run_qemu_reactor()` and `migrate_store()` (`src/wasm.rs`) and
`src/qemu_threads.rs`.

| Export | Type | Flavor | When and what |
|---|---|---|---|
| `_initialize` | `() -> ()` | both | wasi-libc's reactor start-up (C constructors). Call once, first, on the main instance; not after a Store move, whose copied memory is already initialized |
| `wasi_qemu_init` | `() -> i32` | both | Reads the WASI arguments and starts the machine (`qemu_init()`). 0 on success, 1 if the arguments cannot be read; bad options make QEMU print why and exit (a `proc_exit` trap) |
| `wasi_qemu_step` | `() -> i32` | both | One main-loop iteration: -1 to continue, otherwise the exit status (>= 0). Single: also runs the vCPU for one time slice (about 30 ms) and pending RCU callbacks (QEMU's deferred frees). Threads: main loop only; on exit it also unplugs the vCPUs so that their threads end |
| `wasi_qemu_jit_reset` | `() -> ()` | both | Forget every TB handle, because the Store holding their instances was replaced |
| `wasi_qemu_quit` | `() -> ()` | both | Request a clean shutdown, as closing the window would: power off, flush and close disk images. Keep calling `wasi_qemu_step` until it returns a status (m-browser gives up after 10 s) |
| `memory` | memory | both | QEMU's linear memory (threads: the imported shared memory, re-exported for the host's WASI functions) |
| `__indirect_function_table` | table | both | QEMU's function table (below) |
| `wasi_vcpu_enter` | `(i32 stack_top, i32 tls_base, i32 flags) -> i32` | threads | Defined in assembly in `qemu-src/tcg/wasm32.c`: sets this instance's stack pointer and TLS base to the vCPU thread's, then runs one iteration of the vCPU loop (`wasi_vcpu_step()` in `tcg-accel-ops-mttcg.c`). Flag 1 (`WASI_VCPU_JIT_RESET`): new Store, forget TB handles. Returns 1 once the vCPU is unplugged, else 0 |
| `wasi_thread_start` | `(i32 tid, i32 start_arg) -> ()` | threads | wasi-libc's thread entry point, called by the host on a new thread's instance |

Sequence in m-browser, single-threaded: instantiate, `_initialize`,
`wasi_qemu_init`, then `wasi_qemu_step` in a loop. Between two steps no
wasm frame is live, so the host may move QEMU to a fresh Store (new
instance, copy the memory, then `wasi_qemu_jit_reset`); see the m-browser
repository's docs/06-qemu-runner.md. Threaded: the host creates the shared
memory first; `wasi_qemu_init` creates the vCPU threads through
`thread-spawn`, each of which ends up in `host_vcpu_run`; the main thread
then only calls `wasi_qemu_step`.

## Memory and table

| | Single | Threads |
|---|---|---|
| Memory | Defined by QEMU and exported as `memory`; 256 pages (16 MiB) initially in current builds, no maximum, grows as QEMU allocates | Imported as `env.memory`: shared, 256 to 65536 pages (64 KiB each, so up to 4 GiB; `--max-memory=4294967296`), created by the host; re-exported as `memory` (`--export-memory`) |
| Table | `__indirect_function_table`, exported (`-Wl,--export-table`) and without a maximum (`-Wl,--growable-table`) | Same, one per instance |

The table holds every function whose address QEMU's C code takes; a C
function pointer is an index into it, fixed when QEMU is linked (8877
entries in the current single-threaded build, 8918 with threads). The host
appends each loaded TB's `start` function, so handles are larger than the
initial size. Every instance of QEMU, including each vCPU's in the threaded
build, has its own table with the same initial entries; TB handles are
therefore per instance.

The engine must support wasm exception handling (QEMU's `setjmp` and
`longjmp` are compiled to it, see `scripts/configure.sh`) and, for the
threaded build, threads and shared memory. m-browser enables these with
`config.wasm_exceptions(true)` and `config.shared_memory(true)`.

## TB modules

Each translated block of guest code is a separate module, generated by
`wasm_finalize_module()` in `qemu-src/tcg/wasm32/tcg-wasm32-helpers.c.inc`
and loaded through the JIT imports above.

| Item | Value |
|---|---|
| Import `env.buffer` | Memory: QEMU's linear memory. Single: minimum 1 page, no maximum. Threads: shared, minimum 1, maximum 65536 |
| Import `env.table` | Table of `funcref`, minimum 1: QEMU's `__indirect_function_table` |
| Export `start` | Function of type `(i32) -> i32`: called with a pointer to QEMU's `struct wasmContext`; returns a value for `cpu_exec()` |
| Anything else | None: no other imports, no globals, no data, no start function |

A host must instantiate the module with exactly these two imports, put
`start` into QEMU's table and return the index as the handle. TB modules
are stateless and position independent, so a host may cache compiled
modules by their bytes and give every TB with identical bytes the same
instance. Modules from older QEMU builds also imported each helper as
`helper.N` (N indexing `helper_func_indices`); m-browser still links those
through `legacy_linker()`. Inspect a module with `wasm-tools print` and
`wasm-tools validate`
([../07-tcg-backend.md](../07-tcg-backend.md#how-to-inspect-generated-code)).
