# The WebAssembly TCG backend

**What this covers.** How QEMU's code generator, TCG, produces WebAssembly:
how each translation block becomes a small wasm module, how that module is
run (first by an interpreter, later compiled by the host), and every
convention the generated code relies on. Read it before changing anything
in `tcg/wasm32*` or `tcg/wasi_jit.h`, when a guest instruction misbehaves
only in the wasm build, and before porting the backend to a newer QEMU: the
checklists near the end are written for that. All paths are in `qemu-src/`.

## A primer on TCG

### Dynamic translation

QEMU emulates a guest CPU (here x86-64; each emulated processor is a
*vCPU*, and a guest can have several) by translating guest machine code
into host code while the guest runs. The translator is TCG, the Tiny Code
Generator. It works in three stages:

```text
guest code --front end--> TCG ops --optimizer--> TCG ops --backend--> host code
           target/i386/tcg/        tcg/optimize.c          tcg/<host>/
```

TCG ops are a small machine-independent instruction set ("add these two
values", "load 4 bytes", "branch if less", "call this C function"). The
front end knows the guest, the backend knows the host. This document is
about the backend for WebAssembly hosts, `tcg/wasm32`.

### Translation blocks

A translation block (TB) is a run of guest instructions, at most 512,
ending at a jump or similar, translated as one unit. Its generated code
lives in the code buffer (size set by `-accel tcg,tb-size=N`, in MiB),
next to its `struct TranslationBlock`. When the buffer is full, TCG flushes
it: all translations are thrown away and code is retranslated as needed.
`cpu_exec()` (`accel/tcg/cpu-exec.c`) is the loop that finds or translates
the TB for the current guest program counter, runs it, and handles what
comes out (interrupts, exceptions, requests to stop).

### Guest memory: softmmu and the TLB

The guest uses virtual addresses mapped by its own page tables. QEMU's
*softmmu* translates every guest access: a software TLB (translation
lookaside buffer, a cache of recent page translations kept in the CPU
state) maps a guest page to a host address. Generated code inlines the TLB
lookup (the *fast path*) and calls a C function on a miss or for device
memory (the *slow path*), which walks the page tables, emulates the device
or raises a guest page fault.

### Helpers

Helpers are QEMU C functions that generated code calls for anything
complicated: division, CPUID, x87 and SSE instructions, interrupts. A
helper may *throw*: `cpu_loop_exit()` does `siglongjmp()` back to
`cpu_exec()`, abandoning the TB (a guest exception, for example). To know
which guest instruction was running, helpers use `GETPC()`, the "return
address" into the generated code.

### Leaving and chaining TBs

A TB ends with `exit_tb` (return to `cpu_exec()` with a value naming this
TB and the exit taken), `goto_tb` (jump straight to the next TB once
`cpu_exec()` has linked the two, which is called *chaining*; unlinked, fall
through to an `exit_tb`) or `goto_ptr` (jump to the TB that
`helper_lookup_tb_ptr()` found for the new guest pc, or return to
`cpu_exec()` if there is none).

### Why WebAssembly is an unusual host

1. **No jumps to addresses.** Wasm control flow is structured: `block`,
   `loop` and `if` nest, and a branch can only leave an enclosing construct
   (or restart a loop), named by its nesting depth. TCG's labels and
   branches have to be rebuilt from that.
2. **Code is not data.** A wasm program cannot execute bytes it writes into
   memory. New code must become a new *module* (a unit of compiled wasm)
   compiled by the engine. WASI has no API for that, so the host provides
   one (`wasi_jit_*`, see
   [reference/host-interface.md](reference/host-interface.md)). Compiling
   costs about a millisecond, hence the interpreter tier.
3. **No registers.** Values live on an operand stack, in *locals*
   (per-call variables the engine can keep in machine registers) or in
   *globals* (per-module variables, which live in memory).
4. **Calls between modules** go through imports or a *table* of function
   references; `call_indirect` calls entry N of a table and traps unless
   the callee has exactly the expected type. In C compiled to wasm, a
   function pointer is an index into the module's table.
5. **Exceptions.** In this build `setjmp`/`longjmp` are wasm exceptions.
   They unwind through wasm frames, but not through host (Rust) frames.

An *instance* is a module with its imports bound. wasmtime, m-browser's
engine, keeps instances in a *Store* and frees them only when the Store is
dropped (see the m-browser repository's docs/07-jit-host.md). Cranelift is
wasmtime's compiler.

## Where the code is

| File | Contents |
|---|---|
| `tcg/wasm32/tcg-target.h` | Capabilities (`TCG_TARGET_HAS_*`), the 24 registers, call ABI, memory order |
| `tcg/wasm32/tcg-target.c.inc` | The code generator: one emitter per TCG op |
| `tcg/wasm32/tcg-wasm32-helpers.c.inc` | Per-TB module state, labels, `wasm_finalize_module()`; included by `tcg/tcg.c` |
| `tcg/wasm32/tcg-target-con-set.h`, `-con-str.h`, `-reg-bits.h` | Operand constraint sets, the one register class `r`, `TCG_TARGET_REG_BITS` 64 |
| `tcg/wasm32/wasi-interp.c.inc` | The interpreter tier; included by `tcg/wasm32.c` |
| `tcg/wasm32/wasi-interp-calls.c.inc` | Generated helper-call dispatcher for the interpreter |
| `tcg/wasm32/wasi_platform.h` | `WASI_TB_YIELD_SLICE`, declarations |
| `tcg/wasm32.c` | `tcg_qemu_tb_exec()`: tiers, JIT handles, time slices, profile; Emscripten loader |
| `tcg/wasm32.h` | `struct wasmContext`, per-TB slot layout (`WASI_SLOTS`) |
| `tcg/wasi_jit.h` | The host's JIT functions |

The backend arrives with patch 0002; patch 0008 adds the threaded build's
changes ([06-patch-series.md](06-patch-series.md)).

## How it came to be

The backend began as Kohei Tokunaga's Emscripten backend for QEMU (commit
bf5378b6, "TCG: add wasm backend", linked from the top-level README.md),
which runs QEMU in web browsers and compiles TB modules through JavaScript.
It was ported to WASI, with the Emscripten code paths kept
(`#ifdef EMSCRIPTEN`, `#ifndef WASI_PLATFORM`).

Under Emscripten, coroutines can unwind out of a running TB and re-enter
it later, so TCG's registers were module globals and the code had resume
points: the TB was split into blocks at every helper call and memory
access, with flags in the context (`DONE_FLAG`, `UNWINDING`, `do_init`)
telling a re-entered TB where to continue. On WASI a TB always runs from
its start to an exit: a helper either returns or throws through wasm
exceptions. A later change therefore made the registers locals on WASI and
removed the resume machinery, which made guest code about 22% faster
overall (the desktop appeared 25% sooner). In this repository's history,
which starts after the interpreter tier and position independence existed,
that change came together with background compilation (commit "run qemu
faster"), and the threaded build after it. The bugs fixed along the way are
listed in [Things a porter must preserve](#things-a-porter-must-preserve).

## The life of a TB

```text
guest code at pc
   | tb_gen_code(): front end -> TCG ops -> optimizer
   v
tcg_gen_code() + tcg-target.c.inc: each op becomes wasm instructions
   | wasm_finalize_module(): module header, helper list, state slots
   v
TB in the code buffer: wasm bytes + metadata (position independent)
   | tcg_qemu_tb_exec() on every run:
   v
runs 1..: interpreted (decoded once by interp_decode, run by interp_exec)
   | run 50 (QEMU_JIT_WARM): host still has this exact module? use it
   | run 5000 (QEMU_JIT_HOT): host compiles it in the background
   v
host (m-browser): Cranelift compiles header+body, instantiates it with
   env.buffer = QEMU's memory and env.table = QEMU's function table,
   appends its "start" function to QEMU's table, returns the index
   v
later runs: ((uint32_t (*)(struct wasmContext *))handle)(&ctx)
   = call_indirect into the TB's module, with no host frame in between
```

## The code-buffer layout

`wasm_finalize_module()` builds this for every TB. The body is emitted
first, while TCG generates code; the header can only be written afterwards
because it contains the helper types and the body's size.

```text
                 struct TranslationBlock  (tcg_tb_alloc(), before the code)
tb_ptr ------->  u32 export_vec_offset     filled in last
(tb->tc.ptr)     u32 code_size
tb_ptr + 8 --->  body: the wasm instructions of all ops, then the
                 epilogue "end end i32.const 0 end"         (code_size bytes)
                 u32 header_size
                 header: magic, version, type, import, function, export
                 sections, code-section header, local declarations, prologue
                 u32 import_vec_size  (4 * number of helpers)
                 u32 table index of each helper, in type order
                 0-3 bytes of padding (4-byte alignment)
tb_ptr + export_vec_offset -> u32 export_vec_size (4 * WASI_SLOTS)
                 u32 handle[WASI_JIT_SLOTS]  one per vCPU (1, or 8 with threads)
                 u32 run count               WASI_SLOT_COUNT
                 u32 decoded program         WASI_SLOT_PROG
                 (TCG's search data for restoring guest state follows)
```

The module handed to the host is the header followed by the body,
concatenated by `compile_and_cache_tb()`. `tb_handle_slot()` finds the
slots in O(1). Because the slots live inside the TB, a flush that reuses
the buffer starts every new TB with zeroed state.

## The TB module

| Section | Contents |
|---|---|
| Type | Type 0 is `(i32) -> i32`, the TB function; types 1..N are the helpers', in order of first use |
| Import | `env.buffer`: memory, minimum 1 page, no maximum; with `WASI_THREADS` shared, maximum 65536 pages. `env.table`: funcref table, minimum 1 |
| Function | One function, type 0 |
| Global | Emscripten only: the 25 registers |
| Export | `start` = function 0 |
| Code | Local declarations, prologue, body |

Helpers are deliberately not imported as functions: a TB calls them with
`call_indirect` through the imported table, using their C function pointers
(table indices) as constants. Calls stay wasm-to-wasm, and a helper's
exception unwinds straight through the TB.

The function's locals (indices are emitted as single bytes):

| Index | Name in the code | Type | Use |
|---|---|---|---|
| 0 | `CTX_IDX` | i32 | The parameter: `struct wasmContext *` |
| 1 | `TMP32_LOCAL_ENV_IDX` | i32 | Cached env pointer |
| 2 | `TMP32_LOCAL_0_IDX` | i32 | Scratch: TLB entry, `goto_tb` target, byte swaps |
| 3-7 | `TMP64_0_IDX`..`TMP64_4_IDX` | i64 | Scratch |
| 8-23 | R0-R15 | i64 | TCG registers; R13 `TCG_REG_TMP`, R14 env (`TCG_AREG0`), R15 `TCG_REG_CALL_STACK` |
| 24-31 | A0-A7 | i64 | Helper arguments and results |
| 32 | `BLOCK_PTR` | i64 | Dispatch target (below) |

The context `ctx` is a thread-local `struct wasmContext` (`tcg/wasm32.h`);
generated code uses it through these offsets:

| Offset | Macro | Field |
|---|---|---|
| 0 | `ENV_OFF` | `env`, the guest CPU state (`CPUArchState *`) |
| 4 | `STACK_OFF` | `stack`: 2 KiB per thread, TCG's call-argument area and spill frame |
| 8 | `TB_PTR_OFF` | `tb_ptr`: code address of the running TB; the next TB, or 0, on return |
| 12 | `HELPER_RET_TB_PTR_OFF` | `tci_tb_ptr`: address of the thread-local `tci_tb_ptr` (`GETPC()`) |
| 24 | `STACK128_OFF` | `stack128`: 2 KiB per thread for Int128 helper arguments |
| 16, 20, 28 | `DO_INIT_OFF`, `DONE_FLAG_OFF`, `UNWINDING_OFF` | Emscripten only |

The prologue (WASI) sets R14 from `ctx->env` and R15 from `ctx->stack`,
then opens the dispatch loop. Wasm zeroes locals on entry, so `BLOCK_PTR`
starts at 0.

## The generated function: block dispatch

TCG labels (branch targets inside a TB) become numbered *blocks*. The code
from the TB's start to the first label is block 0; each label starts the
next block. Each block is an `if` that runs only when `BLOCK_PTR <= index`,
inside one `loop`:

```text
func start(ctx: i32) -> i32
  R14 = ctx->env; R15 = ctx->stack
  loop                                 ;; "br 1" from a block restarts it
    if (BLOCK_PTR <= 0)                ;; block 0: the TB's start
      ...ops...
      forward branch to L1:   BLOCK_PTR = 2; br 0  (leave this if)
    end
    if (BLOCK_PTR <= 1)                ;; block 1: label L0
      ...ops...
      backward branch to L0:  BLOCK_PTR = 1; br 1  (restart the loop)
    end
    if (BLOCK_PTR <= 2)                ;; block 2: label L1
      ...ops, ending in exit_tb / goto_tb / goto_ptr (return)...
    end
  end
  i32.const 0                          ;; not normally reached
```

A branch stores the target's index in `BLOCK_PTR`. Going forward, it leaves
the current `if`; every block before the target is then skipped because its
index is smaller than `BLOCK_PTR`, and execution falls into the target and
everything after it, like straight-line code. Going backward, it branches
to the `loop`, which re-tests the guards from the top. A conditional branch
(`brcond`) wraps the same sequence in an `if`, one level deeper.

How it is emitted (`tcg-target.c.inc`):

1. `tcg_out_label()` in `tcg/tcg.c` calls `tcg_out_label_cb()`, which
   records the label in `current_label[]` and calls `tcg_out_label_idx()`:
   `end` (close the previous block), then the new guard. Block indices
   come from `wasm_alloc_block_idx()`, which starts at 1
   (`wasm_reset_state()`).
2. `tcg_out_op_br_to_label()` emits `i64.const` with a 5-byte LEB128
   placeholder (a fixed-width number encoding that can be patched in
   place), `local.set BLOCK_PTR` and `br`. If the label is already in
   `current_label[]` the branch is backward and goes to the loop.
3. After the last op, `wasm_resolve_label_placeholders()` patches each
   placeholder with its label's block index (`fill_uint32_leb128()`).

## Values

All TCG values, 32- or 64-bit, live in i64 locals (`TCG_TARGET_REG_BITS`
is 64 although wasm32 pointers are 32-bit). 32-bit ops are mostly emitted
as 64-bit ones (`add_i32` is `i64.add`), so **the upper 32 bits of an i32
value are garbage**. Every op whose result depends on them wraps or extends
its inputs: comparisons for i32 conditions (`tcg_out_op_cond()`), clz and
ctz, extract and deposit, mulu2/muls2/muluh/mulsh, shifts and rotations
(i32 opcodes), division and remainder (`tcg_out_i32_divrem()` computes in
64 bits on extended values, so `INT32_MIN / -1` cannot trap), ctpop and the
byte swaps. `add2_i32` and `sub2_i32` are not provided at all
(`TCG_TARGET_HAS_add2_i32 0`): TCG expands them.

Addresses in linear memory are i32, so a register used as a pointer is
wrapped with `i32.wrap_i64`. Wasm load/store offsets are unsigned; negative
offsets from env (the TLB and `icount_decr` live before `CPUArchState`) are
added explicitly. Reading env goes through `tcg_out_op_global_get_r_i32()`,
which caches the wrapped pointer in local 1. The cache (`env_cached`) is
reset at each TB start, each label and any write to R14.

**Pitfall:** the cache assumes the first env read of a block runs on every
path through it. Inside an `if` arm, read env with
`tcg_out_op_global_get_r()` and `i32.wrap_i64`, as the memory slow paths do.

Register roles (`tcg_target_init()`): all 24 registers are available;
R13 (`TCG_REG_TMP`) and R15 (`TCG_REG_CALL_STACK`) are reserved, and TCG
reserves R14 for env. A call clobbers A0 and A1. There are no constant
operand constraints, so TCG loads every constant into a register first.

## Helper calls

`tcg_out_call()` on WASI:

1. Stores the return address, `ctx->tb_ptr + <offset of the call in the
   TB>`, into `*ctx->tci_tb_ptr`. `GETPC()` is `tci_tb_ptr` for this backend
   (as for TCI, QEMU's portable interpreter backend), and TCG maps that
   offset back to a guest instruction when a helper needs the guest state.
2. On the TB's first call to this helper, registers it in the per-TB list
   (`wasm_register_helper()`) and writes its wasm type with
   `gen_func_type()`; helper number k gets type k + 1.
3. `gen_func_wrapper_code()` pushes the arguments, calls, and stores the
   result:
   - Arguments 1-8 come from A0-A7; TCG puts any further ones in the call
     area at R15 + 8 * n, and they are loaded from there.
   - i32 and pointer arguments are wrapped; i64 ones are passed as is.
   - `Int128` (QEMU's 128-bit integer type) is a struct in this build (no
     `CONFIG_INT128`), and the wasm C ABI passes structs by pointer: both
     halves are copied to `ctx->stack128` and a pointer is passed. An Int128
     result is returned through a hidden first argument pointing at the call
     area; its low half goes to A0 and its high half to A1.
   - Then `i32.const <helper table index>`, `call_indirect (type k+1)`.
   - An i32 result is sign-extended into A0; an i64 result goes to A0.

A throwing helper unwinds through the TB function and `tcg_qemu_tb_exec()`
to the `sigsetjmp()` in `cpu_exec()`. Nothing in the TB needs to survive,
because a TB is never resumed in the middle.

## Guest memory access

`tcg_out_qemu_ld()` and `tcg_out_qemu_st()` emit, on WASI:

```text
tcg_out_tlb_compare():
  index = (addr >> (page_bits - CPU_TLB_ENTRY_BITS)) & fast.mask
  entry = fast.table + index                 -> local 2
  hit   = entry->addr_read (or addr_write) ==
          (addr [+ size - alignment]) & (page_mask | alignment mask)
if hit
  host = entry->addend + (i32)addr           (tcg_out_tlb_host_addr)
  i64.load* / i64.store* at host
else
  helper_ld*_mmu(env, addr, oi, tb_ptr + offset)   via call_indirect
end
```

`CPUTLBDescFast` (`mask`, `table`, two 32-bit fields here) is read as one
i64 and split. The slow-path helpers have the types built by
`gen_func_type_qemu_ld()` (`(i32, i64, i32, i32) -> i64`) and
`gen_func_type_qemu_st()` (`(i32, i64, i32 or i64, i32, i32) -> ()`; the
value is i64 only for 8-byte stores). Byte swapping is left to TCG
(`TCG_TARGET_HAS_MEMORY_BSWAP 0`); x86 guests are little-endian anyway.

## TB exits and chaining

- **`exit_tb(value)`** (`tcg_out_exit_tb()`): if the value is this TB's
  address plus an exit index, it is computed from `ctx->tb_ptr` (see the
  next section); then `ctx->tb_ptr = 0` and `return value`.
- **`goto_tb(n)`** (`tcg_out_goto_tb()`): loads `tb->jmp_target_addr[n]`.
  An unlinked exit holds this TB's own *reset address* (`tc.ptr +
  jmp_reset_offset[n]`, a point inside this TB); anything else is the next
  TB. If linked: `ctx->tb_ptr = target; return 0`. Otherwise fall through
  to the `exit_tb` TCG emits next, which lets `cpu_exec()` link the two.
  The reset offset is patched into a 5-byte LEB `i32.const` once known.
  (An earlier version followed the field unconditionally, "jumping" into
  the middle of its own TB; chaining was disabled until this was fixed.)
- **`goto_ptr(reg)`** (`tcg_out_goto_ptr()`): if `reg` is this TB, restart
  the loop with `BLOCK_PTR = 0` without returning; otherwise
  `ctx->tb_ptr = reg; return 0`. `reg` is 0 when `helper_lookup_tb_ptr()`
  found nothing (`tcg_code_gen_epilogue` is NULL for this backend), which
  ends the chain.

`tcg_qemu_tb_exec()` keeps calling the TB in `ctx->tb_ptr` until it is 0
and then returns the last value to `cpu_exec()`. `tb_target_set_jmp_target()`
is empty: linking only writes `jmp_target_addr[n]`, which the code reads.

In the single-threaded build no other thread can interrupt the vCPU, so it
ends its *time slice* itself: `tcg_qemu_tb_exec()` counts TBs in
`wasi_tbs_executed`, and at `wasi_tb_yield_at` it sets
`icount_decr.u16.high = -1`, which makes the next TB leave through its
start-of-TB check. `cpu_handle_interrupt()` then checks the clock
(`wasi_slice_over()`) and returns to the main loop once the slice
(`QEMU_TB_SLICE_MS`, default 30 ms) is over.

## Position independence

While a TB runs, `ctx->tb_ptr` holds its own code address, so anything in
or near the TB is computed as `ctx->tb_ptr + constant`
(`tcg_out_tb_rel_addr()`): the `TranslationBlock` (for `exit_tb` values and
`jmp_target_addr`; `tb_struct_offset()`), helper return addresses and the
`goto_tb` reset address. The remaining constants (guest values, offsets in
the CPU state, helper table indices fixed when QEMU is linked) do not
depend on where the TB sits. So the same guest code always produces the
same bytes, which the host exploits:

- m-browser caches compiled modules by their bytes: after a flush, or in a
  new Store, retranslated code needs no new compile (`QEMU_JIT_WARM`).
- Every TB with the same bytes can share one *instance*, because TB modules
  have no state of their own: registers are locals, and the module has no
  globals, data or start function.

**Pitfall:** never embed an absolute address of anything in the code
buffer, and never give TB modules state (globals, memory of their own).
The first only defeats the cache; the second would make TBs that share an
instance share that state.

## Memory ordering and the threaded build

With `WASI_THREADS` (patch 0008; see [08-threads.md](08-threads.md)):

- TB modules import QEMU's memory as shared, matching QEMU's own.
- `TCG_TARGET_DEFAULT_MO` is `TCG_MO_ALL & ~TCG_MO_ST_LD`: TB loads and
  stores are plain wasm accesses, which Cranelift emits in program order,
  and an x86-64 host keeps them in x86 order. So x86 guests only get
  barriers where they ask for them (`mfence`, ...). `INDEX_op_mb` becomes
  `atomic.fence`; the single-threaded build emits nothing for it. On hosts
  other than x86-64 this ordering argument does not hold.
- `CONFIG_ATOMIC64` is forced for wasm32 with atomics in `meson.build`.
  Meson's link test fails because wasm-ld rejects `--start-group`, and
  without it every 8-byte guest access that misses the TLB took the
  stop-the-world exclusive path with several vCPUs.
- Each TB has one handle slot per vCPU, the decoded-program arena is
  locked, and decoder state is per thread (next sections).

## Running TBs: the tiers

`tcg_qemu_tb_exec(env, tb_ptr)` (`tcg/wasm32.c`) decides how to run each TB.
On first use per thread it allocates `ctx.stack` and `ctx.stack128`, and
with threads sets `jit_slot` to the vCPU's index (at most `WASI_JIT_SLOTS`,
8). On each call it frees decoded programs retired by a flush
(`interp_free_retired()`), reads `QEMU_JIT_HOT`, `QEMU_JIT_WARM` and
`QEMU_JIT_ASYNC` once, then loops over the chain:

```text
slot = tb_handle_slot(tb_ptr); handle = slot[jit_slot]
handle != 0 ----------------------------------------------> call compiled TB
prog = slot[PROG], decoded on first use (interp_decode)
interpretable, JIT allowed, WARM < HOT, run count at the warm point?
    -> wasi_jit_load_cached(): host has these bytes? ---> handle, call it
async on, interpretable, JIT allowed, count >= HOT, every 16th run?
    -> wasi_jit_load_async(): ready ---------------------> handle, call it
                              WASI_JIT_FAILED -> mark NO_JIT
                              0 (still compiling) -> fall through
interpretable and (NO_JIT or count < HOT or async)?
    -> count++, interp_exec(prog) ------------------------> next TB
otherwise -> wasi_jit_compile() (waits): handle ----------> call it
             fails: interpretable? mark NO_JIT, interpret : abort()
next TB: ctx.tb_ptr == 0 ? return the value : loop
```

Details:

- The *warm point* is `count == QEMU_JIT_WARM` in the single-threaded
  build. With threads it is every 16th run from `QEMU_JIT_WARM` up to
  `QEMU_JIT_HOT`: the run count is shared by all vCPUs but the handles are
  not, and each vCPU must get a chance to ask.
- A program that failed wasm type checking is tagged `INTERP_NO_JIT` at
  decode time and never offered to the JIT; so is one the JIT rejected.
- A TB the interpreter cannot decode (`INTERP_UNSUPPORTED`) is compiled
  synchronously on its first run. If that fails too, QEMU aborts with
  "TB ... can be neither compiled nor interpreted".
- A handle is the index of the TB's `start` function in the function table
  of the instance this thread runs in. Each thread records the handles it
  loaded in a FIFO ring (`struct tb_cache`, 65536 entries, the oldest
  quarter evicted when full) so that it can clear them all:
  `wasi_tb_cache_forget_all()` runs when the host has moved the thread to a
  new Store (`wasi_qemu_jit_reset()`, or the `WASI_VCPU_JIT_RESET` flag of
  `wasi_vcpu_step()`). Forgetting a handle frees nothing in the host; only
  replacing the Store does (the m-browser repository's
  docs/07-jit-host.md).
- `tcg_region_reset_all()` calls `wasi_tb_cache_flush()` on a flush: it
  retires the decoded programs, prints `[qemu-wasi] TB buffer flush #n`
  (first 10, then every 10th) and increments `tb_flush_gen`. A thread
  whose ring is from an older generation empties it without touching the
  slots, which now belong to new TBs.

## The interpreter

Compiling a TB module costs about 1 ms (the comment in `wasi-interp.c.inc`
says 1.5 ms) and saves roughly 0.7 µs per later run, so it only pays off
for TBs that run a few thousand times, and most TBs run only a few times.
`QEMU_JIT_HOT=5000` was measured best; 1000 and 500 were slower. The
interpreter (`tcg/wasm32/wasi-interp.c.inc`) runs exactly the function the
JIT would compile: local declarations and prologue from the header, then
the body.

### Decoding and validation

`interp_decode()` walks the header's sections to the code section, parses
the type section into call signatures (`decode_types()`), copies locals,
prologue and body into a per-thread buffer, and translates the body
(`decode_body()`). It accepts only what the backend emits:

- control: `unreachable`, `nop`, `block`, `loop`, `if`, `else`, `end`,
  `br`, `br_if`, `return`, `call_indirect` (table 0); block types: none,
  i32 or i64; no `br_table`, no branch out of the function;
- `drop`, `select`, `local.get/set/tee` (no globals);
- integer loads and stores (all widths), `i32.const`, `i64.const`;
- integer comparisons and arithmetic, `i32.wrap_i64`,
  `i64.extend_i32_s/u`, the sign-extension ops, `atomic.fence`.

It tracks operand stack heights and types like a wasm validator. A
structural problem (unsupported opcode, branch carrying a value, stack
deeper than 62, blocks nested more than 63 deep, more than 1024 types, a
helper type with more than 8 parameters or a non-integer type) makes the TB
`INTERP_UNSUPPORTED`: up to 20 are reported as `[wasi-interp] TB ... not
interpretable`. A type error is a backend bug: the program still runs in
the interpreter, but is tagged `INTERP_NO_JIT` and reported as `not valid
wasm` with the opcode and offset.

### Program format

The decoded program is a byte stream with fixed-size immediates; `block`,
`loop` and `end` disappear and every branch carries its resolved target
(an offset from the program start):

| Opcode | Immediates | Meaning |
|---|---|---|
| `I_IFZ` (0x04) | u32 target | Pop i32; jump if zero (an `if`) |
| `I_JMP` (0x0c) | u32 target | Jump (`br`, and the jump over an `else` arm) |
| `I_BRNZ` (0x0d) | u32 target | Pop i32; jump if non-zero (`br_if`) |
| `I_RETURN` (0x0f) | | Return the i32 on top |
| `I_CALL` (0x11) | u16 signature, u8 parameters, u8 results | Pop table index and arguments, call |
| `I_TRAP` (0x00), `I_FENCE` (0xfe) | | `unreachable`; `atomic.fence` |
| other wasm opcodes | local u8, memory offset u32, `i32.const` u32, `i64.const` u64 | As in wasm (alignment hints dropped) |

### Executor and arena

`interp_exec()` keeps operands as `uint64_t` (i32 values zero-extended) and
33 locals, local 0 being `ctx`. Memory accesses are plain pointer accesses
to QEMU's own memory. A trap (division by zero, `unreachable`) prints
`[wasi-interp] wasm trap` and aborts. A helper's exception unwinds through
`interp_exec()` like any C function.

Decoded programs are allocated 8-byte aligned (the low bits of the slot
carry the `INTERP_UNSUPPORTED` and `INTERP_NO_JIT` tags) from 1 MiB chunks.
A flush can happen inside a helper called by an interpreted TB, so
`interp_arena_retire()` only moves the chunks to a retired list, and
`interp_free_retired()` frees them at the next `tcg_qemu_tb_exec()`, when
no TB is running. With threads the arena is shared under `interp_lock`
(a flush then runs while all other vCPUs are stopped).

### The helper-call dispatcher

Wasm checks the exact callee type on every `call_indirect`, and a C call
through a function pointer compiles to `call_indirect` with the pointer's
prototype. So `interp_call_helper(sig, fn, args)` in
`wasi-interp-calls.c.inc` is a switch with one correctly typed call per
signature: result none, i32 or i64; 0 to 8 parameters; each i32 or i64.
`INTERP_SIG(r, n, m)` numbers them as `r * 511 + (2^n - 1) + m`, where bit
i of `m` marks parameter i as i64 (1533 cases). The file is generated by
`scripts/gen_interp_calls.py` and matches its output. Regenerate it only if
helpers can get more than 8 wasm parameters (TCG allows 7 arguments,
`MAX_CALL_IARGS`, plus the hidden Int128 result pointer) or a new value
type; the command is in
[05-repository-tour.md](05-repository-tour.md#gen_interp_callspy).

## Environment variables

QEMU reads these from its WASI environment; m-browser passes on every
`QEMU_*` and `WASI_*` variable. Full list:
[reference/environment-variables.md](reference/environment-variables.md).

| Variable | Default | Effect |
|---|---|---|
| `QEMU_JIT_HOT=<n>` | 5000 | Interpreted runs before a TB is compiled; 0 compiles every TB synchronously on its first run (it is still decoded once); 4000000000 means interpreter only, except for TBs it cannot decode |
| `QEMU_JIT_WARM=<n>` | 50 | Runs after which a TB whose module the host still has is reloaded; no effect unless smaller than `QEMU_JIT_HOT` |
| `QEMU_JIT_ASYNC=0` | on | Compile hot TBs synchronously; asynchronous mode is off anyway when `QEMU_JIT_HOT=0` |
| `QEMU_TB_DUMP=<pc>,<dir>` | off | Write the module of the TB at guest pc `<pc>` (hex) to `<dir>/tb_<pc>.wasm` |
| `QEMU_PROF_EVERY=<s>` | off | Print a time profile every s seconds (single-threaded build only) |
| `QEMU_TB_SLICE_MS=<ms>` | 30 | Length of a vCPU time slice, checked every 1024 TBs (single-threaded build only) |
| `QEMU_TB_SLICE=<n>` | off | Slices of n TBs instead of a time (single-threaded build only) |
| `WASI_DEBUG=2` | 0 | Hot-path tracing, including the first TB executions and then every 10 millionth |

`QEMU_PROF_EVERY` output (`wasi_prof_report_maybe()`), per window:

```text
[wasi-prof] 10 s: translate 0.4 s (2100 TBs, 190 us each), decode 0.1 s,
  jit-compile 0.8 s (650), exec-interp ~2.0 s (3.1M TBs, 0.65 us each),
  exec-jit ~4.1 s (40.2M TBs, 0.10 us each), other-cpu 1.2 s,
  main-loop 0.9 s, idle 0.5 s, memory 1650 MiB
```

(The numbers are illustrative; it is one line.) *translate*: time in
`tb_gen_code()`, TBs translated and the mean. *decode*: interpreter
decoding. *jit-compile*: time spent asking the host for modules (compiles
and cache loads), and how many it returned. *exec-interp*, *exec-jit*:
estimated from timing every 256th run. *other-cpu*: the rest of the vCPU's
time (`cpu_exec()` itself, lookups, interrupts, RCU callbacks, which are
QEMU's deferred frees). *main-loop*: timers, devices and display, without
*idle* (sleeping). *memory*: size of QEMU's linear memory. A second line
splits the interpreted runs by how often the TB had run before (<50,
50-199, 200-999, 1000 to `QEMU_JIT_HOT`, never compiled) and counts TBs
reaching 50/200/1000 runs, to judge `QEMU_JIT_HOT`. The flush message and
`QEMU_STATUS_EVERY` also print `wasi_tb_exec_report()`: TBs decoded, not
interpretable, invalid, promoted, reloaded, rejected by the JIT, and
interpreted executions.

**Pitfall:** `QEMU_TB_DUMP` writes through WASI, so `<dir>` is a path inside
QEMU's sandbox (a preopened directory, e.g. under `/data` in m-browser's
GUI). The file is written whenever the module is handed to the host, which
with the tiers happens at the warm or hot point, not on the first run; use
`QEMU_JIT_HOT=0` to dump at first execution.

## Porting checklist: what tcg/tcg.c needs

The hooks generic TCG (QEMU 9.1) expects from a backend, all in
`tcg/wasm32/tcg-target.c.inc` unless noted:

| Hook | Here |
|---|---|
| `tcg_target_op_def()` | Constraint set for every op the backend implements |
| `tcg_target_reg_alloc_order[]`, `tcg_target_call_iarg_regs[]`, `tcg_target_call_oarg_reg()` | R0-R15 then A0-A7; arguments A0-A7; results A0 (+A1) |
| `tcg_target_init()` | Available, clobbered and reserved registers; the frame at R15 |
| `tcg_target_qemu_prologue()` | Empty: there is no generated prologue |
| `tcg_out_tb_start()` | Reserves the 8-byte prefix |
| `tcg_out_ld`, `tcg_out_st`, `tcg_out_sti`, `tcg_out_mov`, `tcg_out_movi` | Loads, stores, moves |
| `tcg_out_ext8s/8u/16s/16u/32s/32u`, `tcg_out_exts_i32_i64`, `tcg_out_extu_i32_i64`, `tcg_out_extrl_i64_i32` | Extensions |
| `tcg_out_addi_ptr`, `tcg_out_xchg` | Unused (assert) / unsupported (`false`) |
| `tcg_out_exit_tb`, `tcg_out_goto_tb`, `tcg_out_call` | As described above |
| `tcg_out_op()` | Every other op, including `goto_ptr`, `br`, `qemu_ld/st`, `mb` |
| `tcg_target_const_match()` | Only `TCG_CT_CONST` |
| `patch_reloc()`, `tb_target_set_jmp_target()` | Unused (`false`), empty |
| `tcg_target_has_memory_bswap()`, `MIN_TLB_MASK_TABLE_OFS` | `false`; `INT_MIN` |
| `tcg-target.h`, `tcg-target-con-set.h`, `tcg-target-con-str.h`, `tcg-target-reg-bits.h` | Capabilities, registers, ABI |
| `tcg_qemu_tb_exec()` (`HAVE_TCG_QEMU_TB_EXEC`), `print_insn_tci()` | `tcg/wasm32.c` |
| Wasm-only calls added to `tcg/tcg.c`: `tcg_out_label_cb()`, `tcg_out_init()`, `wasm_reset_state()`, `wasm_resolve_label_placeholders()`, `wasm_finalize_module()` | `tcg-target.c.inc`, `tcg-wasm32-helpers.c.inc` |

The structure follows TCI (`tcg/tci/`): a C `tcg_qemu_tb_exec()`,
`GETPC()` from `tci_tb_ptr`, `print_insn_tci()`. When upgrading, diff
`tcg/tci/` between the two QEMU releases to see how the backend interface
moved. Upstream has reorganised that interface since 9.1 (as far as we
know, per-opcode emitter structures replace much of `tcg_out_op()`); check
the new release rather than this table. Procedure:
[11-upgrading-qemu.md](11-upgrading-qemu.md).

## Generic QEMU files changed for the backend

| File | Change |
|---|---|
| `tcg/tcg.c` | Forward declarations; `tcg_out_label()` calls `tcg_out_label_cb()`; includes `tcg-wasm32-helpers.c.inc`; `tcg_gen_code()` calls `tcg_out_init()` and `wasm_reset_state()` after `tcg_out_tb_start()`, and `wasm_resolve_label_placeholders()` and `wasm_finalize_module()` at the end instead of flushing the instruction cache; no prologue or epilogue setup |
| `tcg/region.c` | `tcg_region_reset_all()` calls `wasi_tb_cache_flush()`; the code buffer is `aligned_alloc()` memory, never executable |
| `tcg/meson.build` | Adds `tcg/wasm32.c` for wasm32 hosts |
| `tcg/tcg-common.c` | `ARRAY_SIZE` replaced by a `sizeof` division (reason not recorded) |
| `include/tcg/tcg.h`, `include/exec/exec-all.h`, `disas/disas-host.c` | `tcg_qemu_tb_exec()` as a function, `GETPC()` = `tci_tb_ptr`, `print_insn_tci` as for TCI |
| `util/cacheflush.c` | No-op `flush_idcache_range()` for `__wasm__` |
| `accel/tcg/ldst_common.c.inc` | `set_done_flag()` after each load/store helper, Emscripten only |
| `accel/tcg/cpu-exec.c` | Time-slice check in `cpu_handle_interrupt()`, translation time for the profile (patch 0003) |
| `meson.build`, `configure` | wasm32 as a host CPU, `CONFIG_ATOMIC64` (patches 0001, 0008) |
| `include/qemu/atomic.h` | `ATOMIC_REG_SIZE` build assertions disabled (patch 0001) |

## Things a porter must preserve

Each of these was a real bug or a contract with the host.

1. **Block 0's guard is `BLOCK_PTR <= 0` and block indices start at 1**
   (`wasm_reset_state()`). Otherwise a backward branch re-ran block 0: x86
   `rep` string instructions branch back when ECX reaches 0, which pushed
   twice or looped forever.
2. **A call clobbers A0 and A1** (`tcg_target_call_clobber_regs`). An
   Int128 result fills both; with only A0, `cmpxchg16b` used the loaded
   data as its store address.
3. **The upper 32 bits of i32 values are garbage**: wrap or extend in every
   op that depends on them (list under [Values](#values)), and keep
   `TCG_TARGET_HAS_add2_i32` and `sub2_i32` at 0. In `tcg_out_add2()` and
   `tcg_out_sub2()` the carry is an i32 comparison result and must be
   extended to i64 before it is added.
4. **Threaded build: one handle slot per vCPU, a shared run count, and a
   warm-reload check that lets every vCPU ask**; it used to fire only for
   the vCPU that reached `QEMU_JIT_WARM`.
5. **`CONFIG_ATOMIC64` for wasm with atomics**, or 8-byte accesses missing
   the TLB stop every other vCPU.
6. **Position independence and stateless modules**
   ([above](#position-independence)).
7. **Helper calls stay `call_indirect` through the imported table**, with
   types exactly matching the C prototypes. Host trampolines would block
   exceptions; a type mismatch traps.
8. **`tci_tb_ptr` is set before every helper call**, and load/store helpers
   receive the same kind of return address, or guest exceptions restore
   the wrong instruction.
9. **`goto_tb` compares against the reset address**, not 0.
10. **`exit_tb` clears `ctx->tb_ptr`; `goto_tb` and `goto_ptr` store the
    next TB there and return 0.** `tcg_qemu_tb_exec()` depends on it.
11. **The code-buffer layout and slot order** are read by
    `compile_and_cache_tb()`, `interp_decode()` and `tb_handle_slot()`.
12. **The module interface** (`env.buffer`, `env.table`, export `start` of
    type `(i32) -> i32`) and the `struct wasmContext` offsets are shared
    with m-browser.
13. **The interpreter accepts what the backend emits.** A new wasm opcode
    must be added to `decode_body()`, `skip_immediates()`,
    `numeric_types()`/`is_unary()` if numeric, and `interp_exec()`;
    otherwise every TB using it is compiled on its first run.
14. **Local indices and branch depths are single bytes**; keep them under
    128 or emit LEB128.
15. **Placeholders are exactly 5 bytes** (`fill_uint32_leb128()`).

## Emscripten-only code

Kept for reference, not compiled by either WASI build, and not maintained:
`instantiate_wasm()` (`EM_JS`), `prepare_wasm()`, the `active_tbs` ring and
the Emscripten branch of `tcg_qemu_tb_exec()` in `tcg/wasm32.c`; the
`#ifndef WASI_PLATFORM` paths of `tcg-target.c.inc` (block splits at calls
and memory accesses, `DONE_FLAG`/`UNWINDING` checks, `tcg_out_tlb_load()`,
the old `goto_tb`, `global.get/set` registers); the global section and
`do_init` prologue in `wasm_finalize_module()`; `set_done_flag()`,
`set_unwinding_flag()`, `get_core_nums()` and the matching context fields;
`util/coroutine-fiber.c`. It has drifted: for example, the Emscripten
`tcg_qemu_tb_exec()` reads a function index at `export_vec_offset`, where
the current layout stores `export_vec_size`, and it allocates `ctx.stack`
with a byte count divided by 8. Treat it as history, not working code.

## Limits and rough edges

- **`-d out_asm` hangs QEMU** (found by reading the code, not by running
  it): `disas()` loops forever because `print_insn_tci()` returns 0. Use
  `-d in_asm,op_opt` (guest code and the TCG ops the backend received) and
  the module dumps below.
- At most about 100 labels per TB (`current_label[100]`), 1024 helpers and
  4096 label uses. The checks are `tcg_debug_assert()`, which is not
  checked in these builds (`CONFIG_DEBUG_TCG` is off), so going over
  silently corrupts memory.
- A TB that jumps to itself through `goto_ptr` loops inside one wasm call,
  so those iterations do not count toward the single-threaded time slice.
- The `qemu_ld/st_a32` forms are emitted like the `a64` ones without
  zero-extending the address. x86_64-softmmu only uses `a64`; check this
  before building a target with 32-bit guest addresses.
- The statistics counters (`interp_stats`, `wasi_prof`, `wasi_tbs_executed`)
  are not atomic; with threads they are approximate, and `QEMU_PROF_EVERY`
  is never printed because the threaded main loop does not call
  `wasi_prof_report_maybe()`.
- Some comments still say the registers are wasm globals
  (`tcg_out_op_cond()`, `tcg_out_extract()`, `gen_func_wrapper_code()`,
  `tcg_target_init()`), and the layout comment of `wasm_finalize_module()`
  calls `export_vec` empty; the code is right, the comments are stale.

## How to inspect generated code

1. Dump one TB from QEMU: run with `QEMU_TB_DUMP=<pc>,<dir>` (and
   `QEMU_JIT_HOT=0` for the first run), then look for `QEMU_TB_DUMP: wrote`
   on stderr.
2. Or dump modules from the host: m-browser's `QEMU_JIT_DUMP=<dir>[,<n>]`
   saves every n-th compiled module (default 100) as `<dir>/tb-NNNNNNN.wasm`
   (a host path). A module that fails a synchronous compile is saved as
   `qemu-jit-failed.wasm` in the host's temporary directory.
3. Print it as text, from wherever the file is:

   ```sh
   wasm-tools print tb_ffffffff81000000.wasm
   ```

   The prologue, the `loop`, the `if` guards on local 32 and the
   `call_indirect` sequences above are easy to recognise.
4. Validate it; an error here is a backend bug (the interpreter reports the
   same as `not valid wasm`). The threads proposal is enabled by default;
   add `--features all` if a feature is rejected:

   ```sh
   wasm-tools validate tb_ffffffff81000000.wasm
   ```

5. Compare with the guest instructions and TCG ops of the same TB, logged
   by `-d in_asm,op_opt -D <file>` on QEMU's command line.

After any change to the backend, run the instruction test once per tier
([13-testing.md](13-testing.md)), from the qemu-wasi root:

```sh
QEMU_JIT_HOT=0 make test             # compiled code only
QEMU_JIT_HOT=4000000000 make test    # interpreter only
```
