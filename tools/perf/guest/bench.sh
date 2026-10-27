#!/bin/sh
# bench.sh [STEPS...]: time some typical workloads in the guest and report
# each on the serial port as "BENCH <step> <seconds>", then "BENCH done".
# Run as root in a root shell (bench-run.sh does that: it boots
# guest-shell's kind of shell and fetches this file over the network).
# Steps (default: all, in this order):
#   forkexec  start 300 processes (fork and exec: the kernel's page tables)
#   find      walk /usr (file system metadata, disk reads)
#   bzip2     compress 3 MB (plain computation)
#   python    a Python loop (an interpreter)
#   chromium  start headless Chromium and load about:blank (a big program)
out=/dev/ttyS0
mountpoint -q /proc || mount -t proc proc /proc
mountpoint -q /sys || mount -t sysfs sys /sys
mkdir -p /dev/shm && { mountpoint -q /dev/shm || mount -t tmpfs shm /dev/shm; }
mountpoint -q /tmp || mount -t tmpfs tmp /tmp
# Chromium reserves more memory than a small guest has, which the kernel
# refuses without swap (as in the desktop session, where swap is on).
swapon -a 2> /dev/null || swapon /dev/sda2 2> /dev/null
now() { cut -d' ' -f1 /proc/uptime; }
run() {
    name=$1
    shift
    s=$(now)
    "$@" > /dev/null 2>&1
    e=$(now)
    echo "BENCH $name $(awk "BEGIN { printf \"%.1f\", $e - $s }")" > $out
}
step() {
    case $1 in
    forkexec) run forkexec sh -c 'i=0; while [ $i -lt 300 ]; do /bin/true; i=$((i + 1)); done' ;;
    find) run find find /usr -xdev ;;
    bzip2)
        # The input of the measurements in docs/15-performance.md, if there.
        f=/usr/lib/chromium/chrome
        [ -f $f ] || f=$(ls -S /usr/lib/*.so* /usr/bin/* 2> /dev/null | head -1)
        run bzip2 sh -c "head -c 3000000 $f | bzip2 -c"
        ;;
    python)
        if command -v python3 > /dev/null; then
            run python python3 -c 'print(sum(i * i for i in range(200000)))'
        else
            echo "BENCH python skipped" > $out
        fi
        ;;
    chromium)
        if command -v chromium > /dev/null; then
            run chromium timeout 900 chromium --headless --no-sandbox --disable-gpu --no-first-run \
                --user-data-dir=/tmp/chromium --dump-dom about:blank
        else
            echo "BENCH chromium skipped" > $out
        fi
        ;;
    *) echo "BENCH $1 unknown" > $out ;;
    esac
}
start=$(now)
for s in ${*:-forkexec find bzip2 python chromium}; do
    step "$s"
done
echo "BENCH total $(awk "BEGIN { printf \"%.1f\", $(now) - $start }")" > $out
echo "BENCH done" > $out
