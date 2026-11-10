#!/usr/bin/env python3
"""mkicon.py - draws res/setup98.ico: a cardboard box with a green arrow going in (16 and 32 px, 16 colours).
Deterministic. Writes classic BMP-format icon images (4 bpp + AND mask): VC6 rc.exe and Win98 can't read PNG icons.
Usage: python3 tools/mkicon.py [out.ico]"""
import struct, sys
from PIL import Image, ImageDraw

PAL = [(0,0,0),(128,0,0),(0,128,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
       (128,128,128),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)]
KEY = (255, 0, 255)          # transparent while drawing (fuchsia is not used by the art)

def draw(size):
    s = size / 32.0
    im = Image.new("RGB", (size, size), KEY)
    d = ImageDraw.Draw(im)
    def P(pts): return [(round(x * s), round(y * s)) for x, y in pts]
    # box body (front olive, side darker), flaps yellow, black outline
    d.polygon(P([(4, 15), (20, 15), (20, 30), (4, 30)]), fill=(128,128,0), outline=(0,0,0))
    d.polygon(P([(20, 15), (28, 11), (28, 26), (20, 30)]), fill=(128,128,128) if size < 32 else (128,0,0), outline=(0,0,0))
    d.polygon(P([(4, 15), (12, 11), (28, 11), (20, 15)]), fill=(0,0,0))
    d.polygon(P([(4, 15), (0, 11), (8, 7), (12, 11)]), fill=(255,255,0), outline=(0,0,0))
    d.polygon(P([(20, 15), (24, 19), (31, 14), (28, 11)]), fill=(255,255,0), outline=(0,0,0))
    if size >= 32:
        d.line(P([(6, 22), (18, 22)]), fill=(255,255,0))
    # arrow down into the box
    d.polygon(P([(13, 0), (19, 0), (19, 7), (23, 7), (16, 14), (9, 7), (13, 7)]), fill=(0,255,0), outline=(0,128,0))
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
    out = sys.argv[1] if len(sys.argv) > 1 else "res/setup98.ico"
    imgs = [(32, icon_image(draw(32))), (16, icon_image(draw(16)))]
    data = struct.pack("<HHH", 0, 1, len(imgs))
    off = 6 + 16 * len(imgs)
    for size, blob in imgs:
        data += struct.pack("<BBBBHHII", size, size, 16, 0, 1, 4, len(blob), off)
        off += len(blob)
    for _, blob in imgs: data += blob
    open(out, "wb").write(data)
    draw(32).resize((128, 128), Image.NEAREST).save("obj/setup98_preview.png") if len(sys.argv) < 2 else None
    print(out, len(data), "bytes")

main()
