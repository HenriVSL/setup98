#!/usr/bin/env python3
"""cropshots.py - turns raw 800 x 600 Win98 screen captures (VNC framebuffer captures, 1:1 pixels) into the README
screenshots in docs/screenshots: crops each to its window plus a 10 px margin of desktop and saves it as a palette
PNG when it has at most 256 colours (exactly, no dithering), else as RGB. Lossless either way.
Usage: python3 tools/cropshots.py <raw capture folder> docs/screenshots
The raw names and boxes below match the capture session described in AGENTS.md (wizard dragged to x=260, y=99)."""
import sys, os
from PIL import Image
S = sys.argv[1]; OUT = sys.argv[2]
if not os.path.isdir(OUT): os.makedirs(OUT)
M = 10
wiz = (260 - M, 99 - M, 763 + M, 454 + M)
jobs = [
  ("hero.png",            "rs1_welcome.png", (0, 0, 800, 600)),
  ("rs-welcome.png",      "rs1_welcome.png", wiz),
  ("rs-license.png",      "rs2_license.png", wiz),
  ("rs-folder.png",       "rs3_folder.png", wiz),
  ("rs-installing.png",   "inst_a.png", wiz),
  ("rs-finish.png",       "rs5_finish.png", wiz),
  ("rs-upgrade-rc.png",   "rc_upgrade.png", wiz),
  ("pica-filetypes.png",  "p2_types.png", wiz),
  ("rs-startmenu.png",    "start4.png", (174, 274 - M, 490 + M, 540 + M)),
  ("rs-addremove.png",    "arp3.png", (310 - M, 89 - M, 677 + M, 549 + M)),
  ("rs-uninstall-data.png", "un2.png", (158, 232, 648, 382)),
  ("demo.png",            "demo2.png", (260 - M, 120 - M, 740 + M, 400 + M)),
]
for out, src, box in jobs:
    im = Image.open(os.path.join(S, src)).convert("RGB").crop(box)
    cols = im.getcolors(1 << 20)
    if len(cols) <= 256:
        pal_img = Image.new("P", (1, 1))
        colours = [c for _, c in sorted(cols, key=lambda t: -t[0])]
        flat = [v for c in colours for v in c]
        pal_img.putpalette(flat + [0] * (768 - len(flat)))
        idx = dict((c, i) for i, c in enumerate(colours))
        p = Image.new("P", im.size)
        p.putpalette(flat + [0] * (768 - len(flat)))
        p.putdata([idx[c] for c in im.getdata()])
        assert list(p.convert("RGB").getdata()) == list(im.getdata()), out
        kind = "palette, lossless"
    else:
        p = im
        kind = "RGB, lossless (%d colours)" % len(cols)
    path = os.path.join(OUT, out)
    p.save(path, optimize=True)
    print("%-24s %3dx%-3d %6d bytes  %s" % (out, im.size[0], im.size[1], os.path.getsize(path), kind))
