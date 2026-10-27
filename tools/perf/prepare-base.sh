#!/bin/bash
# prepare-base.sh [DISK]: set up WORK (see common.sh) for the other
# scripts, from a copy of the guest's disk image DISK (default: m-browser's
# m.qcow2).  DISK itself is only read.  Creates in WORK:
#
#   base.raw         a raw copy of DISK; every run boots from a throwaway
#                    overlay on top of it, so runs never change it
#   disk-size        its size in bytes
#   kernel, initramfs, cmdline
#                    the guest's kernel, initramfs and kernel command line
#                    (from its bootloader configuration, without "quiet"),
#                    for booting a root shell directly (guest-shell.sh)
#   perf-base.qcow2  base.raw plus the guest logger guest/qemu-profile.start,
#                    which boot-timing.sh needs (skipped with LOGGER=0)
#
# Needs qemu-img, debugfs (e2fsprogs) and python3.  Takes a few minutes:
# installing the logger boots the guest once.  Rerun with FORCE=1 to start
# over, e.g. after changing the disk image.
source "$(dirname "$0")/common.sh"

disk=${1:-$M_BROWSER/m.qcow2}
[ -f "$disk" ] || die "disk image $disk not found"
command -v qemu-img > /dev/null || die "qemu-img not found (install QEMU's tools, e.g. the qemu-utils package)"
DEBUGFS=$(find_tool debugfs) || die "debugfs not found (install e2fsprogs)"

if [ "${FORCE:-0}" = 1 ]; then
    rm -f "$WORK/base.raw" "$WORK/perf-base.qcow2"
fi

# 1. A raw copy of the disk.  Raw, so that debugfs can read the guest's
#    filesystems straight from the file.
if [ -f "$WORK/base.raw" ] && [ "$WORK/base.raw" -nt "$disk" ]; then
    log "using the existing $WORK/base.raw (FORCE=1 makes a new copy)"
else
    # m-browser holds an exclusive lock on the image while QEMU runs;
    # copying it then would give an inconsistent image.
    if command -v flock > /dev/null && ! flock -n -s "$disk" true; then
        die "$disk is in use (is m-browser running QEMU on it?)"
    fi
    log "copying $disk to $WORK/base.raw"
    qemu-img convert -O raw "$disk" "$WORK/base.raw.tmp" || die "qemu-img convert failed"
    mv "$WORK/base.raw.tmp" "$WORK/base.raw"
    rm -f "$WORK/perf-base.qcow2"
fi
stat -c %s "$WORK/base.raw" > "$WORK/disk-size"

# 2. Find the partitions (an MBR partition table, as Alpine's installer
#    writes for BIOS machines).
python3 - "$WORK/base.raw" > "$WORK/partitions" << 'EOF' || die "cannot read the partition table"
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
if mbr[510:512] != b"\x55\xaa":
    sys.exit("no MBR partition table")
for i in range(4):
    entry = mbr[446 + 16 * i:462 + 16 * i]
    ptype = entry[4]
    start, count = struct.unpack("<II", entry[8:16])
    if ptype == 0xEE:
        sys.exit("GPT partition tables are not supported by prepare-base.sh")
    if ptype:
        print(i + 1, "%02x" % ptype, start, count)
EOF
log "partitions (number, type, first sector, sectors):"
sed 's/^/    /' "$WORK/partitions" >&2

# 3. Find the bootloader configuration (extlinux.conf, in the root of a
#    separate /boot partition or in /boot) and extract the kernel, the
#    initramfs and the kernel command line it uses.
conf= offset= dir=
while read -r num type start count; do
    [ "$type" = 83 ] || continue        # Linux partitions
    for d in / /boot/; do
        c=$("$DEBUGFS" -c -R "cat ${d}extlinux.conf" "$WORK/base.raw?offset=$((start * 512))" 2> /dev/null)
        if [ -n "$c" ]; then
            conf=$c offset=$((start * 512)) dir=$d
            break 2
        fi
    done
done < "$WORK/partitions"
[ -n "$conf" ] || die "no extlinux.conf found in the image's Linux partitions"
log "bootloader configuration: ${dir}extlinux.conf at byte offset $offset"

# The first LABEL's LINUX, INITRD and APPEND lines.
field() { printf '%s\n' "$conf" | awk -v k="$1" 'toupper($1) == k { $1 = ""; sub(/^ /, ""); print; exit }'; }
linux=$(field LINUX)
initrd=$(field INITRD)
append=$(field APPEND)
[ -n "$linux" ] && [ -n "$initrd" ] || die "no LINUX/INITRD lines in extlinux.conf"
case $linux in /*) ;; *) linux=$dir$linux ;; esac
case $initrd in /*) ;; *) initrd=$dir$initrd ;; esac

"$DEBUGFS" -c -R "cat $linux" "$WORK/base.raw?offset=$offset" > "$WORK/kernel" 2> /dev/null
"$DEBUGFS" -c -R "cat $initrd" "$WORK/base.raw?offset=$offset" > "$WORK/initramfs" 2> /dev/null
[ -s "$WORK/kernel" ] && [ -s "$WORK/initramfs" ] || die "could not extract $linux and $initrd"
# "quiet" would hide the boot messages the scripts wait for.
printf '%s\n' "$append" | sed -E 's/(^| )quiet( |$)/ /g; s/  +/ /g; s/^ //; s/ $//' > "$WORK/cmdline"
log "kernel $linux, initramfs $initrd"
log "kernel command line: $(cat "$WORK/cmdline")"

# 4. The base for boot-timing.sh: the guest logger installed.  The commands
#    that install it are generated from guest/qemu-profile.start.
if [ "${LOGGER:-1}" = 1 ]; then
    if [ -f "$WORK/perf-base.qcow2" ]; then
        log "using the existing $WORK/perf-base.qcow2"
    else
        {
            echo "mkdir -p /etc/local.d"
            echo "cat > /etc/local.d/qemu-profile.start << 'QEMU_PROFILE_EOF'"
            cat "$PERF_DIR/guest/qemu-profile.start"
            echo "QEMU_PROFILE_EOF"
            echo "chmod +x /etc/local.d/qemu-profile.start"
            echo "rc-update add local default"
            echo "cat /etc/local.d/qemu-profile.start > /dev/ttyS0"
        } > "$WORK/install-logging.cmds"
        log "installing the guest logger into perf-base.qcow2 (boots the guest; a few minutes)"
        "$PERF_DIR/mkbase.sh" logger perf-base.qcow2 "$WORK/install-logging.cmds" base.raw ||
            die "installing the logger failed"
    fi
fi
log "done: $WORK is ready"
