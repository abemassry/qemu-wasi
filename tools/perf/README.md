# tools/perf: measuring boots, clicks and guest workloads

These scripts boot the guest the way m-browser does, but headless (no
window), on throwaway copies of the disk image, and measure how fast it
gets to the desktop, how quickly Chromium answers clicks, and how long
typical workloads take inside the guest.  Use them to compare two QEMU
builds or settings before and after a change.  Read
[docs/15-performance.md](../../docs/15-performance.md) for what was
learned with them, and [docs/16-guest-image.md](../../docs/16-guest-image.md)
for the disk image they work on.

They drive m-browser's headless runner (see the m-browser repository's
docs/11-headless-and-automation.md): QEMU runs exactly as in the GUI, its
screen is saved to a PNG about once a second, and lines written to a named
pipe are typed into the guest.

## Contents

| File | What it does |
|---|---|
| `common.sh` | Settings and helpers shared by the scripts (sourced) |
| `prepare-base.sh [DISK]` | Once: copy the disk image, extract its kernel, install the guest logger |
| `boot-timing.sh NAME` | Boot like the GUI; record the guest log and screenshots; summarize |
| `click-latency.sh NAME` | Measure how long Chromium takes to open its menu after a click |
| `bench-run.sh NAME [STEPS]` | Run the guest benchmark `guest/bench.sh` and print its timings |
| `guest-shell.sh [NAME]` | Boot a root shell you can type into (no password needed) |
| `mkbase.sh NAME NEW_BASE COMMANDS_FILE [BASE]` | Run commands in the guest as root and keep the result as a new base image |
| `analyze-serial.py LOG [FROM]` | Summarize a guest log (milestones, idle time, CPU, swap, timer interrupts) |
| `menupx.py` | Find Chromium's menu button on a screenshot; tell whether its menu is open |
| `guest/qemu-profile.start` | The guest logger, installed into the guest by `prepare-base.sh` |
| `guest/chromium-fast.cmds` | Commands that apply the Chromium speed-ups to an image (for `mkbase.sh`) |
| `guest/bench.sh` | The guest benchmark (runs inside the guest) |

## Requirements

- m-browser built (`cargo build --release` in the m-browser repository),
  next to this repository (or set `M_BROWSER`).
- A QEMU build: by default the one installed in m-browser
  (`qemu-system-x86_64.wasm`); set `QEMU_WASM` to test another.
- The guest disk image, by default m-browser's `m.qcow2`.
- On the host: `qemu-img` (Debian/Ubuntu package `qemu-utils`), `debugfs`
  (package `e2fsprogs`; often in `/usr/sbin`, which the scripts also
  search), `python3`, `flock` (package `util-linux`), and for
  `click-latency.sh` the Python imaging library Pillow (`python3-pil`).
- Disk space: about 2 GB in `WORK` for the raw copy of the image, plus a
  few hundred MB per run while it runs.

## Quick start

Run from the qemu-wasi directory:

```sh
tools/perf/prepare-base.sh                    # once, ~5 minutes
CLICKS=1 tools/perf/boot-timing.sh run1       # ~15 minutes (DURATION=900)
```

The end of the output looks like this (numbers vary with the machine):

```text
[11:53:01] run1: QEMU exited after 660 s
.../serial-run1.log: guest uptime 186 s to 570 s
first seen (guest uptime, s):  Xorg 198   fluxbox 220   chromium 220   renderer 316
first >= 80% idle after 316 s: 350 s
CPU after 316 s (24 samples): usr 1%  sys 0%  idle 90%  irq 4%  sirq 2%
swap: 0 pages in, 0 pages out
local timer interrupts per second (all CPUs, latest last): 76 81 109 66 46 38 50 32
run1: window drawn at 382 s (menu button at 1177,100)
run1: round 1: menu after 15 s, 0 extra clicks (at 419 s)
run1: round 1: menu closed after 2 s
...
```

To compare two QEMU builds, run them one after the other, not at the same
time (two VMs slow each other down):

```sh
QEMU_WASM=/path/to/before.wasm CLICKS=1 tools/perf/boot-timing.sh before
QEMU_WASM=/path/to/after.wasm  CLICKS=1 tools/perf/boot-timing.sh after
```

## Settings

All settings are environment variables; `common.sh` documents them.

| Variable | Default | Meaning |
|---|---|---|
| `WORK` | `tools/perf/work` | Where images, logs and screenshots go (git ignores it) |
| `M_BROWSER` | `../m-browser` (next to qemu-wasi) | The m-browser checkout |
| `QEMU_WASM` | m-browser's `qemu-system-x86_64.wasm` | The QEMU build to run |
| `MEM` | `1024` | Guest RAM in MiB (as in m-browser's GUI) |
| `SMP` | m-browser's choice | vCPUs: 4 for the multi-threaded build on hosts with 8+ CPUs, else 2; the single-threaded build always runs 1 |
| `BASE` | `perf-base.qcow2` (`base.raw` for shells and benchmarks) | The base image in `WORK` to boot from |
| `DURATION` | `900` | `boot-timing.sh`: seconds before QEMU is asked to quit |
| `SHOTS` | `60` | `boot-timing.sh`: seconds between saved screenshots |
| `KBOOT`, `KAPPEND` | `0`, empty | `boot-timing.sh`: start the kernel directly, with extra kernel options |
| `CLICKS` | `0` | `boot-timing.sh`: also run `click-latency.sh` |
| `ROUNDS`, `WAIT_IDLE`, `MENU_X`, `MENU_Y`, `AWAY_X`, `AWAY_Y` | `3`, `0`, detected, detected, `1270`, `600` | `click-latency.sh` (see the script) |
| `NIC` | `user` | `guest-shell.sh`: `none` for no network |
| `FORCE`, `LOGGER` | `0`, `1` | `prepare-base.sh`: redo everything; skip the logger |
| `LIMIT` | `3600` | `bench-run.sh`, `click-latency.sh`: give up after this many seconds |

Variables whose names start with `QEMU_` or `WASI_` also reach QEMU and
m-browser, for example `QEMU_JIT_HOT=1000` or `QEMU_TB_SLICE_MS=10`
(see [docs/reference/environment-variables.md](../../docs/reference/environment-variables.md)).

## The work directory

| File | Made by | Contents |
|---|---|---|
| `base.raw` | `prepare-base.sh` | Raw copy of the disk image; never changed by runs |
| `disk-size`, `partitions` | `prepare-base.sh` | Its size; its partition table |
| `kernel`, `initramfs`, `cmdline` | `prepare-base.sh` | The guest's kernel, initramfs and command line (without `quiet`) |
| `perf-base.qcow2` | `prepare-base.sh` | `base.raw` plus the guest logger |
| `k-NAME.qcow2` | every run | The run's throwaway overlay (deleted by `bench-run.sh`; others leave it for inspection) |
| `run-NAME.log` | every run | m-browser's and QEMU's output |
| `serial-NAME.log` | every run | The guest's serial port (kernel messages, the logger, `BENCH` lines) |
| `screen-NAME.png` | every run | The guest's screen, about once a second |
| `type-NAME.fifo` | every run | Named pipe: write a line to type it into the guest |
| `start-NAME`, `shots-NAME/` | `boot-timing.sh` | Start time; screenshots named by seconds since the start |
| `click-NAME.log`, `clicks-NAME/` | `click-latency.sh` | Results; screens with the menu open |

**Pitfall:** an overlay records its base as `/out/BASE`, the path inside
QEMU (the scripts make `WORK` visible to QEMU as `/out`).  So `qemu-img` on
the host cannot follow the chain, and overlays only work through these
scripts.  [docs/16-guest-image.md](../../docs/16-guest-image.md) explains
how to turn an overlay into a standalone image.

## The scripts in detail

### prepare-base.sh

1. Copies the disk image (default: m-browser's `m.qcow2`) to
   `WORK/base.raw`, unless a copy newer than the image exists.  It refuses
   while m-browser has the image locked (QEMU running on it).
2. Reads the MBR partition table (GPT is not supported) and finds the
   partition holding `extlinux.conf`.
3. Extracts the kernel and initramfs named in its first `LABEL`, and the
   kernel command line (its `APPEND` line, without `quiet`).
4. Unless `LOGGER=0`: installs the guest logger into a new overlay,
   `perf-base.qcow2`, using `mkbase.sh` (the commands are generated from
   `guest/qemu-profile.start`).

### boot-timing.sh NAME

Boots `BASE` (default `perf-base.qcow2`) with m-browser's GUI arguments
(`-m 1024 -accel tcg,tb-size=256 -device qemu-xhci -device usb-tablet
-nic user`, the serial port to a file) for `DURATION` seconds, saving a
screenshot every `SHOTS` seconds, then prints the summary of
`analyze-serial.py` and, with `CLICKS=1`, the click results.

### click-latency.sh NAME

1. Waits until Chromium's toolbar is painted: `menupx.py find` sees the
   three-dot menu button.  Reports "window drawn at T s" (T since QEMU's
   start).  It moves the pointer once a minute meanwhile, because X blanks
   the screen after 10 minutes without input.
2. With `WAIT_IDLE=1`, waits until the guest is at least 80% idle.
3. `ROUNDS` times: clicks the button, measures the time until the menu is
   drawn (`menupx.py count`: new dark text pixels where the menu opens),
   clicks again if nothing happened for a minute (as a user would), closes
   the menu by clicking on the desktop to the right of Chromium's window.

**Note:** this matches the image as it is.  `menupx.py find` looks for
Chromium's light theme at 100% zoom, and the menu is closed by a click at
`AWAY_X`,`AWAY_Y` (1270,600), assumed to be bare desktop to the right of
Chromium's window on the 1280x800 screen.  Chromium remembers its window
size, so the button is searched for, not assumed.

### bench-run.sh NAME [STEPS]

Boots a root shell (like `guest-shell.sh`) with the network up, serves
`guest/` over HTTP on a free local port (the guest reaches the host's
127.0.0.1 as 10.0.2.2), and runs `guest/bench.sh`.  Steps: `forkexec`
(starting 300 processes), `find` (walking `/usr`), `bzip2` (compressing
3 MB), `python` (a Python loop), `chromium` (headless Chromium loading
about:blank; ~8 minutes).  Results are measured with the guest's clock:

```text
b1: BENCH forkexec 19.4
b1: BENCH find 17.2
b1: BENCH bzip2 17.0
b1: BENCH python 8.6
b1: BENCH total 63.2
```

### guest-shell.sh [NAME]

Boots the image's own kernel with `init=/bin/sh`: a root shell, no
password, no services.  The root filesystem is remounted writable and the
network brought up (`NIC=none` skips it).  Type commands from another
terminal with `echo 'ls /' > WORK/type-NAME.fifo`, look at
`WORK/screen-NAME.png`, and redirect output to `/dev/ttyS0` to get it into
`WORK/serial-NAME.log`.  Nothing reaches the base image.

### mkbase.sh NAME NEW_BASE COMMANDS_FILE [BASE]

Like `guest-shell.sh` without network: types each line of `COMMANDS_FILE`
into the root shell, then syncs, powers off, and keeps the overlay as
`WORK/NEW_BASE` (which depends on `BASE`).  Example, applying the Chromium
speed-ups to an image that lacks them:

```sh
tools/perf/mkbase.sh fast fast-base.qcow2 tools/perf/guest/chromium-fast.cmds perf-base.qcow2
BASE=fast-base.qcow2 CLICKS=1 tools/perf/boot-timing.sh fast1
```

**Pitfall:** lines are typed character by character with a US keyboard
layout, at about 16 characters a second: avoid tab characters and
characters a US keyboard cannot type.  A line starting with `!` would be
taken as a pointer command.

### analyze-serial.py LOG [FROM]

Reads a guest log written by the logger and prints when Xorg, Fluxbox,
Chromium and Chromium's first renderer process were first seen (guest
uptime; the logger samples every 10 s), when the guest was first at least
80% idle after `FROM` (default: the renderer's start), the average CPU use
after `FROM`, pages swapped, and the rate of local timer interrupts.

## The guest logger and its log format

`guest/qemu-profile.start` runs at the end of boot (OpenRC's `local`
service) and writes to the serial port:

```text
=== 374.76 intr 244502 ctxt 131830 processes 2068 pswpin 0 pswpout 0 pgfault 105762 pgmajfault 231
Mem: 319776K used, 160248K free, 8268K shrd, 5140K buff, 259728K cached
CPU:  20% usr  42% sys   0% nic   0% idle   0% io  34% irq   2% sirq
Load average: 3.12 4.72 2.80 3/79 2063
  PID  PPID USER     STAT   VSZ %VSZ CPU %CPU COMMAND
 2047  1987 m        R     8880   2%   0  36% fluxbox
...
=== interrupts at 374.76
=I=            CPU0       CPU1       CPU2       CPU3
=I= LOC:      55377      54012      ...   Local timer interrupts
```

- `===` lines: guest uptime and the kernel's counters (interrupts, context
  switches, processes started, pages swapped in and out, page faults),
  every 10 s.
- top's summary and busiest processes, every 10 s.
- `/proc/interrupts` with `=I=` in front, every 40 s.  `LOC` is the local
  timer interrupt (the guest's timer tick); `RES`, `CAL` and `TLB` are
  interrupts between CPUs.

The logger only runs in images made by `prepare-base.sh`, never in
m-browser's `m.qcow2`.

## Measuring well

- Run one VM at a time.  Two VMs, a build, or any busy program slows the
  others down.
- Keep the host's conditions the same: a laptop on battery can be 30-50%
  slower than on mains power.  Compare against a baseline measured in the
  same session, not against numbers from another day.
- Repeat runs: expect differences of 5-10% between identical runs.
- The first run after changing the QEMU build compiles QEMU's module once
  (about a second, cached in `~/.cache/m-browser`); compiled translation
  blocks are not kept between runs, so every run starts equally cold.
- Guest uptime starts when the guest's kernel starts, 10-30 s after QEMU
  (firmware and bootloader); the click script's times count from QEMU's
  start.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `... not found (run prepare-base.sh first)` | `WORK` not prepared, or another `WORK` | Run `prepare-base.sh` with the same `WORK` |
| `debugfs not found` | e2fsprogs missing | Install e2fsprogs |
| `... is in use` | m-browser is running QEMU on the image | Close it, or pass another image to `prepare-base.sh` |
| `the guest did not boot` | Wrong kernel command line, or another kind of image | Look at `WORK/screen-NAME.png` and `WORK/cmdline` |
| `could not type into NAME` | The VM is not running (it exited, or crashed) | See `WORK/run-NAME.log` |
| No menu found, or "no Chromium window" | Different Chromium theme, zoom or window layout | Set `MENU_X`/`MENU_Y`; look at `WORK/screen-NAME.png` |
| No `===` lines in `serial-NAME.log` | Booted an image without the logger | Use `perf-base.qcow2` (the default `BASE`) |
