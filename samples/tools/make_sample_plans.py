import json
import math
import os
import random
import struct
import sys
import zlib

W, H = 2400, 1600


class Canvas:
    # Starts a blank white image.
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.pix = bytearray(b"\xff" * (w * h))

    # Fills a box with one gray value.
    def rect(self, x0, y0, x1, y1, value=0):
        x0, x1 = max(0, x0), min(self.w, x1)
        y0, y1 = max(0, y0), min(self.h, y1)
        if x1 <= x0:
            return
        row = bytes([value]) * (x1 - x0)
        for y in range(y0, y1):
            self.pix[y * self.w + x0:y * self.w + x1] = row

    # Darkens one pixel by the given coverage, never lightening it.
    def blend(self, x, y, coverage):
        if 0 <= x < self.w and 0 <= y < self.h and coverage > 0:
            v = int(round(255 * (1.0 - min(1.0, coverage))))
            i = y * self.w + x
            if v < self.pix[i]:
                self.pix[i] = v

    # Draws an antialiased line of the given width.
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

    # Draws a thin curved line along part of a circle.
    def arc(self, cx, cy, radius, a0, a1, width):
        steps = max(8, int(radius * abs(a1 - a0) / 3))
        prev = None
        for i in range(steps + 1):
            a = a0 + (a1 - a0) * i / steps
            p = (cx + radius * math.cos(a), cy + radius * math.sin(a))
            if prev:
                self.segment(prev[0], prev[1], p[0], p[1], width)
            prev = p

    # Draws a thick curved wall along part of a circle.
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

    # Returns the image encoded as a grayscale PNG file.
    def png_bytes(self):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            raw += self.pix[y * self.w:(y + 1) * self.w]
        # Wraps data as one PNG chunk with its length and checksum.
        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        header = struct.pack(">IIBBBBB", self.w, self.h, 8, 0, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


# Draws a door leaf and its swing arc from the hinge point.
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


# Doors of apartment-01: hinge point, direction toward the gap, direction of the open leaf, width.
DOORS1 = [
    ((1006, 796), (0, -1), (1, 0), 90), ((1006, 1090), (0, -1), (-1, 0), 90),
    ((1100, 700), (1, 0), (0, -1), 90), ((1100, 1285), (1, 0), (0, -1), 90),
]


# Returns the hinge and the swing side point of a door, in image pixels.
def door_marks(hinge, d, n, w):
    centre = (hinge[0] + d[0] * w / 2.0, hinge[1] + d[1] * w / 2.0)
    swing = (centre[0] + n[0] * w / 4.0, centre[1] + n[1] * w / 4.0)
    return {"hinge": [whole(v) for v in hinge], "swing_point": [whole(v) for v in swing]}


# Turns a float with no fraction into an int so the JSON stays short.
def whole(v):
    return int(v) if float(v).is_integer() else v


# Draws the whole apartment plan.
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

    for hinge, d, n, w in DOORS1:
        door(c, hinge[0], hinge[1], d, n, w)

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


# Describes what detection should find in apartment-01, in image pixels.
def expected_01():
    points = [(515, 315), (1006, 315), (1400, 315), (1885, 315), (1885, 700), (1885, 1285),
              (1006, 1285), (515, 1285), (515, 800), (1006, 800), (1006, 700), (1400, 700)]
    outer = [((515, 315), (1006, 315)), ((1006, 315), (1400, 315)), ((1400, 315), (1885, 315)),
             ((1885, 315), (1885, 700)), ((1885, 700), (1885, 1285)), ((515, 1285), (1006, 1285)),
             ((1006, 1285), (1885, 1285)), ((515, 315), (515, 800)), ((515, 800), (515, 1285))]
    inner = [((1006, 315), (1006, 700)), ((1006, 700), (1006, 800)), ((1006, 800), (1006, 1285)),
             ((515, 800), (1006, 800)), ((1006, 700), (1400, 700)), ((1400, 700), (1885, 700)),
             ((1400, 315), (1400, 700))]
    walls = [(a, b, 30) for a, b in outer] + [(a, b, 12) for a, b in inner]
    rooms = [((760, 557), 238135), ((760, 1042), 238135), ((1203, 507), 151690),
             ((1642, 507), 186725), ((1300, 900), 514215)]
    openings = [("window", 750, 315, 200), ("window", 1600, 315, 200), ("window", 515, 1050, 200),
                ("window", 1885, 500, 160)]
    for hinge, d, n, w in DOORS1:
        cx, cy = whole(hinge[0] + d[0] * w / 2), whole(hinge[1] + d[1] * w / 2)
        openings.append(("door", cx, cy, w, door_marks(hinge, d, n, w)))
    openings.append(("plain", 1680, 700, 240))
    not_walls = [("dimension_line", (495, 205, 1905, 235)), ("dimension_line", (385, 295, 415, 1305)),
                 ("door_arc", (1014, 708, 1098, 792)), ("door_arc", (914, 1002, 998, 1088)),
                 ("door_arc", (1098, 608, 1192, 692)), ("door_arc", (1098, 1193, 1192, 1268)),
                 ("curved_wall", (1584, 984, 1868, 1268))]
    return expected_dict("apartment-01.png", 100, points, walls, rooms, openings, not_walls)


# Builds the expected result dictionary in the shared file shape.
def expected_dict(image, ppm, points, walls, rooms, openings, not_walls):
    return {
        "format": "plynth-sample-expected",
        "version": 1,
        "image": image,
        "pixels_per_metre": ppm,
        "points": [list(p) for p in points],
        "walls": [{"from": list(a), "to": list(b), "thickness": t} for a, b, t in walls],
        "rooms": [{"inside": list(p), "centre_line_area": a} for p, a in rooms],
        "openings": [dict({"type": o[0], "x": o[1], "y": o[2], "width": o[3]}, **(o[4] if len(o) > 4 else {}))
                     for o in openings],
        "not_walls": [{"kind": k, "box": list(b)} for k, b in not_walls],
    }


# Writes an expected result as JSON with one list entry per line.
def expected_json(d):
    lines = ["{"]
    keys = list(d)
    for n, key in enumerate(keys):
        value = d[key]
        tail = "," if n < len(keys) - 1 else ""
        if isinstance(value, list):
            lines.append('  "%s": [' % key)
            for j, item in enumerate(value):
                comma = "," if j < len(value) - 1 else ""
                lines.append("    " + json.dumps(item) + comma)
            lines.append("  ]" + tail)
        else:
            lines.append('  "%s": %s%s' % (key, json.dumps(value), tail))
    lines.append("}")
    return "\n".join(lines) + "\n"


PAPER2, INK2 = 240, 50
W2, H2 = 2000, 1400


class PaperCanvas(Canvas):
    # Starts an off white sheet that draws in dark grey instead of black.
    def __init__(self, w, h):
        Canvas.__init__(self, w, h)
        self.pix = bytearray(bytes([PAPER2]) * (w * h))

    # Fills a box, treating black as the ink grey and white as the paper.
    def rect(self, x0, y0, x1, y1, value=0):
        if value == 0:
            value = INK2
        elif value == 255:
            value = PAPER2
        Canvas.rect(self, x0, y0, x1, y1, value)

    # Darkens one pixel toward the ink grey, never lightening it.
    def blend(self, x, y, coverage):
        if 0 <= x < self.w and 0 <= y < self.h and coverage > 0:
            v = int(round(PAPER2 - (PAPER2 - INK2) * min(1.0, coverage)))
            i = y * self.w + x
            if v < self.pix[i]:
                self.pix[i] = v

    # Adds seeded noise and then a light 3 by 3 blur over the whole sheet.
    def roughen(self, seed, amount):
        w, h = self.w, self.h
        noise = random.Random(seed).randbytes(w * h)
        span = 2 * amount + 1
        rows = []
        for y in range(h):
            row = self.pix[y * w:(y + 1) * w]
            n = noise[y * w:(y + 1) * w]
            rows.append([min(255, max(0, p + (q % span) - amount)) for p, q in zip(row, n)])
        sums = []
        for row in rows:
            p = row[:1] + row + row[-1:]
            sums.append([a + b + c for a, b, c in zip(p, p[1:], p[2:])])
        out = bytearray()
        for y in range(h):
            a, b, c = sums[max(y - 1, 0)], sums[y], sums[min(y + 1, h - 1)]
            out += bytes([(p + q + r + 4) // 9 for p, q, r in zip(a, b, c)])
        self.pix = out


GLYPHS = {
    "A": [[(0, 8), (2, 0), (4, 8)], [(0.8, 5.5), (3.2, 5.5)]],
    "B": [[(0, 0), (0, 8), (3, 8), (4, 7), (4, 5), (3, 4), (0, 4)], [(0, 0), (3, 0), (4, 1), (4, 3), (3, 4)]],
    "C": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 7), (1, 8), (3, 8), (4, 7)]],
    "D": [[(0, 0), (0, 8), (2, 8), (4, 6), (4, 2), (2, 0), (0, 0)]],
    "E": [[(4, 0), (0, 0), (0, 8), (4, 8)], [(0, 4), (3, 4)]],
    "F": [[(4, 0), (0, 0), (0, 8)], [(0, 4), (3, 4)]],
    "G": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 7), (1, 8), (3, 8), (4, 7), (4, 4), (2, 4)]],
    "H": [[(0, 0), (0, 8)], [(4, 0), (4, 8)], [(0, 4), (4, 4)]],
    "I": [[(1, 0), (3, 0)], [(2, 0), (2, 8)], [(1, 8), (3, 8)]],
    "J": [[(4, 0), (4, 7), (3, 8), (1, 8), (0, 7)]],
    "K": [[(0, 0), (0, 8)], [(4, 0), (0, 4), (4, 8)]],
    "L": [[(0, 0), (0, 8), (4, 8)]],
    "M": [[(0, 8), (0, 0), (2, 4), (4, 0), (4, 8)]],
    "N": [[(0, 8), (0, 0), (4, 8), (4, 0)]],
    "O": [[(1, 0), (3, 0), (4, 1), (4, 7), (3, 8), (1, 8), (0, 7), (0, 1), (1, 0)]],
    "P": [[(0, 8), (0, 0), (3, 0), (4, 1), (4, 3), (3, 4), (0, 4)]],
    "Q": [[(1, 0), (3, 0), (4, 1), (4, 7), (3, 8), (1, 8), (0, 7), (0, 1), (1, 0)], [(2.5, 5.5), (4, 8)]],
    "R": [[(0, 8), (0, 0), (3, 0), (4, 1), (4, 3), (3, 4), (0, 4)], [(2, 4), (4, 8)]],
    "S": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 3), (1, 4), (3, 4), (4, 5), (4, 7), (3, 8), (1, 8), (0, 7)]],
    "T": [[(0, 0), (4, 0)], [(2, 0), (2, 8)]],
    "U": [[(0, 0), (0, 7), (1, 8), (3, 8), (4, 7), (4, 0)]],
    "V": [[(0, 0), (2, 8), (4, 0)]],
    "W": [[(0, 0), (1, 8), (2, 4), (3, 8), (4, 0)]],
    "X": [[(0, 0), (4, 8)], [(4, 0), (0, 8)]],
    "Y": [[(0, 0), (2, 4), (4, 0)], [(2, 4), (2, 8)]],
    "Z": [[(0, 0), (4, 0), (0, 8), (4, 8)]],
    "0": [[(1, 0), (3, 0), (4, 1), (4, 7), (3, 8), (1, 8), (0, 7), (0, 1), (1, 0)]],
    "1": [[(1, 1), (2, 0), (2, 8)], [(1, 8), (3, 8)]],
    "2": [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 3), (0, 8), (4, 8)]],
    "3": [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 3), (3, 4), (1.5, 4)],
          [(3, 4), (4, 5), (4, 7), (3, 8), (1, 8), (0, 7)]],
    "4": [[(3, 8), (3, 0), (0, 6), (4, 6)]],
    "5": [[(4, 0), (0, 0), (0, 4), (3, 4), (4, 5), (4, 7), (3, 8), (1, 8), (0, 7)]],
    "6": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 7), (1, 8), (3, 8), (4, 7), (4, 5), (3, 4), (0, 4)]],
    "7": [[(0, 0), (4, 0), (1.5, 8)]],
    "8": [[(1, 0), (3, 0), (4, 1), (4, 3), (3, 4), (1, 4), (0, 3), (0, 1), (1, 0)],
          [(1, 4), (0, 5), (0, 7), (1, 8), (3, 8), (4, 7), (4, 5), (3, 4)]],
    "9": [[(4, 3), (3, 4), (1, 4), (0, 3), (0, 1), (1, 0), (3, 0), (4, 1), (4, 7), (3, 8), (1, 8), (0, 7)]],
    ".": [[(2, 7.2), (2, 8)]],
}


# Draws text in the stroke font and returns its padded bounding box.
def text(c, x, y, s, scale=5, width=3):
    pen = x
    for ch in s:
        if ch == " ":
            pen += 4 * scale
            continue
        for stroke in GLYPHS[ch]:
            for a, b in zip(stroke, stroke[1:]):
                c.segment(pen + a[0] * scale, y + a[1] * scale, pen + b[0] * scale, y + b[1] * scale, width)
        pen += (2 if ch == "." else 5.6) * scale
    end = pen - 1.6 * scale
    return (int(x) - 4, int(y) - 4, int(end) + 4, int(y + 8 * scale) + 4)


# Draws a 2 pixel outline of a rectangle.
def outline(c, x0, y0, x1, y1):
    c.rect(x0, y0, x1, y0 + 2)
    c.rect(x0, y1 - 2, x1, y1)
    c.rect(x0, y0, x0 + 2, y1)
    c.rect(x1 - 2, y0, x1, y1)


# Draws a thin circle outline.
def circle(c, cx, cy, r):
    c.arc(cx, cy, r, 0, 2 * math.pi, 2)


L2, R2, T2, B2 = 252, 1828, 252, 1228
XV1, XV2 = 800, 1300
YH1, YH2 = 720, 640
XSPUR, YSPUR = 1500, 736
OUT2, IN2 = 24, 10

# Gaps in walls: (kind, axis, fixed coordinate, start, end, wall thickness).
GAPS2 = [
    ("window", "h", T2, 446, 606, OUT2), ("window", "h", T2, 970, 1130, OUT2),
    ("window", "h", T2, 1484, 1644, OUT2), ("window", "v", R2, 850, 1010, OUT2),
    ("window", "v", L2, 900, 1060, OUT2), ("window", "h", B2, 1220, 1380, OUT2),
    ("door", "h", YH1, 520, 592, IN2), ("door", "v", XV1, 1000, 1072, IN2),
    ("door", "v", XV1, 380, 452, IN2), ("door", "h", YH2, 1060, 1132, IN2),
    ("door", "h", YH2, 1640, 1712, IN2), ("door", "v", XV2, 440, 512, IN2),
    ("plain", "v", XV1, 800, 920, IN2),
]
# Door hinge, direction toward the gap, and direction of the open leaf.
DOORS2 = [
    ((592, YH1), (-1, 0), (0, 1)), ((XV1, 1072), (0, -1), (1, 0)), ((XV1, 452), (0, -1), (1, 0)),
    ((1060, YH2), (1, 0), (0, 1)), ((1712, YH2), (-1, 0), (0, 1)), ((XV2, 512), (0, -1), (1, 0)),
]


# Draws apartment-02 and returns the canvas and the boxes of marks that are not walls.
def build_02():
    c = PaperCanvas(W2, H2)
    h = OUT2 // 2
    q = IN2 // 2
    c.rect(L2 - h, T2 - h, R2 + h, T2 + h)
    c.rect(L2 - h, B2 - h, R2 + h, B2 + h)
    c.rect(L2 - h, T2 - h, L2 + h, B2 + h)
    c.rect(R2 - h, T2 - h, R2 + h, B2 + h)
    c.rect(XV1 - q, T2, XV1 + q, B2)
    c.rect(L2, YH1 - q, XV1, YH1 + q)
    c.rect(XV1, YH2 - q, R2, YH2 + q)
    c.rect(XV2 - q, T2, XV2 + q, YH2 + q)
    c.rect(XSPUR - q, YH2, XSPUR + q, YSPUR)

    for kind, axis, at, a, b, t in GAPS2:
        r = t // 2
        if axis == "h":
            c.rect(a, at - r, b, at + r, 255)
            if kind == "window":
                c.rect(a, at - 5, b, at - 3)
                c.rect(a, at + 3, b, at + 5)
        else:
            c.rect(at - r, a, at + r, b, 255)
            if kind == "window":
                c.rect(at - 5, a, at - 3, b)
                c.rect(at + 3, a, at + 5, b)
    for hinge, d, n in DOORS2:
        door(c, hinge[0], hinge[1], d, n, 72)

    boxes = []
    boxes.append(("door_arc", (520, YH1 + 7, 595, YH1 + 74)))
    boxes.append(("door_arc", (XV1 + 7, 998, XV1 + 75, 1075)))
    boxes.append(("door_arc", (XV1 + 7, 378, XV1 + 75, 455)))
    boxes.append(("door_arc", (1058, YH2 + 7, 1134, YH2 + 74)))
    boxes.append(("door_arc", (1638, YH2 + 7, 1714, YH2 + 74)))
    boxes.append(("door_arc", (XV2 + 7, 438, XV2 + 75, 515)))

    outline(c, 300, 280, 470, 490)
    outline(c, 312, 292, 378, 336)
    outline(c, 392, 292, 458, 336)
    c.rect(300, 400, 470, 402)
    boxes.append(("furniture", (296, 276, 474, 494)))
    outline(c, 290, 940, 480, 1170)
    outline(c, 302, 952, 378, 996)
    outline(c, 392, 952, 468, 996)
    c.rect(290, 1060, 480, 1062)
    boxes.append(("furniture", (286, 936, 484, 1174)))
    outline(c, 1090, 276, 1284, 372)
    outline(c, 1102, 288, 1272, 360)
    boxes.append(("furniture", (1086, 272, 1288, 376)))
    outline(c, 1222, 500, 1272, 600)
    circle(c, 1247, 540, 14)
    boxes.append(("furniture", (1218, 496, 1276, 604)))
    outline(c, 1376, 268, 1804, 334)
    for cx in (1470, 1530, 1700):
        circle(c, cx, 301, 14)
    boxes.append(("furniture", (1372, 266, 1808, 338)))
    outline(c, 1500, 470, 1700, 570)
    for x in (1520, 1640):
        outline(c, x, 576, x + 40, 606)
        outline(c, x, 434, x + 40, 464)
    boxes.append(("furniture", (1496, 430, 1704, 610)))
    outline(c, 1400, 1100, 1740, 1200)
    outline(c, 1400, 1100, 1740, 1135)
    outline(c, 1400, 1100, 1430, 1200)
    outline(c, 1710, 1100, 1740, 1200)
    boxes.append(("furniture", (1396, 1096, 1744, 1204)))
    outline(c, 1480, 960, 1660, 1040)
    boxes.append(("furniture", (1476, 956, 1664, 1044)))
    outline(c, 1000, 980, 1200, 1070)
    boxes.append(("furniture", (996, 976, 1204, 1074)))

    for x, y, s in ((500, 560, "BED 1"), (520, 850, "BED 2"), (900, 540, "BATH"),
                    (1420, 375, "KITCHEN"), (900, 830, "LIVING"), (1380, 1310, "LEGEND")):
        boxes.append(("text", text(c, x, y, s)))

    mid = 1040
    c.rect(L2, 149, mid - 70, 151)
    c.rect(mid + 70, 149, R2, 151)
    c.rect(L2 - 1, 138, L2 + 1, 163)
    c.rect(R2 - 1, 138, R2 + 1, 163)
    text(c, mid - 56, 134, "19.70", 4, 3)
    boxes.append(("dimension_line", (244, 128, 1836, 172)))
    midy = 740
    c.rect(159, T2, 161, midy - 40)
    c.rect(159, midy + 40, 161, B2)
    c.rect(148, T2 - 1, 172, T2 + 1)
    c.rect(148, B2 - 1, 172, B2 + 1)
    text(c, 160 - 54, midy - 16, "12.20", 4, 3)
    boxes.append(("dimension_line", (100, 246, 220, 1234)))

    c.rect(1600, 1280, 1700, 1380)
    boxes.append(("filled_block", (1598, 1278, 1702, 1382)))
    c.roughen(20240611, 12)
    return c, boxes


# Describes what detection should find in apartment-02, in image pixels.
def expected_02(boxes):
    points = [(L2, T2), (R2, T2), (R2, B2), (L2, B2), (XV1, T2), (XV2, T2), (R2, YH2), (XV1, B2),
              (L2, YH1), (XV1, YH1), (XV1, YH2), (XV2, YH2), (XSPUR, YH2), (XSPUR, YSPUR)]
    walls = []
    for a, b in ((L2, XV1), (XV1, XV2), (XV2, R2)):
        walls.append(((a, T2), (b, T2), OUT2))
    walls += [((R2, T2), (R2, YH2), OUT2), ((R2, YH2), (R2, B2), OUT2),
              ((L2, B2), (XV1, B2), OUT2), ((XV1, B2), (R2, B2), OUT2),
              ((L2, T2), (L2, YH1), OUT2), ((L2, YH1), (L2, B2), OUT2)]
    walls += [((XV1, T2), (XV1, YH2), IN2), ((XV1, YH2), (XV1, YH1), IN2), ((XV1, YH1), (XV1, B2), IN2),
              ((L2, YH1), (XV1, YH1), IN2), ((XV1, YH2), (XV2, YH2), IN2),
              ((XV2, YH2), (XSPUR, YH2), IN2), ((XSPUR, YH2), (R2, YH2), IN2),
              ((XV2, T2), (XV2, YH2), IN2), ((XSPUR, YH2), (XSPUR, YSPUR), IN2)]
    rooms = [((530, 490), (XV1 - L2) * (YH1 - T2)), ((530, 975), (XV1 - L2) * (B2 - YH1)),
             ((1050, 445), (XV2 - XV1) * (YH2 - T2)), ((1565, 445), (R2 - XV2) * (YH2 - T2)),
             ((1100, 900), (R2 - XV1) * (B2 - YH2))]
    openings = []
    swings = iter(DOORS2)
    for kind, axis, at, a, b, t in GAPS2:
        mid = (a + b) // 2
        entry = (kind, mid if axis == "h" else at, at if axis == "h" else mid, b - a)
        if kind == "door":
            hinge, d, n = next(swings)
            entry += (door_marks(hinge, d, n, b - a),)
        openings.append(entry)
    return expected_dict("apartment-02.png", 80, points, walls, rooms, openings, boxes)


HW, HH = 1800, 1300
OUTER_T, INNER_T = 30, 12
DIM_TOP, DIM_LEFT = 120, 400

# The two floors of house-01, all in image pixels at 100 per metre. rect is the outer wall centre line box.
# inner holds the inner wall centre lines already split at their junctions. rooms are centre line boxes.
# gaps are (kind, axis, fixed coordinate, start, end, wall thickness) and doors add (hinge, toward the gap,
# direction of the open leaf). The ground floor is 10 by 8 m. The upper floor covers its left two thirds and
# reaches 1 m past it at the back (the top of the image); the left and front outer corners are the same pixels.
HOUSE = {
    "ground": {
        "image": "house-01-ground.png",
        "rect": (515, 315, 1485, 1085),
        "inner": [(1000, 315, 1000, 700), (1000, 700, 1000, 800), (1000, 800, 1000, 1085),
                  (515, 800, 1000, 800), (1000, 700, 1485, 700)],
        "rooms": [(515, 315, 1000, 800), (515, 800, 1000, 1085), (1000, 315, 1485, 700), (1000, 700, 1485, 1085)],
        "gaps": [
            ("window", "h", 315, 650, 850, OUTER_T), ("window", "h", 315, 1142, 1342, OUTER_T),
            ("window", "v", 515, 450, 650, OUTER_T), ("window", "v", 515, 840, 1040, OUTER_T),
            ("window", "v", 1485, 420, 580, OUTER_T), ("window", "v", 1485, 820, 980, OUTER_T),
            ("window", "h", 1085, 600, 800, OUTER_T),
            ("door", "h", 1085, 1200, 1290, OUTER_T, (1200, 1085), (1, 0), (0, -1)),
            ("door", "h", 700, 1100, 1190, INNER_T, (1100, 700), (1, 0), (0, -1)),
            ("door", "v", 1000, 900, 990, INNER_T, (1000, 990), (0, -1), (-1, 0)),
            ("door", "h", 800, 700, 790, INNER_T, (700, 800), (1, 0), (0, -1)),
            ("plain", "v", 1000, 400, 600, INNER_T),
        ],
        "frame": (500, 300, 1500, 1100),
    },
    "upper": {
        "image": "house-01-upper.png",
        "rect": (515, 215, 1155, 1085),
        "inner": [(1000, 215, 1000, 500), (1000, 500, 1000, 660), (1000, 660, 1000, 1085),
                  (515, 660, 1000, 660), (1000, 500, 1155, 500)],
        "rooms": [(515, 215, 1000, 660), (515, 660, 1000, 1085), (1000, 215, 1155, 500), (1000, 500, 1155, 1085)],
        "gaps": [
            ("window", "h", 215, 650, 850, OUTER_T), ("window", "h", 215, 1040, 1120, OUTER_T),
            ("window", "v", 515, 320, 520, OUTER_T), ("window", "v", 515, 770, 970, OUTER_T),
            ("window", "h", 1085, 650, 850, OUTER_T), ("window", "v", 1155, 720, 880, OUTER_T),
            ("door", "v", 1000, 540, 630, INNER_T, (1000, 630), (0, -1), (-1, 0)),
            ("door", "v", 1000, 900, 990, INNER_T, (1000, 990), (0, -1), (-1, 0)),
            ("door", "h", 500, 1040, 1130, INNER_T, (1130, 500), (-1, 0), (0, -1)),
        ],
        "frame": (500, 200, 1170, 1100),
    },
}


# Draws one floor of house-01.
def build_house(level):
    c = Canvas(HW, HH)
    left, top, right, bottom = level["rect"]
    half = OUTER_T // 2
    c.rect(left - half, top - half, right + half, top + half)
    c.rect(left - half, bottom - half, right + half, bottom + half)
    c.rect(left - half, top - half, left + half, bottom + half)
    c.rect(right - half, top - half, right + half, bottom + half)
    for x0, y0, x1, y1 in level["inner"]:
        q = INNER_T // 2
        if x0 == x1:
            c.rect(x0 - q, y0, x0 + q, y1)
        else:
            c.rect(x0, y0 - q, x1, y0 + q)
    for gap in level["gaps"]:
        kind, axis, at, a, b, t = gap[:6]
        r = t // 2
        if axis == "h":
            c.rect(a, at - r, b, at + r, 255)
            if kind == "window":
                c.rect(a, at - r + 8, b, at - r + 10)
                c.rect(a, at - r + 20, b, at - r + 22)
                c.rect(a - 2, at - r, a, at + r)
                c.rect(b, at - r, b + 2, at + r)
        else:
            c.rect(at - r, a, at + r, b, 255)
            if kind == "window":
                c.rect(at - r + 8, a, at - r + 10, b)
                c.rect(at - r + 20, a, at - r + 22, b)
                c.rect(at - r, a - 2, at + r, a)
                c.rect(at - r, b, at + r, b + 2)
        if kind == "door":
            hinge, d, n = gap[6:]
            door(c, hinge[0], hinge[1], d, n, b - a)
    x0, y0, x1, y1 = level["frame"]
    c.rect(x0, DIM_TOP, x1, DIM_TOP + 2)
    c.rect(x0, DIM_TOP - 10, x0 + 2, DIM_TOP + 12)
    c.rect(x1 - 2, DIM_TOP - 10, x1, DIM_TOP + 12)
    c.rect(DIM_LEFT, y0, DIM_LEFT + 2, y1)
    c.rect(DIM_LEFT - 10, y0, DIM_LEFT + 12, y0 + 2)
    c.rect(DIM_LEFT - 10, y1 - 2, DIM_LEFT + 12, y1)
    return c


# Returns the box around the swing arc of a door that stays clear of its wall band.
def arc_box(hinge, d, n, w, thickness):
    near = thickness // 2 + 2
    box = [0, 0, 0, 0]
    for axis in (0, 1):
        if n[axis] != 0:
            start = hinge[axis] + n[axis] * near
            end = hinge[axis] + n[axis] * (w + 2)
        else:
            start = hinge[axis] - 2 * d[axis]
            end = hinge[axis] + d[axis] * (w + 2)
        box[axis], box[axis + 2] = min(start, end), max(start, end)
    return tuple(box)


# Describes what detection should find in one floor of house-01, in image pixels.
def expected_house(level):
    left, top, right, bottom = level["rect"]
    inner = level["inner"]
    junctions = []
    for x0, y0, x1, y1 in inner:
        for p in ((x0, y0), (x1, y1)):
            if p not in junctions:
                junctions.append(p)
    points = [(left, top), (right, top), (right, bottom), (left, bottom)]
    points += [p for p in junctions if p not in points]
    walls = []
    edges = [((left, top), (right, top), 0), ((right, top), (right, bottom), 1),
             ((left, bottom), (right, bottom), 0), ((left, top), (left, bottom), 1)]
    for a, b, axis in edges:
        stops = sorted({a[axis], b[axis]} | {p[axis] for p in junctions
                                             if p[1 - axis] == a[1 - axis] and a[axis] < p[axis] < b[axis]})
        for lo, hi in zip(stops, stops[1:]):
            walls.append(((lo, a[1]) if axis == 0 else (a[0], lo), (hi, a[1]) if axis == 0 else (a[0], hi), OUTER_T))
    walls += [((x0, y0), (x1, y1), INNER_T) for x0, y0, x1, y1 in inner]
    rooms = [(((x0 + x1) // 2, (y0 + y1) // 2), (x1 - x0) * (y1 - y0)) for x0, y0, x1, y1 in level["rooms"]]
    openings = []
    frame = level["frame"]
    not_walls = [("dimension_line", (frame[0] - 5, DIM_TOP - 15, frame[2] + 5, DIM_TOP + 15)),
                 ("dimension_line", (DIM_LEFT - 15, frame[1] - 5, DIM_LEFT + 15, frame[3] + 5))]
    for gap in level["gaps"]:
        kind, axis, at, a, b, t = gap[:6]
        mid = (a + b) // 2
        entry = (kind, mid if axis == "h" else at, at if axis == "h" else mid, b - a)
        if kind == "door":
            hinge, d, n = gap[6:]
            centre = (hinge[0] + d[0] * (b - a) / 2.0, hinge[1] + d[1] * (b - a) / 2.0)
            entry = (kind, whole(centre[0]), whole(centre[1]), b - a, door_marks(hinge, d, n, b - a))
            not_walls.append(("door_arc", arc_box(hinge, d, n, b - a, t)))
        openings.append(entry)
    return expected_dict(level["image"], 100, points, walls, rooms, openings, not_walls)


# Returns the keyhole outline in the unit box: an ellipse on top of a tapered base, anticlockwise.
def keyhole_outline():
    cx, cy, rx, ry = 0.5, 0.72, 0.5, 0.28
    neck = math.acos(0.36)
    start, end = -neck, math.pi + neck
    steps = 56
    ts = {start + (end - start) * i / steps for i in range(steps + 1)}
    ts |= {0.0, math.pi / 2, math.pi}
    pts = [(0.2, 0.0), (0.8, 0.0)]
    for t in sorted(ts):
        x = min(1.0, max(0.0, round(cx + rx * math.cos(t), 6)))
        y = min(1.0, max(0.0, round(cy + ry * math.sin(t), 6)))
        pts.append((x, y))
    return pts


# Returns the keyhole shape file text, one point per line.
def keyhole_json():
    pts = keyhole_outline()
    lines = ["{", '  "format": "plynth-shape",', '  "version": 1,', '  "id": "keyhole",',
             '  "name": "Keyhole",', '  "outline": [']
    for i, p in enumerate(pts):
        lines.append("    " + json.dumps([whole(p[0]), whole(p[1])]) + ("," if i < len(pts) - 1 else ""))
    lines += ["  ]", "}"]
    return "\n".join(lines) + "\n"


# Writes the plan images and their expected results next to the samples folder.
def main():
    out_dir = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plans"))
    os.makedirs(out_dir, exist_ok=True)
    c2, boxes = build_02()
    outputs = [
        ("apartment-01.png", build().png_bytes()),
        ("apartment-01.expected.json", expected_json(expected_01()).encode("utf-8")),
        ("apartment-02.png", c2.png_bytes()),
        ("apartment-02.expected.json", expected_json(expected_02(boxes)).encode("utf-8")),
    ]
    for level in HOUSE.values():
        outputs.append((level["image"], build_house(level).png_bytes()))
        outputs.append((level["image"].replace(".png", ".expected.json"),
                        expected_json(expected_house(level)).encode("utf-8")))
    shape_dir = os.path.normpath(os.path.join(out_dir, "..", "shapes"))
    os.makedirs(shape_dir, exist_ok=True)
    with open(os.path.join(shape_dir, "keyhole.shape.json"), "wb") as f:
        f.write(keyhole_json().encode("utf-8"))
    for name, data in outputs:
        path = os.path.join(out_dir, name)
        with open(path, "wb") as f:
            f.write(data)
        print("wrote", path)


if __name__ == "__main__":
    sys.exit(main())
