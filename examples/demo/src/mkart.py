#!/usr/bin/env python3
"""mkart.py - draws the Setup98 demo's art: ../data/logo.bmp (24-bit, 96 x 96) and s98demo.ico (16 and 32 px,
16 colours, classic BMP-format icon images that VC6 rc.exe and Win98 can read). Deterministic, needs Pillow.
Usage (from examples/demo/src): python3 mkart.py"""
import os, struct
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PAL = [(0,0,0),(128,0,0),(0,128,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
       (128,128,128),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)]
KEY = (255, 0, 255)

def floppy(size, bg):
    """a 3.5" floppy disk with a green arrow, drawn on a 32-unit grid scaled to size"""
    s = size / 32.0
    im = Image.new("RGB", (size, size), bg)
    d = ImageDraw.Draw(im)
    def P(pts): return [(round(x * s), round(y * s)) for x, y in pts]
    d.polygon(P([(3, 3), (27, 3), (29, 5), (29, 29), (3, 29)]), fill=(0,0,128), outline=(0,0,0))
    d.rectangle(P([(9, 3), (23, 12)]), fill=(192,192,192), outline=(0,0,0))
    d.rectangle(P([(18, 5), (21, 10)]), fill=(0,0,128))
    d.rectangle(P([(7, 16), (25, 29)]), fill=(255,255,255), outline=(0,0,0))
    if size >= 32:
        for y in (20, 23, 26):
            d.line(P([(9, y), (23, y)]), fill=(128,128,128))
    # arrow into the disk
    d.polygon(P([(14, 13), (18, 13), (18, 19), (21, 19), (16, 25), (11, 19), (14, 19)]), fill=(0,255,0), outline=(0,128,0))
    return im

def nearest(c):
    best, bi = None, 0
    for i, p in enumerate(PAL):
        dd = sum((a - b) ** 2 for a, b in zip(c, p))
        if best is None or dd < best: best, bi = dd, i
    return bi

def icon_image(im):
    w, h = im.size
    px = im.load()
    xor_row = ((w * 4 + 31) // 32) * 4
    and_row = ((w + 31) // 32) * 4
    xor, mask = bytearray(), bytearray()
    for y in range(h - 1, -1, -1):               # bottom-up
        row, mrow = bytearray(xor_row), bytearray(and_row)
        for x in range(w):
            c = px[x, y]
            idx = 0 if c == KEY else nearest(c)
            if c == KEY: mrow[x // 8] |= 0x80 >> (x % 8)
            row[x // 2] |= idx << (4 if x % 2 == 0 else 0)
        xor += row
        mask += mrow
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 4, 0, len(xor) + len(mask), 0, 0, 16, 0)
    pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in PAL)
    return hdr + pal + bytes(xor) + bytes(mask)

def main():
    imgs = [(32, icon_image(floppy(32, KEY))), (16, icon_image(floppy(16, KEY)))]
    data = struct.pack("<HHH", 0, 1, len(imgs))
    off = 6 + 16 * len(imgs)
    for size, blob in imgs:
        data += struct.pack("<BBBBHHII", size, size, 16, 0, 1, 4, len(blob), off)
        off += len(blob)
    for _, blob in imgs: data += blob
    with open(os.path.join(HERE, "s98demo.ico"), "wb") as f: f.write(data)
    # logo: the 32 px drawing at 3x on the window colour, plain 24-bit BMP
    logo = floppy(32, (192,192,192)).resize((96, 96), Image.NEAREST)
    logo.save(os.path.join(HERE, "..", "data", "logo.bmp"))
    print("s98demo.ico", len(data), "bytes; ../data/logo.bmp")

main()
