#!/usr/bin/env python3
"""analyze-serial.py LOG [FROM]: summarize a guest log written by the guest
logger (guest/qemu-profile.start), i.e. the serial port of a boot-timing.sh
run (WORK/serial-NAME.log).

Prints, in seconds of guest uptime (time since the guest's kernel started,
which is the time since QEMU started minus 10-30 s of firmware and
bootloader):
  - when X (Xorg), Fluxbox, Chromium and Chromium's first renderer process
    were first seen running (the logger samples every 10 s, so these are
    up to 10 s late);
  - when the guest was first at least 80% idle after FROM (default: the
    renderer's start), i.e. when Chromium had settled;
  - the average CPU use after FROM;
  - pages swapped in and out (non-zero means the guest is short of RAM);
  - the rate of local timer interrupts (the guest's timer tick), summed
    over all CPUs.
"""
import re
import sys

MILESTONES = [("Xorg", "Xorg"), ("fluxbox", "fluxbox"),
              ("chromium", "chromium"), ("renderer", "--type=renderer")]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    t = None                  # uptime of the latest sample
    first_t = last_t = None
    first = {}                # milestone -> uptime
    cpu = []                  # (uptime, usr, sys, idle, irq, sirq)
    counters = []             # (uptime, {name: value})
    loc = []                  # (uptime, local timer interrupts, all CPUs)
    irq_t = None
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = re.match(r"^=== interrupts at ([\d.]+)", line)
            if m:
                irq_t = float(m.group(1))
                continue
            m = re.match(r"^=== ([\d.]+) (.*)$", line)
            if m:
                t = float(m.group(1))
                first_t = t if first_t is None else first_t
                last_t = t
                words = m.group(2).split()
                values = {}
                for k, v in zip(words[0::2], words[1::2]):
                    if v.isdigit():
                        values[k] = int(v)
                counters.append((t, values))
                continue
            if line.startswith("=I= LOC:") and irq_t is not None:
                nums = [int(x) for x in line.split()[2:] if x.isdigit()]
                loc.append((irq_t, sum(nums)))
                continue
            if t is None:
                continue
            m = re.match(r"^CPU:\s+(\d+)% usr\s+(\d+)% sys\s+(\d+)% nic\s+"
                         r"(\d+)% idle\s+(\d+)% io\s+(\d+)% irq\s+(\d+)% sirq",
                         line)
            if m:
                v = [int(x) for x in m.groups()]
                cpu.append((t, v[0], v[1], v[3], v[5], v[6]))
                continue
            if re.match(r"^\s*\d+\s+\d+\s", line):   # a process, from top
                for key, needle in MILESTONES:
                    if key not in first and needle in line:
                        first[key] = t

    if first_t is None:
        sys.exit(f"{path}: no logger output (is the guest logger installed?)")
    since = float(sys.argv[2]) if len(sys.argv) > 2 else first.get("renderer", 0.0)

    print(f"{path}: guest uptime {first_t:.0f} s to {last_t:.0f} s")
    print("first seen (guest uptime, s):  " + "   ".join(
        f"{k} {first[k]:.0f}" if k in first else f"{k} -" for k, _ in MILESTONES))
    idle_at = next((s[0] for s in cpu if s[0] >= since and s[3] >= 80), None)
    print(f"first >= 80% idle after {since:.0f} s: "
          + (f"{idle_at:.0f} s" if idle_at is not None else "never"))
    after = [s for s in cpu if s[0] >= since]
    if after:
        n = len(after)
        avg = [sum(s[i] for s in after) / n for i in range(1, 6)]
        print(f"CPU after {since:.0f} s ({n} samples): usr {avg[0]:.0f}%  "
              f"sys {avg[1]:.0f}%  idle {avg[2]:.0f}%  irq {avg[3]:.0f}%  "
              f"sirq {avg[4]:.0f}%")
    if counters:
        last = counters[-1][1]
        print(f"swap: {last.get('pswpin', 0)} pages in, "
              f"{last.get('pswpout', 0)} pages out")
    rates = [(b[1] - a[1]) / (b[0] - a[0])
             for a, b in zip(loc, loc[1:]) if b[0] > a[0]]
    if rates:
        print("local timer interrupts per second (all CPUs, latest last): "
              + " ".join(f"{r:.0f}" for r in rates[-8:]))


if __name__ == "__main__":
    main()
