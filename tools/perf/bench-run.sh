#!/bin/bash
# bench-run.sh NAME [STEPS...]: run the guest benchmark guest/bench.sh and
# print its results, e.g. to compare two QEMU builds:
#
#   QEMU_WASM=~/old.wasm tools/perf/bench-run.sh old
#   QEMU_WASM=~/new.wasm tools/perf/bench-run.sh new forkexec find bzip2
#
# Boots a root shell with the network up (see start_shell in common.sh) on
# a throwaway overlay of WORK/BASE (default base.raw), serves tools/perf/guest/
# to the guest over HTTP on a free local port (the guest reaches the host's
# 127.0.0.1 as 10.0.2.2), and runs bench.sh with STEPS (default: all).
# Prints "BENCH <step> <seconds>" lines, measured with the guest's clock;
# the full guest log is WORK/serial-NAME.log.  Takes ~10 minutes with the
# chromium step.
source "$(dirname "$0")/common.sh"

name=$1
[ -n "$name" ] || die "usage: bench-run.sh NAME [STEPS...]"
shift
steps=$*
need_m_browser

port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$PERF_DIR/guest" > /dev/null 2>&1 &
http=$!
trap 'kill $http $QEMU_PID 2> /dev/null' EXIT

log "$name: booting a root shell on ${BASE:-base.raw}"
start_shell "$name" "${BASE:-base.raw}" user
log "$name: running bench.sh ${steps:-(all steps)}"
send "$name" "wget -q -O /tmp/bench.sh http://10.0.2.2:$port/bench.sh && sh /tmp/bench.sh $steps"
if ! wait_for "$WORK/serial-$name.log" "BENCH done" "${LIMIT:-3600}"; then
    log "$name: the benchmark did not finish (see $WORK/screen-$name.png)"
fi
grep -a '^BENCH' "$WORK/serial-$name.log" | grep -v done | sed "s/^/$name: /"
send "$name" "poweroff -f"
stop_qemu "$QEMU_PID" 60
rm -f "$WORK/k-$name.qcow2"
