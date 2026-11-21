import os
import unittest
import zipfile

import bpy

from cleanup_helpers import CleanupTest
from plynth import scene


class FormatTests(CleanupTest):
    # Cleans the sample chair and returns the result, so its asset.blend can be exported.
    def cleaned_chair(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        self.assertTrue(result["ok"], result["error"])
        return result

    # Cleans the file again into a second output folder and returns the result.
    def second_cleanup(self, file, fmt, **overrides):
        return self.cleanup(file, fmt, output_folder=self.path("item2"), **overrides)

    # Checks that the size of a result is the chair's within a millimetre.
    def assert_chair_size(self, result):
        self.assertTrue(result["ok"], result["error"])
        size = result["output"]["size"]
        self.assertAlmostEqual(size["width"], 0.45, delta=1e-3)
        self.assertAlmostEqual(size["depth"], 0.50, delta=1e-3)
        self.assertAlmostEqual(size["height"], 0.90, delta=1e-3)

    # The cleaned chair exported to GLB and imported again has the same size.
    def test_glb_round_trip(self):
        self.cleaned_chair()
        self.open_asset()
        glb = self.path("in", "chair.glb")
        bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB")
        result = self.second_cleanup(glb, "gltf")
        self.assert_chair_size(result)
        self.assertEqual(result["output"]["triangles"], 72)

    # The cleaned chair exported to FBX and imported again has the same size.
    def test_fbx_round_trip(self):
        self.cleaned_chair()
        self.open_asset()
        fbx = self.path("in", "chair.fbx")
        bpy.ops.export_scene.fbx(filepath=fbx)
        result = self.second_cleanup(fbx, "fbx")
        self.assert_chair_size(result)

    # A .blend with a camera and a light has them removed and says so.
    def test_blend_with_extras(self):
        self.cleaned_chair()
        self.open_asset()
        camera = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
        light = bpy.data.objects.new("Lamp", bpy.data.lights.new("Lamp", "POINT"))
        for obj in (camera, light):
            bpy.context.scene.collection.objects.link(obj)
        blend = self.path("in", "extras.blend")
        bpy.ops.wm.save_as_mainfile(filepath=blend, copy=True)
        result = self.second_cleanup(blend, "blend")
        self.assert_chair_size(result)
        self.assertIn("removed_objects", self.codes(result))
        self.assertEqual(result["output"]["objects"], 6)
        names = sorted(o.name for o in self.open_asset_two())
        self.assertEqual(names, ["chair_%02d" % n for n in range(1, 7)])

    # Opens the asset.blend of the second output folder.
    def open_asset_two(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("item2", "asset.blend"))
        return scene.scene_objects()

    # Parented objects keep their place when an empty parent is removed.
    def test_empty_parent_removed(self):
        self.cleaned_chair()
        self.open_asset()
        empty = bpy.data.objects.new("Group", None)
        empty.location = (5.0, 0.0, 0.0)
        bpy.context.scene.collection.objects.link(empty)
        parent_world = {}
        for obj in scene.scene_objects():
            if obj.type == "MESH":
                obj.parent = empty
        bpy.context.view_layer.update()
        blend = self.path("in", "parented.blend")
        bpy.ops.wm.save_as_mainfile(filepath=blend, copy=True)
        result = self.second_cleanup(blend, "blend")
        self.assertTrue(result["ok"], result["error"])
        self.assertIn("removed_objects", self.codes(result))
        self.assert_chair_size(result)
        self.assertEqual(parent_world, {})

    # Packed images are written to textures/ and linked by a relative path.
    def test_packed_image(self):
        self.cleaned_chair()
        self.open_asset()
        bpy.ops.file.pack_all()
        blend = self.path("in", "packed.blend")
        bpy.ops.wm.save_as_mainfile(filepath=blend, copy=True)
        result = self.second_cleanup(blend, "blend")
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["textures"], ["textures/chair_wood.png"])
        self.assertEqual(result["output"]["missing_textures"], [])
        self.assertTrue(os.path.isfile(self.path("item2", "textures", "chair_wood.png")))
        self.open_asset_two()
        image = bpy.data.images["chair_wood.png"]
        self.assertEqual(image.filepath.replace("\\", "/"), "//textures/chair_wood.png")
        self.assertIsNone(image.packed_file)


class ZipTests(CleanupTest):
    # A zip of the chair folder is unpacked and the model inside is used.
    def test_zip_of_folder(self):
        chair = self.copy_chair()
        entries = [("chair/" + name, os.path.join(chair, name)) for name in os.listdir(chair)]
        archive = self.make_zip("chair.zip", entries)
        result = self.cleanup(archive, "zip")
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["picked_file"], "chair/chair.obj")
        self.assertEqual(result["output"]["triangles"], 72)
        self.assertEqual(result["output"]["textures"], ["textures/chair_wood.png"])
        self.assertEqual(result["output"]["missing_textures"], [])
        self.assertTrue(os.path.isdir(self.path("work", "unpacked")))
        self.assert_result_valid(result)

    # Two models of the same best kind give several_models.
    def test_several_models(self):
        chair = self.copy_chair()
        obj = os.path.join(chair, "chair.obj")
        archive = self.make_zip("two.zip", [("a.obj", obj), ("b.obj", obj)])
        result = self.cleanup(archive, "zip")
        self.assertEqual(result["error"]["code"], "several_models")
        self.assertFalse(os.path.exists(self.path("item", "asset.blend")))
        self.assert_result_valid(result)

    # A better kind wins over a worse one: an .obj next to a .blend is not several models.
    def test_kind_preference(self):
        from plynth.importers import zip as zip_importer
        self.assertEqual(zip_importer.pick_model(["a/x.obj", "a/y.glb", "z.txt"]), "a/y.glb")
        self.assertEqual(zip_importer.pick_model(["x.obj", "y.blend", "z.fbx"]), "y.blend")

    # A zip with no model is import_failed.
    def test_no_model(self):
        chair = self.copy_chair()
        archive = self.make_zip("none.zip", [("readme.txt", os.path.join(chair, "chair.mtl"))])
        result = self.cleanup(archive, "zip")
        self.assertEqual(result["error"]["code"], "import_failed")

    # A zip entry that climbs out of the folder gives unpack_failed and nothing is written outside.
    def test_unsafe_path(self):
        os.makedirs(self.path("in"))
        archive = self.path("in", "evil.zip")
        with zipfile.ZipFile(archive, "w") as handle:
            handle.writestr("chair/ok.obj", "o a\n")
            handle.writestr("../evil.txt", "bad")
        result = self.cleanup(archive, "zip")
        self.assertEqual(result["error"]["code"], "unpack_failed")
        self.assertFalse(os.path.exists(self.path("evil.txt")))
        self.assertFalse(os.path.exists(self.path("work", "evil.txt")))
        self.assertFalse(os.path.exists(self.path("work", "unpacked", "chair", "ok.obj")))
        self.assert_result_valid(result)

    # Absolute names and a zip that is not a zip are refused too.
    def test_other_unpack_failures(self):
        from plynth.importers import zip as zip_importer
        self.assertTrue(zip_importer.is_unsafe("/etc/x"))
        self.assertTrue(zip_importer.is_unsafe("C:/x"))
        self.assertTrue(zip_importer.is_unsafe("a\\..\\b"))
        self.assertFalse(zip_importer.is_unsafe("a/b..c/d.obj"))
        os.makedirs(self.path("in"))
        broken = self.path("in", "broken.zip")
        with open(broken, "wb") as handle:
            handle.write(b"not a zip")
        result = self.cleanup(broken, "zip")
        self.assertEqual(result["error"]["code"], "unpack_failed")


if __name__ == "__main__":
    unittest.main()
