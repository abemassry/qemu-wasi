#!/bin/bash
# click-latency.sh NAME: measure how long Chromium takes to react to clicks
# in a VM started by boot-timing.sh NAME (run it at the same time, or set
# CLICKS=1 for boot-timing.sh).
#
# 1. Waits until Chromium's toolbar is painted: its menu button (three dots)
#    is on the screen (menupx.py find).  Meanwhile it moves the pointer once
#    a minute, so that X does not blank the screen.  Reports "window drawn
#    at T s", T counted from QEMU's start.
# 2. With WAIT_IDLE=1, also waits until the guest is idle (the guest
#    logger's top reports at least 80% idle).  Clicks made while Chromium
#    is still starting can be lost; this option avoids measuring that.
# 3. ROUNDS times (default 3): clicks the menu button, measures the time
#    until the menu is drawn (menupx.py count), clicking again if nothing
#    happened for a minute, then closes the menu by clicking on the desktop
#    (AWAY_X,AWAY_Y, default 1270,600: right of Chromium's window, assumed
#    to be bare desktop) and waits 40 s.
# The results go to WORK/click-NAME.log, and the screens with the menu open
# to WORK/clicks-NAME/.  Set MENU_X and MENU_Y to click a fixed position
# instead of the detected button.
source "$(dirname "$0")/common.sh"

name=$1
[ -n "$name" ] || die "usage: click-latency.sh NAME"
start=$(cat "$WORK/start-$name" 2> /dev/null) || start=$(date +%s)
screen=$WORK/screen-$name.png
serial=$WORK/serial-$name.log
out=$WORK/click-$name.log
shots=$WORK/clicks-$name
mkdir -p "$shots"
rm -f "$out"

el() { echo $(( $(date +%s) - start )); }
note() { echo "$name: $*" | tee -a "$out" >&2; }
running() { ! grep -aq "\[qemu\] exited\|QEMU exited" "$WORK/run-$name.log" 2> /dev/null; }
idle() { awk '/^CPU:/ { v = $8 + 0 } END { print v + 0 }' "$serial" 2> /dev/null || echo 0; }
button() { python3 "$PERF_DIR/menupx.py" find "$screen" 2> /dev/null; }
menu() { python3 "$PERF_DIR/menupx.py" count "$shots/before.png" "$screen" "$bx" "$by"; }
stop() { note "QEMU exited ($*)"; exit 1; }

# 1. Wait for the toolbar.
nudge=0
until pos=$(button) && [ -n "$pos" ]; do
    running || stop "before Chromium's window appeared"
    [ "$(el)" -gt "${LIMIT:-3600}" ] && { note "no Chromium window after $(el) s; giving up"; exit 1; }
    sleep 5
    if [ $(( $(el) / 60 )) -gt $nudge ]; then
        nudge=$(( $(el) / 60 ))
        send "$name" "!move ${AWAY_X:-1270} $(( 300 + nudge % 2 * 100 ))"
    fi
done
read -r bx by <<< "$pos"
bx=${MENU_X:-$bx} by=${MENU_Y:-$by}
note "window drawn at $(el) s (menu button at $bx,$by)"

# 2. Optionally wait until the guest is idle.
if [ "${WAIT_IDLE:-0}" = 1 ]; then
    while [ "$(idle)" -lt 80 ]; do
        running || stop "while waiting for the guest to settle"
        sleep 2
    done
    note "guest idle at $(el) s"
fi

# 3. Click rounds.
for round in $(seq 1 "${ROUNDS:-3}"); do
    sleep 20
    running || stop "before round $round"
    cp "$screen" "$shots/before.png"
    send "$name" "!move $bx $by"
    sleep 1
    send "$name" "!click 0"
    t0=$(date +%s)
    tc=$t0
    retries=0
    opened=0
    while running; do
        [ "$(menu)" -gt 1500 ] && { opened=1; break; }
        [ $(( $(date +%s) - t0 )) -gt 600 ] && break
        # As a user would: click again if nothing happened for a minute.
        if [ $(( $(date +%s) - tc )) -ge 60 ]; then
            send "$name" "!click 0"
            tc=$(date +%s)
            retries=$((retries + 1))
        fi
        sleep 1
    done
    running || stop "during round $round"
    if [ $opened = 0 ]; then
        note "round $round: no menu after $(( $(date +%s) - t0 )) s"
        continue
    fi
    note "round $round: menu after $(( $(date +%s) - t0 )) s, $retries extra clicks (at $(el) s)"
    cp "$screen" "$shots/menu$round.png"
    send "$name" "!move ${AWAY_X:-1270} ${AWAY_Y:-600}"
    sleep 1
    send "$name" "!click 0"
    t1=$(date +%s)
    while running && [ "$(menu)" -gt 300 ] && [ $(( $(date +%s) - t1 )) -lt 300 ]; do sleep 1; done
    note "round $round: menu closed after $(( $(date +%s) - t1 )) s"
    sleep 20
done
