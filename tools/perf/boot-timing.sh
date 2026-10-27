#!/bin/bash
# boot-timing.sh NAME: boot the guest the way m-browser's GUI does, and
# record how far it gets and how fast.
#
# Runs QEMU headless with the GUI's arguments (1 GiB, tb-size=256, USB
# tablet, user network) on a throwaway overlay of WORK/BASE (default
# perf-base.qcow2, which has the guest logger; see prepare-base.sh), for
# DURATION seconds (default 900), then asks QEMU to quit.  Output in WORK:
#   serial-NAME.log   the guest logger's output (summarized at the end by
#                     analyze-serial.py: when X, Fluxbox and Chromium
#                     started, CPU use, swapping, timer interrupts)
#   run-NAME.log      m-browser's and QEMU's output
#   screen-NAME.png   the screen, updated about once a second
#   shots-NAME/       a copy of the screen every SHOTS seconds (default 60),
#                     named by seconds since the start
#   start-NAME        when QEMU started (seconds since 1970)
#
# Options (environment variables): QEMU_WASM, MEM, SMP (see common.sh),
# BASE, DURATION, SHOTS,
#   KBOOT=1        start the guest's kernel directly instead of through its
#                  bootloader, with its usual command line plus KAPPEND
#                  (e.g. KAPPEND="mitigations=off"); saves ~10 s of boot
#   CLICKS=1       also run click-latency.sh NAME (measures how fast
#                  Chromium answers clicks once its window is drawn)
# plus any QEMU_*/WASI_* variable for QEMU or m-browser (QEMU_JIT_HOT=...).
#
# Example: compare two QEMU builds, one at a time (runs in parallel slow
# each other down):
#   QEMU_WASM=~/old.wasm tools/perf/boot-timing.sh old
#   QEMU_WASM=~/new.wasm tools/perf/boot-timing.sh new
source "$(dirname "$0")/common.sh"

name=$1
[ -n "$name" ] || die "usage: boot-timing.sh NAME"
base=${BASE:-perf-base.qcow2}
duration=${DURATION:-900}
shots=${SHOTS:-60}
need_m_browser

boot=()
if [ "${KBOOT:-0}" = 1 ]; then
    [ -f "$WORK/kernel" ] || die "$WORK/kernel not found (run prepare-base.sh first)"
    boot=(-kernel /out/kernel -initrd /out/initramfs -append "$(cat "$WORK/cmdline") ${KAPPEND:-}")
fi

new_overlay "$name" "$base"
rm -rf "$WORK/serial-$name.log" "$WORK/screen-$name.png" "$WORK/shots-$name" "$WORK/click-$name.log"
mkdir -p "$WORK/shots-$name"
start=$(date +%s)
echo "$start" > "$WORK/start-$name"
log "$name: booting $(basename "$QEMU_WASM") on $base for $duration s (MEM=$MEM SMP=${SMP:-default})"
(
    export QEMU_QUIT_AFTER=$duration
    run_qemu "$name" -drive "file=/out/k-$name.qcow2,format=qcow2" "${boot[@]}" \
        -accel tcg,tb-size=256 -device qemu-xhci -device usb-tablet \
        -monitor vc -serial "file:/out/serial-$name.log" -nic user
) &
qpid=$!
trap 'kill $qpid 2> /dev/null; exit 1' INT TERM

if [ "${CLICKS:-0}" = 1 ]; then
    # Its results are printed at the end (from WORK/click-NAME.log).
    "$PERF_DIR/click-latency.sh" "$name" 2> /dev/null &
fi

# A copy of the screen every SHOTS seconds, while QEMU runs.
while kill -0 "$qpid" 2> /dev/null; do
    sleep "$shots"
    elapsed=$(( $(date +%s) - start ))
    [ -f "$WORK/screen-$name.png" ] &&
        cp "$WORK/screen-$name.png" "$WORK/shots-$name/$(printf %05d "$elapsed").png"
done
wait "$qpid"
log "$name: QEMU exited after $(( $(date +%s) - start )) s"
grep -a "error\|failed\|panicked" "$WORK/run-$name.log" | head -5 >&2
python3 "$PERF_DIR/analyze-serial.py" "$WORK/serial-$name.log"
[ -f "$WORK/click-$name.log" ] && cat "$WORK/click-$name.log"
