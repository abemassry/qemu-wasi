# Display, input and network

**What this covers.** The QEMU side of the services the outside world gives
the emulated PC: the screen (the display backend in `display/`), keyboard
and mouse (input events the same backend polls from the host) and the
network (the `user` network backend, `qemu-src/net/wasi-user.c.inc`): data
paths, functions, the reasons behind them, extending and debugging. The
host side is documented in the m-browser repository's
docs/09-display-and-input.md (window, key mapping, screenshots) and
docs/10-networking.md (the virtual network). Terms are also explained in
[reference/glossary.md](reference/glossary.md).

## Overview

```text
           DISPLAY               INPUT                 NETWORK
GUEST      draws on screen,      reads keys and        sends and receives
           writes to ttyS0       pointer               Ethernet frames
             v                     ^                     ^ v
DEVICE     VGA ("std"), serial   PS/2 keyboard and     e1000 network
MODELS     -> "vc" text console  mouse, usb-tablet     card (NIC)
             v DisplaySurface      ^ qemu_input_*()      ^ v frames
QEMU,      display/              display/              net/wasi-user.c.inc
THIS DOC   wasi_gfx_dcl.c,       wasi_gfx_display.c    (patch 0006)
           wasi_gfx_display.c    (poll, pace, route)
             v                     ^                     ^ v
HOST       host_gfx_init,        host_gfx_poll_events  host_net_recv/wait,
IMPORTS    host_gfx_resize,                            host_net_send,
("env")    host_gfx_update_                            host_net_hostfwd,
           framebuffer                                 host_main_wake
             v                     ^                     ^ v
M-BROWSER  src/qemu_gfx.rs:      src/app.rs (winit),   src/qemu_net.rs:
           softbuffer window     src/headless.rs       10.0.2.0/24, NAT
           or PNG snapshot
```

A refresh timer in QEMU's main loop drives display and input, and each
main loop iteration polls the network; frames from the guest leave from
whichever thread runs the NIC model. Exact signatures:
[reference/host-interface.md](reference/host-interface.md).

## Display

### Terms

- A *framebuffer* is an image of the screen in memory, here 4 bytes per
  pixel: blue, green, red, unused (*BGRA*; QEMU's x8r8g8b8).
- A *console* is a screen QEMU can show: a *graphic console* belongs to an
  emulated graphics card (the standard VGA), a *text console* ("vc") is a
  terminal QEMU draws itself, here for the monitor (QEMU's command prompt)
  and the guest's serial port.
- A *DisplaySurface* (`include/ui/surface.h`) is a console's framebuffer
  object: size, stride (bytes per row), format, pixels. In VGA graphics
  modes it usually points straight into the emulated video memory.
- A *DisplayChangeListener* (DCL) is what a display backend (GTK, VNC,
  ours) registers: a console (`dcl->con`) and callbacks
  (`DisplayChangeListenerOps`): a periodic refresh, "the surface was
  replaced", and "this *dirty rectangle* changed" (`dpy_gfx_update()`),
  so that only what changed is copied.

### How the backend gets into QEMU

`display/` lies outside `qemu-src/`, so meson does not know it.
`scripts/fix_build_ninja.py` compiles `DISPLAY_SOURCES` with the flags of
`FLAGS_FROM` (`libcommon.a.p/ui_console.c.o`, QEMU's `ui/console.c`) plus
`-DQEMU_BUILD`, and links them into `qemu-system-x86_64`
([05-repository-tour.md](05-repository-tour.md#fix_build_ninjapy)).
Changes in `display/` are plain qemu-wasi commits; no patch to export.

`qemu_init_displays()` in `qemu-src/system/vl.c` calls
`wasi_gfx_display_register_dcl()` right after `qemu_display_init()` (patch
0007), once the machine and its consoles exist, whatever `-display` says
(it is no selectable display type, which would need a new `DisplayType`
in QAPI; without GTK, SDL, VNC or curses, `-display` is `none` anyway).
Its comments show that it began as a wasi-gfx WIT client (GPU device,
pipeline, frame events); today it hands BGRA frames to four host
functions, so several comments are stale
([below](#known-inaccuracies-in-comments)).

### The functions

(dcl) marks `display/wasi_gfx_dcl.c`, the rest are in
`display/wasi_gfx_display.c`. Its state is static, so there is one
display: `wasi_display` (`fb_width`, `fb_height`, `fb_data` the backend's
copy of the screen, `scale`, `out_data` the scaled copy the host reads,
`initialized`) and `frame_dirty`.

| Function | Called by | What it does |
|---|---|---|
| `wasi_gfx_display_register_dcl()` (dcl) | `qemu_init_displays()` | Picks a console (`wasi_gfx_pick_console()`), takes its surface size (1024x768 if none), calls `wasi_gfx_display_init()`, registers the DCL, sets the input console. With no console at all it prints `no console found, deferring init` and returns -1, which the caller ignores: nothing retries, there is no display and input is never polled |
| `wasi_gfx_display_switch_console(i)` (dcl) | Ctrl+Alt+n | Unregisters the DCL, points it and the input at console `i`, registers it again |
| `wasi_gfx_dpy_refresh()` (dcl) | QEMU's refresh timer | One refresh cycle ([below](#data-path-the-refresh-cycle)) |
| `wasi_gfx_dpy_update()` (dcl) | `dpy_gfx_update()` | Passes the dirty rectangle to `wasi_gfx_display_update()` |
| `wasi_gfx_dpy_switch()` (dcl) | `dpy_gfx_replace_surface()`, registration | `wasi_gfx_display_resize()`, then `wasi_gfx_display_blit()` of the whole new surface |
| `wasi_gfx_display_init(w, h)` | registration | Chooses the scale, allocates `fb_data` and `out_data`, `host_gfx_init()` with the scaled size |
| `wasi_gfx_display_update(s, x, y, w, h)` | `wasi_gfx_dpy_update()` | Copies the rows of the rectangle that differ into `fb_data`, sets `frame_dirty` |
| `wasi_gfx_display_blit(p, w, h, stride)` | full refresh, switch | The same for a whole surface; reallocates and calls `host_gfx_resize()` if the size differs |
| `wasi_gfx_display_render()` | refresh | If needed: scales `fb_data` into `out_data`, `host_gfx_update_framebuffer()` |
| `wasi_gfx_display_resize(w, h)` | `wasi_gfx_dpy_switch()` | Sets `frame_dirty`; if size or scale changed, reallocates, `host_gfx_resize()` |
| `wasi_gfx_display_poll_events()` | refresh | Input ([below](#input)) |
| `wasi_gfx_display_shutdown()` | nobody | Frees the buffers |

### Data path: the refresh cycle

QEMU's `gui_update()` (`ui/console.c`) runs on a realtime timer every
`GUI_REFRESH_INTERVAL_DEFAULT` (30 ms; the DCL sets no `update_interval`)
and calls `wasi_gfx_dpy_refresh()`:

1. `wasi_gfx_display_poll_events()`: input first.
2. The refresh is *full* with `WASI_GFX_FULL_REFRESH`, else when
   `refresh_count % FULL_REFRESH_EVERY == 1` (refreshes 1, 61, 121, ...).
3. Graphic consoles only (text consoles have no hardware: `ui/console-vc.c`
   draws as characters arrive and calls `dpy_gfx_update()` itself): on a
   full refresh `graphic_hw_invalidate()` (VGA forgets its mode), then
   `graphic_hw_update()`. VGA finds what the guest changed (graphics
   modes: dirty bits of video memory; text mode: changed character cells)
   and calls `dpy_gfx_update()` per changed band. On a mode change, and in
   graphics modes after an invalidate or a page flip, it first makes a new
   surface, which reaches `wasi_gfx_dpy_switch()`.
4. Full refresh: `wasi_gfx_display_blit()` of the console's surface.
5. `wasi_gfx_display_render()` returns at once unless `frame_dirty` is set
   or `FRAME_RESEND_INTERVAL` (30) refreshes passed without a frame.
6. Timings go to the `QEMU_DISPLAY_STATS` counters.

Why it works this way:

- **Dirty tracking first, full redraw as a safety net.** At bring-up dirty
  tracking was thought unreliable under the WASI TCG backend, so every
  refresh forced a full redraw and copy. But VGA answers an invalidate in
  graphics modes with a new surface, so each frame became a reallocation:
  over 30% of the CPU at 1280x800. Now the forced redraw runs every 60th
  refresh only, in case an update was missed.
- **Compare before copying.** Each row is `memcmp()`ed first, and only a
  difference sets `frame_dirty` (hence the local copy `fb_data`): VGA text
  mode reports the cursor cell as changed on every refresh, and a frame
  costs far more than the comparison.
- **Unchanged frames are not sent.** Scaling and handing over a frame
  takes several milliseconds on the thread that, in the single-threaded
  build, also runs the guest CPU, and the screen is usually static. After
  30 refreshes without a change (about a second) one is sent anyway, in
  case the host must redraw its window. `wasi_gfx_display_resize()` sets
  `frame_dirty` even for an unchanged size, so each surface switch sends a
  frame too.

### Scaling

`wasi_gfx_scale()` reads `WASI_GFX_SCALE` once (`atoi`, default 2, clamped
to 1..8); `wasi_gfx_scale_for(w, h)` lowers it until the result fits in
1600x1200. A text console is 80x24 characters of an 8x16 font, 640x384
pixels, tiny on a modern display; a whole factor with nearest-neighbour
copying keeps glyph edges sharp. The cap keeps large desktops 1:1, where
doubling would also make each frame four times as expensive.

| Console | Size | Sent to the host |
|---|---|---|
| VGA before the guest draws (QEMU's placeholder) | 640x480 | 1280x960 (x2) |
| VGA text mode | 720x400 | 1440x800 (x2) |
| Text console (`-monitor vc`) | 640x384 | 1280x768 (x2) |
| 1024x768, 1280x800 | same | same (x1) |

`wasi_gfx_display_render()` copies each source pixel `scale` times, then
the row `scale - 1` times (`wasi_gfx_alloc_out()` falls back to 1x if
`out_data` cannot be allocated). Everything the host sees is scaled:
sizes, frames, its window and PNG snapshots, and the pointer positions it
sends back.

### Measuring: QEMU_DISPLAY_STATS

With `QEMU_DISPLAY_STATS` set, `refresh_stats_report()` prints at the
first refresh after each 20 s: `wasi-gfx: <n> refreshes in <ms> ms: <x> ms
each (hw <a>, blit <b>, render <c>) = <p>% of the time`. *hw* is steps 1
and 3 (input, then VGA's update with the copies and surface switches it
causes), *blit* step 4, *render* step 5 including the host's handling of
the frame; *p* is the share of wall-clock time spent refreshing, which the
single-threaded build takes from the guest.

### Consoles

`wasi_gfx_pick_console()` takes `WASI_GFX_CONSOLE=<index>` if that console
exists (else `wasi-gfx: WASI_GFX_CONSOLE=... not found, ignoring`). Else
it prints `wasi-gfx: console <i> is graphic|text (Ctrl+Alt+<i+1>)` up to
the first graphic console and picks it, or the first text console. QEMU
numbers consoles from 0, graphic ones of cold-plugged devices first
(`qemu_console_register()`), then text ones in creation order: with
m-browser's `-monitor vc -serial vc`, 0 is the guest's screen, 1 the
monitor, 2 the serial port. More on the variables:
[reference/environment-variables.md](reference/environment-variables.md#display).

Text consoles are drawn by `ui/console-vc.c`, which needs pixman, a
library not ported to WASI. Patch 0005 gives upstream's stand-in
`include/ui/pixman-minimal.h` the few operations it uses (glyph masks,
fills, blending, rectangle copies) and builds `console-vc.c` instead of
its stubs
([06-patch-series.md](06-patch-series.md#0005-ui-draw-text-consoles-without-pixman)).

## Input

### Terms

- An *evdev keycode* is Linux's number for a physical key position
  (`linux/input-event-codes.h`; `KEY_A` is 30): a stable public numbering
  for the host to send. A *qcode* (`QKeyCode`, `Q_KEY_CODE_A`) is QEMU's
  own, understood by all its keyboard models; `qemu_input_linux_to_qcode()`
  converts, with a table generated at build time
  (`qemu-build/ui/input-keymap-linux-to-qcode.c.inc`).
- A *keysym* is what a key press means as a character. Text consoles take
  keysyms: a character code, or a `QEMU_KEY_*` value for special keys
  (`include/ui/console.h`).
- A *relative* pointing device (a mouse) reports movements, an *absolute*
  one (a tablet) positions. The host only knows where its pointer is, so
  the backend sends positions, which need `-device usb-tablet` (on
  `-device qemu-xhci`); the PC's PS/2 mouse is relative.

### The event protocol

`host_gfx_poll_events(buf, max)` writes up to `max` events of three
`int32_t`s, `[type, code, value]`, and returns how many.

| Type | Event | `code` | `value` | QEMU does | Paced |
|---|---|---|---|---|---|
| 1 | Pointer position | 0 = x, otherwise y | Pixel of the scaled frame | Clamps to the frame; `qemu_input_queue_abs(NULL, axis, value, 0, span - 1)`, `span` = framebuffer size x scale | no |
| 2 | Button | `InputButton`: 0 left, 1 middle, 2 right, 3/4 wheel up/down, 5 side, 6 extra, 7/8 wheel left/right | 1 down, 0 up | `qemu_input_queue_btn()`, `qemu_input_event_sync()` | no |
| 3 | Key | evdev keycode | 1 down, 0 up | `wasi_gfx_handle_key()` | yes |
| 4 | Host window resized | width | height | Prints `... (ignored)` | no |
| 5 | Text | Unicode code point | unused | `wasi_gfx_handle_text()` | yes |

Other types are ignored. Pointer and button events carry no console
(`NULL`), so they reach the guest's pointing device whatever is shown. A
sync after each button event matters: pointing devices build one report
per sync, so a quick click within one poll would cancel out. Type 4 is
ignored: the host's window shows the scaled frame, so feeding its size
back would compound the scale, and QEMU owns console sizes anyway.

### Delivery and pacing

`wasi_gfx_display_poll_events()` runs first in every refresh (once the
display is initialized). It tops up `pending_events` (`MAX_EVENTS`, 128)
from `host_gfx_poll_events()`, the rest staying in the host; handles
events in order but stops before the seventh key or text event
(`KEYS_PER_POLL` is 6), which waits with everything after it, pointer
events too, for the next refresh; and calls `qemu_input_event_sync()`.

Why pacing: the guest's devices buffer little and drop the rest. A PS/2
keyboard holds 16 bytes (`PS2_QUEUE_SIZE`, `hw/input/ps2.c`), a USB
keyboard 16 events (`QUEUE_LENGTH`, `include/hw/input/hid.h`), a text
console 16 characters (`out_fifo`, `ui/console-vc.c`). When the main loop
stalled, for instance while the host moved QEMU to a fresh wasmtime Store
(done now and then to free compiled code), keys typed meanwhile arrived in
one burst and were partly lost. Six events per refresh, about two
characters (press, text, release each), leave the guest time to drain its
device.

### Keys and text

`wasi_gfx_handle_key(evdev, down)` tracks Ctrl (evdev 29, 97) in
`wasi_ctrl_held` and Alt (56, 100) in `alt_held`; a press clears
`wasi_swallow_text`. Ctrl+Alt+1..9 (evdev 2..10) switches consoles
([below](#consoles-ctrlaltn)). Other keys go through
`qemu_input_linux_to_qcode()` (none: `wasi-gfx: no qcode for evdev N`).
On a graphic console, `qemu_input_event_send_key_qcode()` hands them to a
keyboard not bound to another console (PS/2, or a USB keyboard if one was
added). On a text console only presses count, through
`qemu_text_console_put_qcode()`, which knows only Tab, Backspace, Delete,
arrows, Home, End, PageUp and PageDown (Ctrl+Up/Down/PageUp/PageDown
scroll the console's history).

All other keys reach a text console as text. `wasi_gfx_handle_text()`
drops the event if `wasi_swallow_text` is set or the input console is not
a text console; with Ctrl held, a-z and A-Z become control characters
1-26, which the monitor's line editing uses (Ctrl+A, Ctrl+U). Then
`qemu_text_console_put_keysym()`, as QEMU's GTK front end does through
`qemu_text_console_put_string()`. Enter and Escape reach text consoles
only this way, as 13 and 27. So each key press reaches QEMU twice, as key
and as text, and each kind of console uses one: the host must send no
text for the keys `qemu_text_console_put_qcode()` handles, or text
consoles act on them twice (m-browser's `handled_as_qcode`).

### Consoles: Ctrl+Alt+n

Ctrl+Alt+n (n = 1..9) shows console n-1 and sends input there, as in
QEMU's GTK and SDL front ends. `wasi_gfx_handle_key()`:

1. If the old console is graphic, sends releases of both Ctrl and both
   Alt keys to the guest: it saw them go down, and the real releases will
   go elsewhere.
2. Calls `wasi_gfx_display_switch_console(n - 1)`: `wasi-gfx: no console
   N` if missing, nothing if already shown, else `wasi-gfx: switching to
   graphic|text console N` and re-registration of the DCL, whereupon QEMU
   calls `wasi_gfx_dpy_switch()` with the new surface.
3. Sets `swallow_release` and `wasi_swallow_text`: neither the digit's
   release nor its text reaches the new console.

The backend keeps its own input console instead of QEMU's "active console"
so that keys go to what is on screen. The switch runs inside the refresh
callback: unregistering the only listener frees QEMU's refresh timer, and
registering creates one that `gui_update()` then re-arms.

## Network

### Terms

- The *NIC* (network interface card) is the emulated card the guest's
  driver talks to (the `pc` machine's default: Intel e1000). A *netdev*
  (network backend) connects its other end: `user`, `tap`, ... Both are
  `NetClientState`s, each the other's *peer*.
- An *Ethernet frame* is what a network card sends and receives: target
  and source hardware (MAC) addresses, a type and a payload (an IP packet,
  say).
- *User-mode networking* (`-nic user`) needs no privileges: a private
  10.0.2.0/24 network behind NAT, emulated in the emulator's process,
  normally by libslirp. WASI builds have none: m-browser emulates the
  network and QEMU passes it the frames.

### Setting up

`-nic user`, `-netdev user` with a `-device` NIC, and legacy `-net user`
all reach `net_init_wasi_user()`: `net/net.c` includes `wasi-user.c.inc`
under `WASI_PLATFORM && !CONFIG_SLIRP` (no meson change needed) and puts it
in `net_client_init_fun[NET_CLIENT_DRIVER_USER]`
([06-patch-series.md](06-patch-series.md#0006-net-user-mode-networking-provided-by-the-wasi-host)).
`net_init_wasi_user()`:

1. Rejects the first option `wasi_user_unsupported_option()` finds (`user
   network option 'X' is not supported: the WASI host provides a fixed
   network`). Only `id`, `hostfwd` and the defaults `ipv4=on`, `ipv6=off`,
   `restrict=off` pass; `net`, `dns`, `tftp`, `smb`, `guestfwd`, ... are
   refused, as the host's addresses are fixed and ignoring them would
   silently give another network than the one asked for.
2. Refuses a second user network (`only one user network is supported`):
   the host has one network, and `wasi_user` is a global.
3. Passes each `hostfwd` rule to `host_net_hostfwd(rule, strlen(rule))`;
   if the host refuses, it prints why (m-browser forwards TCP only) and
   QEMU stops with `could not set up host forwarding rule '...'`.
4. Creates the client (`qemu_new_net_client()`, info string
   `net=10.0.2.0/24, provided by the WASI host`) and sets `wasi_user` and
   `wasi_idle_wait = wasi_user_idle_wait`, which `wasi_user_cleanup()`
   resets.

**Default network.** Without `-nic`, `-net` or `-netdev`,
`qemu_create_default_devices()` (`system/vl.c`) adds `-net nic` and, since
patch 0007, `-net user` (upstream: only with `CONFIG_SLIRP`), joined
through QEMU's hub 0 like all legacy `-net` clients. `-nic none` or
`-nodefaults` gives no network.

### Frames both ways

Guest to host: the NIC model transmits with `qemu_send_packet()` to its
peer, and `wasi_user_receive()` calls `host_net_send(buf, size)`,
returning `size` whatever the host answers: like a wire, the host may drop
a frame. With no `can_receive`, QEMU never holds frames back on this side.

Host to guest: there are no file descriptors to watch, so
`qemu_main_loop_step()` (`system/runstate.c`) calls `wasi_net_poll()`
after every `main_loop_wait()`, in both builds:

```text
while not blocked:
    len = host_net_recv(buf, NET_BUFSIZE)          0: nothing waiting
    if qemu_send_packet_async(nc, buf, len, wasi_user_send_completed) == 0:
        blocked = true                 the NIC cannot take it now
```

A 0 means the NIC cannot take the frame now (receive ring full, or
receiving not enabled yet): QEMU keeps a copy in its queue and calls
`wasi_user_send_completed()`, clearing `blocked`, once the NIC took it.
Meanwhile frames wait in the host, whose queue is bounded, not in QEMU's,
which never drops a frame that has a callback and would grow without
limit while the guest is slow. A frame can be up to `NET_BUFSIZE`
(4096 + 65536) bytes.

### Sleeping while idle

glib-wasi's `g_poll()` never waits, so when `main_loop_wait()`
(`util/main-loop.c`) may block, it sleeps until the next timer deadline,
at most 10 ms (single-threaded) or 30 ms (threaded), in the hook
`wasi_idle_wait` from patch 0004, or `nanosleep()` if that is NULL.
`wasi_user_idle_wait()` sleeps in `host_net_wait(ns)`, which returns as
soon as a frame for the guest arrives, so a reply wakes an idle guest at
once. While `blocked` it uses `nanosleep()`: waking for frames that cannot
be delivered would spin. In the single-threaded build an iteration is the
main loop (blocking only if the vCPU, the virtual CPU, is idle:
`main_loop_wait(!all_cpu_threads_idle())`), `wasi_net_poll()`, then a vCPU
slice (`QEMU_TB_SLICE_MS`, 30 ms by default); a frame arriving during the
slice waits for its end.

### The threaded build

- The main loop has a thread of its own and always blocks
  (`main_loop_wait(false)`), with the BQL (QEMU's global lock) released,
  for up to 30 ms; the cap is for host input, which only the refresh polls.
- Other threads end the sleep when they need the main loop: `aio_notify()`
  (`util/async.c`) calls `wasi_main_wake()`, which calls the
  `host_main_wake` import unless it runs on the main loop's thread
  (`wasi_main_thread`, set by `qemu_init_main_loop()`). That ends the
  current or next `host_net_wait()`, not a `nanosleep()`: without a user
  network, or while `blocked`, the main loop may oversleep by up to 30 ms.
- vCPU threads call `host_net_send()` too (guest writes to the e1000 run
  there, holding the BQL), so the host must be thread-safe. The refresh
  and `wasi_net_poll()` run on the main loop thread, also holding the BQL;
  `wasi_gfx_dpy_update()` can run on a vCPU thread when the guest writes
  to a serial port shown as a text console.
- Only this build imports `host_main_wake` (m-browser: `src/qemu_net.rs`).

## How to extend

Changes in `qemu-src/` must go into the patch series
([10-changing-qemu.md](10-changing-qemu.md)). Build and install:

```sh
# from the qemu-wasi root
make && make THREADS=1   # both flavors
make install             # the single-threaded one into m-browser
                         # (make THREADS=1 install: the threaded one)
```

### Add an input event type

Example: relative mouse motion, for a guest without a tablet.

1. Take the next unused type number, 6; never reuse one.
2. m-browser: push `InputEvent { event_type: 6, code, value }` onto the
   `GfxEventQueue` in `App::window_event()` (`src/app.rs`) and, if wanted,
   `src/headless.rs`; update the comment on `InputEvent`
   (`src/qemu_gfx.rs`). `host_gfx_poll_events` passes any type through.
3. QEMU: add `case 6:` to the switch in `wasi_gfx_display_poll_events()`,
   here `qemu_input_queue_rel(NULL, code == 0 ? INPUT_AXIS_X :
   INPUT_AXIS_Y, value)`, sent by the sync at the end of the poll. Pace it
   (the `type == 3 || type == 5` test) only if bursts could overflow a
   device buffer; range-check values that become QAPI enums
   ([Pitfalls](#pitfalls)).
4. Document it: the comment above `host_gfx_poll_events`,
   [reference/host-interface.md](reference/host-interface.md), this
   document and m-browser's docs/09-display-and-input.md.

Versions mix safely: QEMU ignores unknown types, and an older host never
sends the new one. Horizontal scrolling needs no new type, just button
codes 7 and 8 from the host.

### Change the scaling

The factor and cap are in `wasi_gfx_scale()` and `wasi_gfx_scale_for()`
(update the table above and
[reference/environment-variables.md](reference/environment-variables.md#display)).
Another algorithm (smooth, fractional) replaces the loop in
`wasi_gfx_display_render()`; keep `span` in
`wasi_gfx_display_poll_events()` equal to the size the host shows, or
pointer positions will be off. To change the factor at run time, make
`wasi_gfx_scale()` return it (every surface switch recomputes the scale)
and do what `wasi_gfx_display_resize()` does.

### Add a host import for the display

Example: `host_gfx_update_rect(ptr, len, x, y, w, h, stride)`, to send only
the changed part of a frame.

1. Declare it with the other `extern`s in `wasi_gfx_display.c`; linked
   with `-Wl,--allow-undefined`, it becomes an `env` import with no build
   change (as does a misspelt name).
2. Implement it in m-browser's `add_gfx_to_linker()` (`src/qemu_gfx.rs`),
   checking bounds with `with_guest()` and copying what it keeps.
3. Ship both together: a host lacking an import refuses the module
   ("unknown import"). Check with `wasm-tools print --skeleton`
   ([reference/host-interface.md](reference/host-interface.md#checking-a-builds-interface))
   and document it there and in m-browser's docs/reference/host-imports.md.
   Call it from the refresh path (the main loop thread).

### Support another network option

Example: `dns=` (the DNS server's address).

1. Teach the host first, for instance through a new import
   `host_net_config(key, key_len, value, value_len)` declared at the top
   of `wasi-user.c.inc` and implemented in m-browser's `src/qemu_net.rs`.
2. Remove the option from `wasi_user_unsupported_option()` and pass it on
   in `net_init_wasi_user()` before `qemu_new_net_client()`, like the
   `hostfwd` loop (`error_setg()` if the host refuses); keep the info
   string true.
3. The monitor's `hostfwd_add`, `hostfwd_remove` and `info usernet` are
   built only with `CONFIG_SLIRP` (`hmp-commands*.hx`).
4. On a QEMU upgrade, compare `NetdevUserOptions` (`qapi/net.json`) with
   the list: an option upstream adds would be silently accepted.

## Pitfalls

**Pitfall:** without an absolute pointing device (`-device qemu-xhci
-device usb-tablet`) QEMU finds no handler for positions and drops them;
buttons still reach the PS/2 mouse, wherever its pointer is.

**Pitfall:** this build makes no text consoles by default:
`qemu_display_get_vc()` returns NULL without `CONFIG_PIXMAN`, so there is
no default monitor and the default serial port goes to `null`. Ask for
them: `-monitor vc -serial vc`.

**Pitfall:** while the VM is stopped (`stop` in the monitor),
`qemu_input_event_send()` and `qemu_input_event_sync()` discard input for
the guest; text consoles still work.

**Pitfall:** button codes are passed on unchecked. A code outside 0..9
fails the assertion in `qapi_enum_lookup()`, reached through
`qemu_input_event_trace()`, and QEMU aborts.

**Pitfall:** text consoles take one byte per character
(`qemu_text_console_handle_keysym()` keeps the low 8 bits), so code points
above U+00FF arrive mangled; nothing is UTF-8 encoded.

**Pitfall:** refresh, input and frames only happen while the main loop
runs: not during start-up or a Store move, nor (single-threaded build)
during a vCPU slice. Input then waits and arrives paced.

## Debugging

| Tool | What it tells you |
|---|---|
| Always printed | `wasi-gfx: initializing display WxH (scale Nx -> WxH)`, `resize to ...`, the console list, `switching to ...`, `no qcode for evdev N` |
| `QEMU_DISPLAY_STATS=1` | What refreshes cost ([above](#measuring-qemu_display_stats)) |
| `WASI_DEBUG=1` | `wasi-gfx:` traces: the first updates, refreshes and renders, then every 60th; every key (`key evdev=N down=D qcode=Q ...`) and text event; `blit #N ... sample_sum=S` (0: the surface is all black). Plus much unrelated tracing |
| `WASI_GFX_FULL_REFRESH=1` | Rules out missed dirty updates when the screen looks stale |
| Monitor `info network` | The NIC, and `type=user,net=10.0.2.0/24, provided by the WASI host` |
| Monitor `info usb`, `info mice` | Whether the tablet exists; which pointing device gets events (`*`, `(absolute)`) |
| Monitor `sendkey ctrl-alt-f2` | Keys injected without the host: separates host from guest problems |
| m-browser `QEMU_SNAPSHOT=<file.png>` | The frames as a PNG, at most once a second (the monitor's `screendump` needs pixman and is not built) |
| m-browser `QEMU_NET_DEBUG=1` | The guest's network traffic |

To read the monitor without a window, run the headless runner on a copy of
the disk image, start on console 1 and type commands into it (times count
from m-browser's start, including loading QEMU), then look at the PNG:

```sh
# from the m-browser directory
mkdir -p ~/vm-test && cp m.qcow2 ~/vm-test/disk.qcow2
WASI_GFX_CONSOLE=1 QEMU_SNAPSHOT=/tmp/monitor.png QEMU_QUIT_AFTER=180 \
QEMU_TYPE='90:info network\n;95:info mice\n' \
./target/release/m-browser --qemu-headless --preopen "$HOME/vm-test=/out" \
    qemu-system-x86_64.wasm -L /bios \
    -drive file=/out/disk.qcow2,format=qcow2 \
    -device qemu-xhci -device usb-tablet -monitor vc -serial vc -nic user
```

In the guest, `ip addr` should show 10.0.2.15 and `ping -c1 10.0.2.2`
should answer. More in m-browser's docs/11-headless-and-automation.md and
[14-debugging.md](14-debugging.md).

## Known inaccuracies in comments

- `wasi_gfx_display.c`: the event list above `host_gfx_poll_events` lacks
  type 5; `host_gfx_init()` and `host_gfx_update_framebuffer()` are said
  to drive a GPU device, pipeline and texture (m-browser uses softbuffer);
  `wasi_gfx_display_blit()` is said to run "each frame" (now every 60th).
- `wasi_gfx_display.h` speaks of a GPU device, pipeline and "frame events"
  (the refresh timer drives everything) and omits text events;
  `wasi_gfx_display_shutdown()` would "Release all GPU and wasi-gfx
  resources" (it frees two buffers, and nothing calls it).
- `wasi_gfx_dcl.c`: the trailing comment (constructors, `module_init`, a
  "custom WASI main wrapper") describes plans; the comment for
  `wasi_gfx_display_register_dcl()` sits above `wasi_gfx_pick_console()`;
  "deferring init" defers nothing; `refresh_stats` assumes the guest CPU
  shares the main loop's thread (single-threaded build only).
- The standalone mode (no `QEMU_BUILD`) of `wasi_gfx_display.c` and its
  header uses `WASI_LOG()` and `wasi_debug_level()`, which only QEMU's
  headers define, so it seems no longer to compile (not tried).
