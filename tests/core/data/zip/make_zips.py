"""Makes the small zips used by ZipListingTests.cpp and prints the names Python's zipfile reads from each,
into a .txt file beside it. Run once; the results are committed."""
import struct
import sys
import zipfile
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
FIXED = (2020, 1, 1, 0, 0, 0)


def info(name):
    zi = zipfile.ZipInfo(name, FIXED)
    zi.compress_type = zipfile.ZIP_DEFLATED
    return zi


def finish(path):
    with zipfile.ZipFile(path) as zf:
        names = zf.namelist()
    path.with_suffix(".txt").write_text("\n".join(names) + "\n", encoding="utf-8", newline="\n")


with zipfile.ZipFile(out / "deflated.zip", "w") as zf:
    zf.writestr(info("Chair/"), b"")
    zf.writestr(info("Chair/Chair.fbx"), b"fbx data " * 40)
    zf.writestr(info("Chair/Textures/T_Chair_BaseColor_2K.png"), b"png data " * 40)
finish(out / "deflated.zip")

with zipfile.ZipFile(out / "utf8.zip", "w") as zf:
    zf.writestr(info("café/stühl.obj"), b"o chair\n")
finish(out / "utf8.zip")

# A name in code page 437: write an ASCII placeholder, then patch the byte in both headers.
with zipfile.ZipFile(out / "cp437.zip", "w") as zf:
    zf.writestr(info("cafX.txt"), b"hello")
raw = (out / "cp437.zip").read_bytes().replace(b"cafX.txt", b"caf\x82.txt")
(out / "cp437.zip").write_bytes(raw)
finish(out / "cp437.zip")

with zipfile.ZipFile(out / "encrypted.zip", "w") as zf:
    zf.writestr(info("secret.fbx"), b"not really encrypted")
    zf.writestr(info("plain.fbx"), b"plain")
# Set the encryption flag of the first entry in its local and central headers (the data stays plain).
raw = bytearray((out / "encrypted.zip").read_bytes())
raw[6] |= 1
raw[raw.index(b"PK") + 8] |= 1
(out / "encrypted.zip").write_bytes(bytes(raw))
finish(out / "encrypted.zip")


def zip64_zip(path, name, data):
    crc = zipfile.crc32(data)
    local = struct.pack("<IHHHHHIIIHH", 0x04034B50, 45, 0, 0, 0, 0x21, crc, len(data), len(data), len(name), 0) + name
    body = local + data
    extra = struct.pack("<HHQQ", 0x0001, 16, len(data), len(data))
    central = struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45, 0, 0, 0, 0x21, crc, 0xFFFFFFFF, 0xFFFFFFFF,
                          len(name), len(extra), 0, 0, 0, 0, 0) + name + extra
    cd_offset = len(body)
    z64_pos = cd_offset + len(central)
    z64_eocd = struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0, 1, 1, len(central), cd_offset)
    locator = struct.pack("<IIQI", 0x07064B50, 0, z64_pos, 1)
    eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)
    path.write_bytes(body + central + z64_eocd + locator + eocd)


zip64_zip(out / "zip64.zip", b"big/Model.fbx", b"zip64 entry data")
finish(out / "zip64.zip")

good = (out / "deflated.zip").read_bytes()
cd_start = good.rindex(b"PK\x01\x02")
cut = good[:20] + good[cd_start:]
(out / "cutoff.zip").write_bytes(cut)
(out / "notzip.txt.bin").write_bytes(b"This is just a text file, not a zip.\n" * 4)
