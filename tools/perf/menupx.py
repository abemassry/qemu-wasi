#!/usr/bin/env python3
"""menupx.py: find Chromium's menu button on a screenshot, and tell whether
its menu is open.  Used by click-latency.sh.

  menupx.py find SCREEN.png
      Prints "X Y", the centre of Chromium's three-dot menu button (the
      rightmost one on the screen), or nothing (exit status 1) when there
      is none, e.g. while Chromium's toolbar is not painted yet.
  menupx.py count BEFORE.png NOW.png X Y
      Counts the pixels in the area where the menu of the button at X,Y
      opens (below it and to its left) that are dark now, as the menu's
      text is, but were not dark in BEFORE (the screen just before the
      click).  Prints the count: a few thousand once the menu is drawn, 0
      while it is closed or not painted yet (an unpainted menu is white).
      Prints -1 if an image cannot be read.

The button is three dark dots, each about 2 pixels across, one above the
other about 5.5 pixels apart, on a light toolbar; this matches Chromium's
default light theme at 100% zoom.  Needs Pillow (Debian/Ubuntu: apt install
python3-pil).
"""
import sys

from PIL import Image


def find(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()

    def dark(x, y):
        return sum(px[x, y]) < 300

    def light(x, y):
        return sum(px[x, y]) > 600

    # From the right: the menu button is the rightmost icon of the toolbar.
    for x in range(w - 5, 4, -1):
        for y in range(20, min(h - 12, 260)):
            if (dark(x, y) and dark(x, y + 6) and dark(x, y + 11)
                    and light(x, y + 3) and light(x, y + 9)
                    and light(x - 4, y + 6) and light(x + 4, y + 6)
                    and light(x, y - 3) and light(x, y + 14)):
                return x, y + 6
    return None


def count(before_path, now_path, x, y):
    try:
        before = Image.open(before_path).convert("RGB")
        now = Image.open(now_path).convert("RGB")
    except Exception:
        return -1
    w, h = now.size
    box = (max(0, x - 310), min(h, y + 23), min(w, x + 9), min(h, y + 673))
    pairs = zip(before.crop(box).getdata(), now.crop(box).getdata())
    return sum(1 for p, q in pairs if sum(q) < 300 and sum(p) >= 450)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "find":
        pos = find(sys.argv[2])
        if pos is None:
            sys.exit(1)
        print(*pos)
    elif len(sys.argv) == 6 and sys.argv[1] == "count":
        print(count(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
