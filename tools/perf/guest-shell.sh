#!/bin/bash
# guest-shell.sh [NAME]: boot the guest into a root shell you can type into.
#
# The guest's kernel is started directly with init=/bin/sh (see start_shell
# in common.sh), so no password is needed: useful when the image's root
# password is unknown.  Runs on a throwaway overlay of WORK/BASE (default
# base.raw): nothing reaches the base image.  The root filesystem is made
# writable and the network brought up (NIC=none skips the network).
#
# While it runs, from another terminal:
#   echo 'ls /etc' > WORK/type-NAME.fifo      type a command (and Enter)
#   WORK/screen-NAME.png                      the guest's screen (1 s)
#   echo 'dmesg > /dev/ttyS0' > WORK/type-NAME.fifo
#                                             output into WORK/serial-NAME.log
# Stop with Ctrl+C here, or type "poweroff -f".
source "$(dirname "$0")/common.sh"

name=${1:-shell}
base=${BASE:-base.raw}
nic=${NIC:-user}
need_m_browser

trap 'kill $QEMU_PID 2> /dev/null; exit 1' INT TERM
log "$name: booting a root shell on $base"
start_shell "$name" "$base" "$nic"
cat << EOF
The root shell is ready.
  type:     echo 'COMMAND' > $WORK/type-$name.fifo
  screen:   $WORK/screen-$name.png
  serial:   $WORK/serial-$name.log (write to /dev/ttyS0 in the guest)
  stop:     Ctrl+C here, or type "poweroff -f"
EOF
wait "$QEMU_PID"
log "$name: QEMU exited"
