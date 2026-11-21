import math
import os
import struct
import sys
import zlib

W, H = 2400, 1600
PX_PER_M = 100


class Canvas:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.pix = bytearray(b"\xff" * (w * h))

    def rect(self, x0, y0, x1, y1, value=0):
        x0, x1 = max(0, x0), min(self.w, x1)
        y0, y1 = max(0, y0), min(self.h, y1)
        if x1 <= x0:
            return
        row = bytes([value]) * (x1 - x0)
        for y in range(y0, y1):
            self.pix[y * self.w + x0:y * self.w + x1] = row

    def blend(self, x, y, coverage):
        if 0 <= x < self.w and 0 <= y < self.h and coverage > 0:
            v = int(round(255 * (1.0 - min(1.0, coverage))))
            i = y * self.w + x
            if v < self.pix[i]:
                self.pix[i] = v

    def segment(self, x0, y0, x1, y1, width):
        r = width / 2.0
        pad = int(math.ceil(r)) + 1
        dx, dy = x1 - x0, y1 - y0
        length2 = dx * dx + dy * dy
        for y in range(int(min(y0, y1)) - pad, int(max(y0, y1)) + pad + 1):
            for x in range(int(min(x0, x1)) - pad, int(max(x0, x1)) + pad + 1):
                px, py = x + 0.5, y + 0.5
                t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / length2))
                d = math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))
                self.blend(x, y, r + 0.5 - d)

    def arc(self, cx, cy, radius, a0, a1, width):
        steps = max(8, int(radius * abs(a1 - a0) / 3))
        prev = None
        for i in range(steps + 1):
            a = a0 + (a1 - a0) * i / steps
            p = (cx + radius * math.cos(a), cy + radius * math.sin(a))
            if prev:
                self.segment(prev[0], prev[1], p[0], p[1], width)
            prev = p

    def ring(self, cx, cy, radius, thickness, a0, a1):
        r0, r1 = radius - thickness / 2.0, radius + thickness / 2.0
        lo, hi = int(r0) - 2, int(r1) + 3
        for y in range(int(cy) - hi, int(cy) + hi):
            for x in range(int(cx) - hi, int(cx) + hi):
                px, py = x + 0.5 - cx, y + 0.5 - cy
                r = math.hypot(px, py)
                if r < lo or r > hi:
                    continue
                a = math.atan2(py, px) % (2 * math.pi)
                if a0 <= a <= a1:
                    self.blend(x, y, min(r - r0 + 0.5, r1 - r + 0.5))

    def png_bytes(self):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            raw += self.pix[y * self.w:(y + 1) * self.w]
        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        header = struct.pack(">IIBBBBB", self.w, self.h, 8, 0, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def door(c, hx, hy, d, n, w):
    ex, ey = hx + n[0] * w, hy + n[1] * w
    if n[0] == 0:
        c.rect(hx - 1, min(hy, ey), hx + 2, max(hy, ey), 0)
    else:
        c.rect(min(hx, ex), hy - 1, max(hx, ex), hy + 2, 0)
    base = math.atan2(n[1], n[0])
    other = math.atan2(d[1], d[0])
    diff = (other - base + math.pi) % (2 * math.pi) - math.pi
    c.arc(hx, hy, w, base, base + diff, 2)


def build():
    c = Canvas(W, H)
    x0, y0, x1, y1 = 500, 300, 1900, 1300
    t, i = 30, 12

    c.rect(x0, y0, x1, y0 + t)
    c.rect(x0, y1 - t, x1, y1)
    c.rect(x0, y0, x0 + t, y1)
    c.rect(x1 - t, y0, x1, y1)

    c.rect(1000, 330, 1000 + i, 1270)
    c.rect(530, 794, 1000, 806)
    c.rect(1012, 694, 1870, 706)
    c.rect(1394, 330, 1406, 700)

    cx, cy, rad = 1870, 1270, 280
    c.ring(cx, cy, rad, i, math.pi, math.pi * 1.25)
    c.ring(cx, cy, rad, i, math.pi * 1.36, math.pi * 1.5)

    c.rect(1000, 706, 1012, 796, 255)
    c.rect(1000, 1000, 1012, 1090, 255)
    c.rect(1100, 694, 1190, 706, 255)
    c.rect(1560, 694, 1800, 706, 255)
    c.rect(1100, y1 - t, 1190, y1, 255)

    door(c, 1006, 796, (0, -1), (1, 0), 90)
    door(c, 1006, 1090, (0, -1), (-1, 0), 90)
    door(c, 1100, 700, (1, 0), (0, -1), 90)
    door(c, 1100, y1 - t // 2, (1, 0), (0, -1), 90)

    for a, b in ((650, 850), (1500, 1700)):
        c.rect(a, y0, b, y0 + t, 255)
        c.rect(a, y0 + 8, b, y0 + 10)
        c.rect(a, y0 + 20, b, y0 + 22)
        c.rect(a - 2, y0, a, y0 + t)
        c.rect(b, y0, b + 2, y0 + t)
    a, b = 950, 1150
    c.rect(x0, a, x0 + t, b, 255)
    c.rect(x0 + 8, a, x0 + 10, b)
    c.rect(x0 + 20, a, x0 + 22, b)
    c.rect(x0, a - 2, x0 + t, a)
    c.rect(x0, b, x0 + t, b + 2)
    a, b = 420, 580
    c.rect(x1 - t, a, x1, b, 255)
    c.rect(x1 - t + 8, a, x1 - t + 10, b)
    c.rect(x1 - t + 20, a, x1 - t + 22, b)
    c.rect(x1 - t, a - 2, x1, a)
    c.rect(x1 - t, b, x1, b + 2)

    c.rect(x0, 220, x1, 222)
    c.rect(x0, 210, x0 + 2, 232)
    c.rect(x1 - 2, 210, x1, 232)
    c.rect(400, y0, 402, y1)
    c.rect(390, y0, 412, y0 + 2)
    c.rect(390, y1 - 2, 412, y1)
    return c


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plans")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.normpath(os.path.join(out_dir, "apartment-01.png"))
    with open(path, "wb") as f:
        f.write(build().png_bytes())
    print("wrote", path)


if __name__ == "__main__":
    sys.exit(main())
