import os
import struct
import sys
import zlib

# The chair is built in Blender axes (X right, Y back, Z up) and written in OBJ axes (Y up, -Z back).
# It is moved off the origin so the cleanup has something to do: after Blender's default OBJ import the
# bounding box minimum is at (1.0, 2.0, 0.5).
OFFSET = (1.0, 2.0, 0.5)

# Name, then x range, y range, z range in metres, and the material.
BOXES = [
    ("Back", (0.00, 0.45), (0.46, 0.50), (0.47, 0.90), "Wood"),
    ("Leg_BL", (0.00, 0.04), (0.46, 0.50), (0.00, 0.43), "Wood"),
    ("Leg_BR", (0.41, 0.45), (0.46, 0.50), (0.00, 0.43), "Wood"),
    ("Leg_FL", (0.00, 0.04), (0.00, 0.04), (0.00, 0.43), "Wood"),
    ("Leg_FR", (0.41, 0.45), (0.00, 0.04), (0.00, 0.43), "Wood"),
    ("Seat", (0.00, 0.45), (0.00, 0.50), (0.43, 0.47), "Fabric"),
]

# Corner indices of the six faces of a box, seen from outside in counter clockwise order. Corner index bits:
# 1 is the high x, 2 the high y, 4 the high z.
FACES = [
    (0, 2, 3, 1),  # bottom, normal -Z
    (4, 5, 7, 6),  # top, +Z
    (0, 1, 5, 4),  # front, -Y
    (2, 6, 7, 3),  # back, +Y
    (0, 4, 6, 2),  # left, -X
    (1, 3, 7, 5),  # right, +X
]

UVS = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]


# Returns the eight corners of a box in Blender axes, with the offset added.
def box_corners(x, y, z):
    corners = []
    for index in range(8):
        px = x[1] if index & 1 else x[0]
        py = y[1] if index & 2 else y[0]
        pz = z[1] if index & 4 else z[0]
        corners.append((px + OFFSET[0], py + OFFSET[1], pz + OFFSET[2]))
    return corners


# Builds the text of chair.obj.
def chair_obj():
    lines = ["# Sample chair for Plynth, made by make_sample_models.py", "mtllib chair.mtl"]
    for u, v in UVS:
        lines.append("vt %.4f %.4f" % (u, v))
    base = 0
    for name, x, y, z, material in BOXES:
        lines.append("o " + name)
        for px, py, pz in box_corners(x, y, z):
            lines.append("v %.6f %.6f %.6f" % (px, pz, -py))
        lines.append("usemtl " + material)
        lines.append("s off")
        for face in FACES:
            corners = " ".join("%d/%d" % (base + corner + 1, slot + 1) for slot, corner in enumerate(face))
            lines.append("f " + corners)
        base += 8
    return "\n".join(lines) + "\n"


# Builds the text of chair.mtl.
def chair_mtl():
    lines = [
        "# Materials of the sample chair",
        "newmtl Wood",
        "Kd 0.800000 0.800000 0.800000",
        "map_Kd chair_wood.png",
        "",
        "newmtl Fabric",
        "Kd 0.450000 0.120000 0.100000",
        "",
    ]
    return "\n".join(lines)


# Packs one PNG chunk.
def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


# Builds a 64 by 64 wood stripe picture with fixed values.
def wood_png():
    size = 64
    seed = 12345
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            noise = (seed >> 16) % 9 - 4
            stripe = 18 if (x // 6) % 3 == 0 else 0
            wobble = 6 if ((x + y // 5) // 11) % 2 == 0 else 0
            base = 150 - stripe + wobble + noise
            rows += bytes([max(0, min(255, base + 40)), max(0, min(255, base)), max(0, min(255, base - 60))])
    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + png_chunk(b"IEND", b""))


# Writes bytes to a file, making the folder when needed.
def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


# Writes the sample chair files next to this script.
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    folder = os.path.join(here, "chair")
    write_file(os.path.join(folder, "chair.obj"), chair_obj().encode("ascii"))
    write_file(os.path.join(folder, "chair.mtl"), chair_mtl().encode("ascii"))
    write_file(os.path.join(folder, "chair_wood.png"), wood_png())
    print("Wrote the sample chair to " + folder)


if __name__ == "__main__":
    sys.exit(main())
