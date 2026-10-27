#!/bin/bash
# mkbase.sh NAME NEW_BASE COMMANDS_FILE [BASE]: make a new base image in
# WORK by running shell commands in the guest as root.
#
# Boots a root shell (see start_shell in common.sh) on a new overlay of
# WORK/BASE (default base.raw), types each line of COMMANDS_FILE into it,
# then syncs and powers off, and keeps the overlay as WORK/NEW_BASE.  The
# root filesystem is already writable when the commands start, and there
# is no network.  NEW_BASE depends on BASE (it records only the changes),
# so keep BASE.
#
# Lines are typed exactly as written, one per line: here-documents work
# (cat > FILE << 'EOF' ... EOF), but avoid tab characters.  The commands'
# output goes to the guest's screen (WORK/screen-NAME.png); redirect to
# /dev/ttyS0 to get it into WORK/serial-NAME.log.
#
# Example: tools/perf/mkbase.sh flags fast-base.qcow2 tools/perf/guest/chromium-fast.cmds perf-base.qcow2
source "$(dirname "$0")/common.sh"

name=$1 new=$2 cmds=$3 base=${4:-base.raw}
[ -n "$name" ] && [ -n "$new" ] && [ -f "$cmds" ] ||
    die "usage: mkbase.sh NAME NEW_BASE COMMANDS_FILE [BASE]"
[ -e "$WORK/$new" ] && [ "${FORCE:-0}" != 1 ] && die "$WORK/$new exists (FORCE=1 replaces it)"
need_m_browser

log "$name: booting a root shell on $base"
start_shell "$name" "$base" none
log "$name: typing $(wc -l < "$cmds") lines from $cmds"
while IFS= read -r line || [ -n "$line" ]; do
    send "$name" "$line"
    # Let the typist keep pace: about 16 characters a second.
    sleep "$(( ${#line} / 16 + 1 ))"
done < "$cmds"
send "$name" "sync; echo MKBASE-DONE > /dev/ttyS0; sync; poweroff -f"
if ! wait_for "$WORK/serial-$name.log" MKBASE-DONE 1800; then
    kill "$QEMU_PID" 2> /dev/null
    die "$name: the commands did not finish (see $WORK/screen-$name.png)"
fi
stop_qemu "$QEMU_PID" 300
mv "$WORK/k-$name.qcow2" "$WORK/$new"
log "$name: created $WORK/$new"
