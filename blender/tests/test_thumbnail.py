import hashlib
import os
import struct
import unittest
import zlib

from cleanup_helpers import BACKGROUND, CleanupTest


# Reads an 8 bit RGB or RGBA PNG and returns (width, height, rows) where each row is a list of pixel tuples.
def read_png(path):
    with open(path, "rb") as handle:
        data = handle.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    position = 8
    header = None
    compressed = b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        position += 12 + length
    width, height, depth, color_type = header[0], header[1], header[2], header[3]
    assert depth == 8 and color_type in (2, 6), "expected 8 bit RGB or RGBA"
    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(compressed)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    index = 0
    for _ in range(height):
        filter_type = raw[index]
        line = bytearray(raw[index + 1:index + 1 + stride])
        index += 1 + stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 255
            elif filter_type == 2:
                line[i] = (line[i] + up) & 255
            elif filter_type == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif filter_type == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else upper_left)
                line[i] = (line[i] + predictor) & 255
        rows.append([tuple(line[x * channels:x * channels + 3]) for x in range(width)])
        previous = line
    return width, height, rows


class ThumbnailTests(CleanupTest):
    # Cleans the chair with a thumbnail and returns the result.
    def chair_with_thumbnail(self, size=128):
        chair = self.copy_chair()
        spec = {"render": True, "size": size, "background": BACKGROUND}
        return self.cleanup(os.path.join(chair, "chair.obj"), "obj", thumbnail=spec)

    # Checks the picture: square, corners equal the background, centre is something else.
    def assert_picture(self, path, size):
        width, height, rows = read_png(path)
        self.assertEqual((width, height), (size, size))
        wanted = [round(c * 255) for c in BACKGROUND]
        for x, y in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)):
            for channel in range(3):
                self.assertLessEqual(abs(rows[y][x][channel] - wanted[channel]), 2, (x, y))
        centre = rows[height // 2][width // 2]
        self.assertGreater(sum(abs(centre[c] - wanted[c]) for c in range(3)), 40)

    # An Eevee thumbnail of the cleaned chair has the right size, background and content.
    def test_eevee_thumbnail(self):
        result = self.chair_with_thumbnail()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        thumb = result["output"]["thumbnail"]
        self.assertEqual(thumb, {"file": "thumb.png", "engine": "eevee", "error": None})
        self.assertEqual(result["warnings"], [])
        self.assert_picture(self.path("item", "thumb.png"), 128)
        print("EEVEE_TIMING", [line for line in self.printed.splitlines() if "Thumbnail rendered" in line])

    # The thumbnail is rendered after asset.blend is saved, and no half written file is left.
    def test_no_leftovers(self):
        self.chair_with_thumbnail()
        self.assertEqual(sorted(os.listdir(self.path("item"))), ["asset.blend", "asset.fbx", "textures", "thumb.png"])

    # Not asking for a thumbnail renders nothing and reports no error.
    def test_render_off(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        self.assertEqual(result["output"]["thumbnail"], {"file": None, "engine": None, "error": None})
        self.assertFalse(os.path.exists(self.path("item", "thumb.png")))

    # The retry task measures the chair, writes the picture and leaves asset.blend byte for byte unchanged.
    def test_render_thumbnail_task(self):
        self.chair_with_thumbnail(64)
        blend = self.path("item", "asset.blend")
        before = hashlib.sha256(open(blend, "rb").read()).hexdigest()
        target = self.path("out", "again.png")
        os.makedirs(self.path("out"))
        spec = {"render": True, "size": 128, "background": BACKGROUND}
        result = self.run_task("render_thumbnail", {"blend_file": blend, "output_file": target, "measure": True, "fbx_file": None,
                                                   "thumbnail": spec}, job_id="retry-1")
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertAlmostEqual(output["size"]["width"], 0.45, delta=1e-4)
        self.assertAlmostEqual(output["size"]["depth"], 0.50, delta=1e-4)
        self.assertAlmostEqual(output["size"]["height"], 0.90, delta=1e-4)
        self.assertEqual(output["triangles"], 72)
        self.assertEqual(output["objects"], 6)
        self.assertEqual(output["materials"], ["Fabric", "Wood"])
        self.assertIn("textures/maps/wood/base_color.png", output["textures"])
        self.assertNotIn("textures/chair_wood.png", output["textures"])
        self.assertEqual(output["thumbnail"], {"file": "again.png", "engine": "eevee", "error": None})
        self.assert_picture(target, 128)
        self.assertEqual(hashlib.sha256(open(blend, "rb").read()).hexdigest(), before)
        self.assertEqual(sorted(os.listdir(self.path("item"))), ["asset.blend", "asset.fbx", "textures", "thumb.png"])

    # Without measure the measures are null.
    def test_render_thumbnail_no_measure(self):
        self.chair_with_thumbnail(64)
        spec = {"render": True, "size": 64, "background": BACKGROUND}
        result = self.run_task("render_thumbnail", {"blend_file": self.path("item", "asset.blend"),
                                                   "output_file": self.path("again.png"), "measure": False, "fbx_file": None,
                                                   "thumbnail": spec})
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["size"])
        self.assertIsNone(result["output"]["triangles"])
        self.assert_result_valid(result)

    # A blend file that is not there fails the retry task with import_failed.
    def test_render_thumbnail_missing_file(self):
        spec = {"render": True, "size": 64, "background": BACKGROUND}
        result = self.run_task("render_thumbnail", {"blend_file": self.path("none.blend"),
                                                   "output_file": self.path("again.png"), "measure": False, "fbx_file": None,
                                                   "thumbnail": spec})
        self.assertEqual(result["error"]["code"], "import_failed")


if __name__ == "__main__":
    unittest.main()
