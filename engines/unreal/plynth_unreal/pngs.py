import struct
import zlib


# Returns one PNG chunk with its length and checksum.
def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


# Writes a small image of one color (red, green, blue from 0 to 255) as an 8 bit RGB PNG file.
def write_solid(path, rgb, size=4):
    row = b"\x00" + bytes(rgb) * size
    pixels = zlib.compress(row * size, 9)
    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", pixels))
        handle.write(chunk(b"IEND", b""))
