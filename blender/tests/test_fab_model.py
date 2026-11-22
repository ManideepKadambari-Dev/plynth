import os
import unittest
import zipfile

import numpy as np

import fab_helpers
from fab_helpers import FabTest
from plynth.materials import read_pixels
from plynth.stores import fab_names

VARIANT = "Chair/FBX/Medium/"
MEDIUM_TEXTURES = ["Chair/FBX/Medium/Textures/T_Chair_%s_2K.png" % c
                   for c in ("BaseColor", "Normal", "Roughness", "Metallic")]
UNLINKED = ["Wood_BaseColor.png", "Wood_Normal.png", "Wood_Roughness.png",
            "Fabric_BaseColor.png", "Fabric_Normal.png", "Fabric_Roughness.png"]


class VariantsTests(FabTest):
    # The picked FBX gives the chair at its size, and only the listed entries are unpacked.
    def test_variants_cleanup(self):
        zip_file = fab_helpers.zip_path("chair_variants.zip")
        files = [VARIANT + "Chair_LOD0.fbx"] + MEDIUM_TEXTURES
        source = self.write_source(zip_file, VARIANT + "Chair_LOD0.fbx", files, MEDIUM_TEXTURES)
        result = self.cleanup_fab(source)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        size = result["output"]["size"]
        self.assertAlmostEqual(size["width"], 0.45, delta=1e-3)
        self.assertAlmostEqual(size["depth"], 0.50, delta=1e-3)
        self.assertAlmostEqual(size["height"], 0.90, delta=1e-3)
        self.assertEqual(result["output"]["picked_file"], VARIANT + "Chair_LOD0.fbx")
        self.assertEqual(self.unpacked(), sorted(files))
        self.assertFalse(os.path.exists(self.path("work", "unpacked", "Chair", "FBX", "High")))
        self.assertEqual(result["warnings"], [])
        self.assertEqual(result["output"]["materials"], ["Fabric", "Wood"])
        self.assertEqual(sorted(result["output"]["textures"]), sorted(
            "textures/maps/%s/%s.png" % (key, channel) for key in ("fabric", "wood")
            for channel in ("base_color", "normal", "roughness", "metallic")))

    # The one map set of the chair feeds both of its materials with the four channels.
    def test_single_set_feeds_every_material(self):
        zip_file = fab_helpers.zip_path("chair_variants.zip")
        files = [VARIANT + "Chair_LOD0.fbx"] + MEDIUM_TEXTURES
        result = self.cleanup_fab(self.write_source(zip_file, VARIANT + "Chair_LOD0.fbx", files, MEDIUM_TEXTURES))
        self.assertTrue(result["ok"], result["error"])
        self.open_asset()
        images = self.material_images(result)
        for name in ("Wood", "Fabric"):
            self.assertEqual(images[name], {"Base Color": "T_Chair_BaseColor_2K.png",
                                            "Normal": "T_Chair_Normal_2K.png",
                                            "Roughness": "T_Chair_Roughness_2K.png",
                                            "Metallic": "T_Chair_Metallic_2K.png"})

    # A glTF model with its .bin is read the same way.
    def test_gltf_variant(self):
        zip_file = fab_helpers.zip_path("chair_variants.zip")
        base = "Chair/glTF/Medium/"
        textures = [base + "Textures/T_Chair_%s_2K.png" % c for c in ("BaseColor", "Normal", "Roughness")]
        files = [base + "Chair.gltf", base + "Chair.bin"] + textures
        result = self.cleanup_fab(self.write_source(zip_file, base + "Chair.gltf", files, textures))
        self.assertTrue(result["ok"], result["error"])
        size = result["output"]["size"]
        self.assertAlmostEqual(size["height"], 0.90, delta=1e-3)
        self.open_asset()
        self.assertEqual(self.material_images(result)["Wood"]["Base Color"], "T_Chair_BaseColor_2K.png")

    # The flat layout of a Megascans download: one set, a Diffuse beside the BaseColor and a Cavity map
    # that is not used. Nothing is warned.
    def test_flat_layout(self):
        zip_file = fab_helpers.zip_path("chair_flat.zip")
        stem = "Wooden_Chair_Old_01_abc123"
        textures = ["%s_High_4K_%s.jpg" % (stem, c) for c in ("BaseColor", "Normal", "Roughness", "Diffuse", "Cavity")]
        files = [stem + "_High.fbx"] + textures
        result = self.cleanup_fab(self.write_source(zip_file, stem + "_High.fbx", files, textures))
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["warnings"], [])
        self.open_asset()
        images = self.material_images(result)
        self.assertEqual(images["Wood"]["Base Color"], stem + "_High_4K_BaseColor.jpg")
        self.assertEqual(images["Wood"]["Roughness"], stem + "_High_4K_Roughness.jpg")
        self.assertNotIn("Metallic", images["Wood"])


class UnlinkedTests(FabTest):
    # Runs the unlinked chair with the given textures and returns the result.
    def run_unlinked(self, textures=UNLINKED, **fields):
        zip_file = fab_helpers.zip_path("chair_unlinked.zip")
        source = self.write_source(zip_file, "Chair.fbx", ["Chair.fbx"] + list(textures), textures, **fields)
        return self.cleanup_fab(source)

    # Each material takes the maps named after it.
    def test_maps_by_material_name(self):
        result = self.run_unlinked()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["warnings"], [])
        self.open_asset()
        images = self.material_images(result)
        for name in ("Wood", "Fabric"):
            self.assertEqual(images[name], {"Base Color": name + "_BaseColor.png", "Normal": name + "_Normal.png",
                                            "Roughness": name + "_Roughness.png"})

    # One material and one map set not named after it are joined.
    def test_single_set_and_one_material(self):
        zip_file = fab_helpers.zip_path("pack.zip")
        textures = ["Box/Textures/T_Slate_%s_2K.png" % c for c in ("BaseColor", "Normal", "Roughness", "Metallic")]
        result = self.cleanup_fab(self.write_source(zip_file, "Box/Box.fbx", ["Box/Box.fbx"] + textures, textures))
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["warnings"], [])
        self.open_asset()
        self.assertEqual(self.material_images(result)["Stone"]["Base Color"], "T_Slate_BaseColor_2K.png")

    # A texture no material took gives unused_image.
    def test_unused_image(self):
        entries = fab_helpers.zip_entries("chair_unlinked.zip")
        entries["Sticker.png"] = fab_helpers.small_png()
        archive = self.write_entries_zip("with_sticker.zip", entries)
        textures = UNLINKED + ["Sticker.png"]
        source = self.write_source(archive, "Chair.fbx", ["Chair.fbx"] + textures, textures)
        result = self.cleanup_fab(source)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(self.codes(result), ["unused_image"])
        self.assertIn("Sticker.png", result["warnings"][0]["message"])

    # A material with a set but no base color gives missing_texture and keeps its look.
    def test_missing_base_color(self):
        textures = ["Wood_BaseColor.png", "Wood_Normal.png", "Fabric_Roughness.png"]
        result = self.run_unlinked(textures)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(self.codes(result), ["missing_texture"])
        self.assertIn("Fabric", result["warnings"][0]["message"])
        self.open_asset()
        self.assertEqual(self.material_images(result)["Wood"]["Base Color"], "Wood_BaseColor.png")
        self.assertEqual(self.material_images(result)["Fabric"], {})

    # Maps picked for a material win over the name match, and "*" applies to the others.
    def test_picked_maps_win(self):
        maps = {"Wood": {"base_color": "Fabric_Roughness.png"}, "*": {"normal": "Fabric_Normal.png"}}
        result = self.run_unlinked(maps=maps)
        self.assertTrue(result["ok"], result["error"])
        self.open_asset()
        images = self.material_images(result)
        self.assertEqual(images["Wood"]["Base Color"], "Fabric_Roughness.png")
        self.assertEqual(images["Wood"]["Roughness"], "Wood_Roughness.png")
        self.assertEqual(images["Wood"]["Normal"], "Fabric_Normal.png")
        self.assertEqual(images["Fabric"]["Base Color"], "Fabric_BaseColor.png")
        self.assertEqual(images["Fabric"]["Normal"], "Fabric_Normal.png")

    # A DirectX normal map has its green channel flipped.
    def test_directx_normal_is_flipped(self):
        result = self.run_unlinked(normal="directx")
        self.assertTrue(result["ok"], result["error"])
        original = read_pixels(self.path("work", "unpacked", "Wood_Normal.png"))
        flipped = read_pixels(self.map_path("wood", "normal"))
        self.assertTrue(np.allclose(flipped[:, :, 1], 1.0 - original[:, :, 1], atol=1.5 / 255))
        self.assertTrue(np.allclose(flipped[:, :, 0], original[:, :, 0], atol=1.5 / 255))

    # Auto reads the convention from the name.
    def test_auto_normal_reads_the_name(self):
        entries = {"Chair.fbx": fab_helpers.zip_entries("chair_unlinked.zip")["Chair.fbx"],
                   "Wood_BaseColor.png": fab_helpers.small_png(),
                   "Wood_Normal_DX.png": fab_helpers.small_png(60)}
        archive = self.write_entries_zip("dx.zip", entries)
        textures = ["Wood_BaseColor.png", "Wood_Normal_DX.png"]
        result = self.cleanup_fab(self.write_source(archive, "Chair.fbx", ["Chair.fbx"] + textures, textures))
        self.assertTrue(result["ok"], result["error"])
        flipped = read_pixels(self.map_path("wood", "normal"))
        self.assertAlmostEqual(float(flipped[0, 0, 1]), 1.0 - 60 / 255, delta=2 / 255)

    # A glossiness map becomes a roughness map of one minus its value.
    def test_gloss_becomes_roughness(self):
        entries = {"Chair.fbx": fab_helpers.zip_entries("chair_unlinked.zip")["Chair.fbx"],
                   "Wood_BaseColor.png": fab_helpers.small_png(),
                   "Wood_Gloss.png": fab_helpers.small_png(51)}
        archive = self.write_entries_zip("gloss.zip", entries)
        textures = ["Wood_BaseColor.png", "Wood_Gloss.png"]
        result = self.cleanup_fab(self.write_source(archive, "Chair.fbx", ["Chair.fbx"] + textures, textures))
        self.assertTrue(result["ok"], result["error"])
        rough = read_pixels(self.map_path("wood", "roughness"))
        self.assertAlmostEqual(float(rough[0, 0, 0]), 1.0 - 51 / 255, delta=2 / 255)

    # An entry that climbs out of the folder gives unpack_failed and nothing is written outside.
    def test_unsafe_entry(self):
        archive = self.write_entries_zip("evil.zip", {"Chair.fbx": b"x", "../evil.png": fab_helpers.small_png()})
        source = self.write_source(archive, "Chair.fbx", ["Chair.fbx", "../evil.png"])
        result = self.cleanup_fab(source)
        self.assertEqual(result["error"]["code"], "unpack_failed")
        self.assertFalse(os.path.exists(self.path("evil.png")))
        self.assertFalse(os.path.exists(self.path("work", "evil.png")))
        self.assertEqual(self.unpacked(), [])
        self.assert_result_valid(result)

    # A listed entry that is not in the zip gives unpack_failed.
    def test_missing_entry(self):
        zip_file = fab_helpers.zip_path("chair_unlinked.zip")
        result = self.cleanup_fab(self.write_source(zip_file, "Chair.fbx", ["Chair.fbx", "Nope.png"]))
        self.assertEqual(result["error"]["code"], "unpack_failed")

    # A source file that is not valid gives import_failed.
    def test_bad_source(self):
        zip_file = fab_helpers.zip_path("chair_unlinked.zip")
        result = self.cleanup_fab(self.write_source(zip_file, "Chair.fbx", ["Chair.fbx"], model_format="stl"))
        self.assertEqual(result["error"]["code"], "import_failed")
        self.assert_result_valid(result)


class UnpackTests(FabTest):
    # Only the named members are unpacked.
    def test_members(self):
        from plynth.importers import zip as zip_importer
        names = zip_importer.unpack(fab_helpers.zip_path("oak_material.zip"), self.path("out"),
                                    ["Oak/T_Oak_BaseColor_2K.png"])
        self.assertEqual(names, ["Oak/T_Oak_BaseColor_2K.png"])
        self.assertTrue(os.path.isfile(self.path("out", "Oak", "T_Oak_BaseColor_2K.png")))
        self.assertFalse(os.path.exists(self.path("out", "Oak", "T_Oak_BaseColor_4K.png")))

    # An unsafe zip entry that is not listed does not matter, a listed one does.
    def test_unsafe_member_only_when_listed(self):
        from plynth.importers import zip as zip_importer
        from plynth.jobs import WorkerError
        archive = self.write_entries_zip("odd.zip", {"a.png": b"1", "../b.png": b"2"})
        self.assertEqual(zip_importer.unpack(archive, self.path("out"), ["a.png"]), ["a.png"])
        with self.assertRaises(WorkerError):
            zip_importer.unpack(archive, self.path("out2"), ["../b.png"])
        self.assertFalse(os.path.exists(self.path("b.png")))


class NameTests(FabTest):
    # Resolution words, written as 4K or as pixels.
    def test_resolution(self):
        self.assertEqual(fab_names.resolution_of("Textures/T_Chair_BaseColor_2K.png"), 2048)
        self.assertEqual(fab_names.resolution_of("Item_High_4K_Normal.jpg"), 4096)
        self.assertEqual(fab_names.resolution_of("T_x_05K.png"), 512)
        self.assertEqual(fab_names.resolution_of("rock_diff_1024.png"), 1024)
        self.assertIsNone(fab_names.resolution_of("Wood_Normal.png"))

    # Channel words of Fab packs.
    def test_classify(self):
        self.assertEqual(fab_names.classify("Item_High_4K_BaseColor.jpg")[0], "base_color")
        self.assertEqual(fab_names.classify("Item_High_4K_Diffuse.jpg")[0], "base_color")
        self.assertEqual(fab_names.classify("Item_High_4K_Diffuse.jpg")[2], 0)
        self.assertEqual(fab_names.classify("Item_High_4K_BaseColor.jpg")[2], 1)
        self.assertEqual(fab_names.classify("Item_High_4K_Gloss.jpg")[0], "gloss")
        self.assertEqual(fab_names.classify("Item_High_4K_Cavity.jpg")[0], "ignored")
        self.assertEqual(fab_names.classify("Item_High_4K_Bump.jpg")[0], "ignored")
        self.assertEqual(fab_names.classify("Item_High_4K_Displacement.jpg")[0], "ignored")
        self.assertEqual(fab_names.classify("Item_High_4K_Metalness.jpg")[0], "metallic")
        self.assertEqual(fab_names.classify("Item_High_4K_AO.jpg")[0], "occlusion")
        self.assertEqual(fab_names.classify("T_Oak_Orm.png")[0], "packed")
        self.assertEqual(fab_names.classify("T_Oak_Nrm_DX.png")[:2], ("normal", "directx"))
        self.assertEqual(fab_names.classify("Item_BentNormal.png")[0], "ignored")
        self.assertIsNone(fab_names.classify("Sticker.png")[0])

    # A texture and a material share a key when the name without channel, size, quality and prefix is equal.
    def test_keys(self):
        self.assertEqual(fab_names.set_key("T_Chair_BaseColor_2K.png"), "chair")
        self.assertEqual(fab_names.set_key("Chair/Textures/T_Chair_Normal_DX_4K.png"), "chair")
        self.assertEqual(fab_names.set_key("Item_Old_01_abc_High_4K_Roughness.jpg"), "item_old_01_abc")
        self.assertEqual(fab_names.set_key("MI_Wood_Roughness.png"), "wood")
        self.assertEqual(fab_names.material_key("MAT_Wood.001"), "wood")
        self.assertEqual(fab_names.material_key("M_Wood"), "wood")


class ZipToolTests(FabTest):
    # Two runs of the zip maker give the same bytes.
    def test_zips_are_repeatable(self):
        first = fab_helpers.make_fab_zips.make_zips(self.path("one"))
        second = fab_helpers.make_fab_zips.make_zips(self.path("two"))
        self.assertEqual(sorted(first), sorted(second))
        for name in first:
            self.assertEqual(fab_helpers.make_fab_zips.digest(first[name]),
                             fab_helpers.make_fab_zips.digest(second[name]), name)

    # The variants zip has the layout the plan describes.
    def test_variants_layout(self):
        with zipfile.ZipFile(fab_helpers.zip_path("chair_variants.zip")) as archive:
            names = archive.namelist()
        for expected in ("Chair/FBX/Low/Chair_LOD0.fbx", "Chair/FBX/Medium/Chair_LOD1.fbx",
                         "Chair/glTF/Medium/Chair.bin", "Chair/Previews/front.png", "Chair/Chair.json",
                         "Chair/FBX/High/Textures/T_Chair_Normal_1K.png",
                         "Chair/FBX/High/Textures/T_Chair_Normal_2K.png"):
            self.assertIn(expected, names)


if __name__ == "__main__":
    unittest.main()
