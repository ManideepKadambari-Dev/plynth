import json
import os
import unittest

import fab_helpers
import helpers  # noqa: F401
from material_helpers import BACKGROUND, MaterialTest
from plynth import registry
from plynth.materials import sources, read_size
from plynth.stores import fab_materials

OAK_2K = ["Oak/T_Oak_%s_2K.png" % c for c in ("BaseColor", "Normal", "Roughness")]
OAK_4K = ["Oak/T_Oak_%s_4K.png" % c for c in ("BaseColor", "Normal", "Roughness")]
OAK_JSON = "Oak/Oak.json"
FLAT_STEM = "Wooden_Chair_Old_01_abc123_High_4K_"


class FabMaterialTest(MaterialTest):
    # Writes a material source.fab.json beside the test files and returns its path.
    def write_material_source(self, zip_file, files, textures, metadata=None, **fields):
        source = {
            "format": "plynth-fab-source", "version": 1, "zip": zip_file.replace("\\", "/"), "kind": "material",
            "unpack_folder": self.path("work", "unpacked"), "files": sorted(files), "model": None,
            "model_format": None, "textures": sorted(textures), "metadata": metadata, "maps": {}, "normal": "auto",
        }
        source.update(fields)
        path = self.path("work", "source.fab.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(source, handle)
        return path

    # Cleans a Fab source file as a material.
    def clean_fab(self, path, **kwargs):
        return self.clean(path, kind="fab", **kwargs)

    # Returns the warning codes that are about matching textures, leaving out the quality checks that tiny
    # test images always trigger.
    def matching_warnings(self, result):
        return [c for c in self.codes(result) if c not in ("visible_seams", "low_resolution")]

    # Returns the files under the unpack folder as entry names.
    def unpacked(self):
        root = self.path("work", "unpacked")
        return sorted(os.path.relpath(os.path.join(c, n), root).replace("\\", "/")
                      for c, _d, files in os.walk(root) for n in files)


class OakTests(FabMaterialTest):
    # The source is a registry entry.
    def test_registered(self):
        registry.discover(sources)
        self.assertIn("fab", registry.MATERIAL_SOURCES.ids())

    # The 2K set is used byte for byte, the 4K files and the preview are not unpacked, and the metadata gives
    # the name and the tile size.
    def test_two_k_set(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K + [OAK_JSON], OAK_2K, OAK_JSON)
        result = self.clean_ok(source, kind="fab", tile_size=None)
        entries = fab_helpers.zip_entries("oak_material.zip")
        for channel, entry in (("base_color", OAK_2K[0]), ("normal", OAK_2K[1]), ("roughness", OAK_2K[2])):
            self.assertEqual(self.read_bytes(self.map_path(result, channel)), entries[entry], channel)
        self.assertEqual(self.unpacked(), sorted(OAK_2K + [OAK_JSON]))
        self.assertEqual(self.matching_warnings(result), [])
        output = result["output"]
        self.assertEqual(output["source_info"], {"name": "Oak"})
        self.assertEqual(output["tile_size"], {"width": 2.0, "height": 2.0})
        self.assertEqual(output["texture_size"], {"width": 64, "height": 64})
        self.assertIsNone(output["picked_material"])

    # The job's tile size wins over the metadata's.
    def test_job_tile_size_wins(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K + [OAK_JSON], OAK_2K, OAK_JSON)
        result = self.clean_ok(source, kind="fab", tile_size={"width": 0.5, "height": 0.25})
        self.assertEqual(result["output"]["tile_size"], {"width": 0.5, "height": 0.25})

    # Both resolutions listed: the larger one wins without a warning for the smaller.
    def test_both_resolutions(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        textures = OAK_2K + OAK_4K
        source = self.write_material_source(zip_file, textures, textures)
        result = self.clean_ok(source, kind="fab")
        self.assertEqual(result["output"]["texture_size"], {"width": 96, "height": 96})
        self.assertIsNone(result["output"]["source_info"])

    # A map picked under * wins over the name match.
    def test_picked_map_wins(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        picked = "Oak/T_Oak_BaseColor_4K.png"
        source = self.write_material_source(zip_file, OAK_2K + [picked], OAK_2K + [picked],
                                            maps={"*": {"roughness": picked}})
        result = self.clean_ok(source, kind="fab")
        entries = fab_helpers.zip_entries("oak_material.zip")
        self.assertEqual(self.read_bytes(self.map_path(result, "roughness")), entries[picked])
        self.assertEqual(result["output"]["maps"]["roughness"]["from"], "file")

    # A pick that is not in the zip is import_failed.
    def test_picked_map_missing(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K, OAK_2K, maps={"*": {"roughness": "Oak/none.png"}})
        result = self.clean(source, kind="fab")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "import_failed")

    # The thumbnail renders with Eevee.
    def test_thumbnail_renders(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K + [OAK_JSON], OAK_2K, OAK_JSON)
        result = self.clean_ok(source, kind="fab",
                               thumbnail={"render": True, "size": 128, "background": BACKGROUND})
        thumb = result["output"]["thumbnail"]
        self.assertIsNone(thumb["error"])
        self.assertIn("EEVEE", thumb["engine"].upper())
        self.assertEqual(read_size(os.path.join(self.out_folder, "thumb.png")), (128, 128))

    # An entry with .. in the files list writes nothing outside the folder.
    def test_unsafe_entry(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K + ["../evil.png"], OAK_2K + ["../evil.png"])
        result = self.clean(source, kind="fab")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "unpack_failed")
        self.assertFalse(os.path.exists(self.path("work", "evil.png")))

    # A source file for a model is refused.
    def test_wrong_kind(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K, OAK_2K, kind="model")
        result = self.clean(source, kind="fab")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "import_failed")

    # A set with no base color is no_base_color.
    def test_no_base_color(self):
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K[1:], OAK_2K[1:])
        result = self.clean(source, kind="fab")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "no_base_color")


class FlatTests(FabMaterialTest):
    # The flat Megascans layout: BaseColor beats Diffuse, Cavity is ignored, jpg maps are copied as they are.
    def test_flat_zip(self):
        entries = fab_helpers.zip_entries("chair_flat.zip")
        textures = sorted(name for name in entries if name.endswith(".jpg"))
        self.assertTrue(textures)
        zip_file = fab_helpers.zip_path("chair_flat.zip")
        source = self.write_material_source(zip_file, textures + ["abc123.json"], textures, "abc123.json")
        result = self.clean_ok(source, kind="fab", tile_size=None)
        self.assertEqual(self.matching_warnings(result), [])
        base = FLAT_STEM + "BaseColor.jpg"
        self.assertEqual(self.read_bytes(self.map_path(result, "base_color")), entries[base])
        self.assertEqual(result["output"]["maps"]["base_color"]["file"], "textures/base_color.jpg")
        self.assertEqual(result["output"]["source_info"], {"name": "Wooden Chair Old 01"})
        self.assertEqual(result["output"]["tile_size"], {"width": 0.45, "height": 0.5})

    # A DirectX normal given by the file has its green flipped.
    def test_directx_flag(self):
        entries = fab_helpers.zip_entries("oak_material.zip")
        zip_file = fab_helpers.zip_path("oak_material.zip")
        source = self.write_material_source(zip_file, OAK_2K, OAK_2K, normal="directx")
        result = self.clean_ok(source, kind="fab")
        made = self.pixels(self.map_path(result, "normal"))
        original = self.pixels(self.path("work", "unpacked", *OAK_2K[1].split("/")))
        self.assertEqual(entries[OAK_2K[1]][:4], b"\x89PNG")
        self.assertTrue((made[:, :, 1] == 255 - original[:, :, 1]).all())


class UnitsTests(unittest.TestCase):
    # Sizes in other units become metres.
    def test_parse_size(self):
        from plynth.materials.sources import fab
        self.assertEqual(fab.parse_size("2x2"), (2.0, 2.0))
        self.assertEqual(fab.parse_size("60 x 60 cm"), (0.6, 0.6))
        self.assertIsNone(fab.parse_size("big"))
        self.assertIsNone(fab.parse_size("0x0"))
        self.assertEqual(fab.find_size({"meta": [{"key": "scanArea", "value": "1.5x1 m"}]}), (1.5, 1.0))
        self.assertIsNone(fab.find_size({"meta": "x"}))

    # The material helper is shared with the model importer.
    def test_helper_present(self):
        self.assertTrue(callable(fab_materials.make_sets))


if __name__ == "__main__":
    unittest.main()
