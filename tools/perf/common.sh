# tools/perf/common.sh: settings and helpers shared by the tools/perf
# scripts.  It is sourced by them, not run.  Every setting can be
# overridden from the environment, for example:
#
#   WORK=/scratch/perf SMP=2 tools/perf/boot-timing.sh run1
#
# Settings
#   M_BROWSER  the m-browser checkout (default: m-browser next to qemu-wasi)
#   WORK       where disk images, logs and screenshots go
#              (default: tools/perf/work, which git ignores)
#   QEMU_WASM  the QEMU build to run (default: the one installed in
#              m-browser, i.e. what its GUI runs)
#   MEM        guest RAM in MiB (default 1024, as in m-browser's GUI)
#   SMP        number of vCPUs (default: m-browser's choice, which is 4 for
#              the multi-threaded build on a host with 8+ CPUs; the
#              single-threaded build always runs 1)
#
# Variables whose names start with QEMU_ or WASI_ also reach QEMU itself
# (m-browser forwards them), e.g. QEMU_JIT_HOT=1000.

PERF_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TOP=$(cd "$PERF_DIR/../.." && pwd)
M_BROWSER=${M_BROWSER:-$(cd "$TOP/.." && pwd)/m-browser}
WORK=${WORK:-$PERF_DIR/work}
QEMU_WASM=${QEMU_WASM:-$M_BROWSER/qemu-system-x86_64.wasm}
MEM=${MEM:-1024}
SMP=${SMP:-}
MB=$M_BROWSER/target/release/m-browser

mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)

log() { echo "[$(date +%H:%M:%S)] $*" >&2; }
die() { echo "error: $*" >&2; exit 1; }

# Check that m-browser and the QEMU build exist.
need_m_browser() {
    [ -x "$MB" ] || die "$MB not found: build m-browser first (cargo build --release in $M_BROWSER)"
    [ -f "$QEMU_WASM" ] || die "QEMU build $QEMU_WASM not found (set QEMU_WASM)"
    QEMU_WASM=$(cd "$(dirname "$QEMU_WASM")" && pwd)/$(basename "$QEMU_WASM")
}

# find_tool NAME: the path of a program, also looking in the sbin
# directories, which are often not on a normal user's PATH (debugfs).
find_tool() {
    command -v "$1" 2>/dev/null && return 0
    local d
    for d in /usr/sbin /sbin; do
        [ -x "$d/$1" ] && { echo "$d/$1"; return 0; }
    done
    return 1
}

# new_overlay NAME BASE: create k-NAME.qcow2 in WORK, an empty
# copy-on-write disk on top of WORK/BASE.  The guest's writes go to the
# overlay; BASE is never changed.  The backing file is recorded by its path
# inside QEMU (/out/BASE: run_qemu makes WORK visible to QEMU as /out), not
# by its host path, because QEMU is the one that opens it.  So qemu-img on
# the host cannot follow it (hence -u, "unsafe": do not check the backing
# file), and overlays only work through these scripts.
new_overlay() {
    local name=$1 base=$2 fmt=qcow2
    [ -f "$WORK/$base" ] || die "$WORK/$base not found (run prepare-base.sh first)"
    [ -f "$WORK/disk-size" ] || die "$WORK/disk-size not found (run prepare-base.sh first)"
    case $base in *.raw) fmt=raw ;; esac
    rm -f "$WORK/k-$name.qcow2"
    qemu-img create -q -f qcow2 -u -b "/out/$base" -F "$fmt" \
        "$WORK/k-$name.qcow2" "$(cat "$WORK/disk-size")" ||
        die "qemu-img create failed"
}

# run_qemu NAME QEMU-ARGS...: run QEMU in m-browser's headless runner, with
# WORK visible to QEMU as /out and m-browser's pc-bios/ as /bios.  The
# screen is saved to WORK/screen-NAME.png about once a second, lines
# written to WORK/type-NAME.fifo are typed into the guest (see send), and
# m-browser's output goes to WORK/run-NAME.log.
#
# Always run it in the background (run_qemu NAME ... &): it replaces the
# shell it runs in with m-browser (exec), so $! is m-browser's process id,
# which callers wait for or kill.
run_qemu() {
    local name=$1
    shift
    local smp=()
    [ -n "$SMP" ] && smp=(-smp "$SMP")
    [ -p "$WORK/type-$name.fifo" ] || mkfifo "$WORK/type-$name.fifo"
    cd "$M_BROWSER" || exit 1
    exec env QEMU_SNAPSHOT="$WORK/screen-$name.png" \
        QEMU_TYPE_FIFO="$WORK/type-$name.fifo" \
        "$MB" --qemu-headless --preopen "$WORK=/out" "$QEMU_WASM" \
        -L /bios -m "$MEM" "${smp[@]}" "$@" > "$WORK/run-$name.log" 2>&1
}

# send NAME LINE: type LINE, then Enter, into the running VM NAME.  Lines
# starting with "!" are pointer commands instead: "!move X Y" (in pixels of
# the screen as saved in screen-NAME.png), "!click B" (B: 0 left,
# 1 middle, 2 right), "!down B", "!up B".  Characters are typed at about
# 16 per second; the guest may drop keys while it is very busy.
send() {
    timeout 60 bash -c 'printf "%s\n" "$1" > "$2"' _ "$2" "$WORK/type-$1.fifo" ||
        log "could not type into $1 (is it running?)"
}

# wait_for FILE PATTERN SECONDS: wait until FILE contains PATTERN (a grep
# regular expression).  Fails after SECONDS.
wait_for() {
    local i=0
    until grep -aq -- "$2" "$1" 2>/dev/null; do
        sleep 2
        i=$((i + 2))
        [ $i -ge "$3" ] && return 1
    done
    return 0
}

# start_shell NAME BASE NIC: boot a root shell in the background: the
# guest's kernel (extracted by prepare-base.sh) is started directly with
# init=/bin/sh, so no password is needed, on a new overlay of WORK/BASE.
# Returns once the shell can take commands, with the root filesystem
# writable and, if NIC is "user", the network up.  Sets QEMU_PID.  The
# shell's output goes to the guest's screen; write to /dev/ttyS0 to get
# something into WORK/serial-NAME.log.
start_shell() {
    local name=$1 base=$2 nic=$3
    [ -f "$WORK/kernel" ] || die "$WORK/kernel not found (run prepare-base.sh first)"
    new_overlay "$name" "$base"
    rm -f "$WORK/serial-$name.log" "$WORK/screen-$name.png"
    run_qemu "$name" -drive "file=/out/k-$name.qcow2,format=qcow2" \
        -kernel /out/kernel -initrd /out/initramfs \
        -append "$(cat "$WORK/cmdline") rw init=/bin/sh console=ttyS0 console=tty0" \
        -serial "file:/out/serial-$name.log" -nic "$nic" &
    QEMU_PID=$!
    # Alpine's initramfs prints this when it has found the root filesystem.
    if ! wait_for "$WORK/serial-$name.log" "Mounting root: ok" 900; then
        kill "$QEMU_PID" 2>/dev/null
        die "$name: the guest did not boot (see $WORK/run-$name.log and screen-$name.png)"
    fi
    sleep 20
    send "$name" "mount -o remount,rw /"
    if [ "$nic" = user ]; then
        send "$name" "modprobe e1000 && ip link set lo up && ip link set eth0 up && udhcpc -i eth0 -q -n > /dev/null 2>&1; echo NET-UP > /dev/ttyS0"
        wait_for "$WORK/serial-$name.log" NET-UP 300 || log "$name: the network did not come up"
    fi
}

# stop_qemu PID SECONDS: wait up to SECONDS for QEMU to exit, then kill it.
stop_qemu() {
    local i=0
    while kill -0 "$1" 2>/dev/null; do
        sleep 2
        i=$((i + 2))
        if [ $i -ge "$2" ]; then
            kill "$1" 2>/dev/null
            sleep 2
            break
        fi
    done
}
