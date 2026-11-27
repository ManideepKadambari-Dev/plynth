import copy
import json
import os
import unittest
from unittest import mock

import bpy

from build_helpers import BuildTest, load_params
from schema_check import load_schema, validate
from slot_fixtures import upgrade


# Imports an FBX into an empty scene and returns its mesh objects.
def import_parts(path):
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


# Returns the absolute image paths the objects use, lower case with forward slashes.
def image_paths(objects):
    found = set()
    for obj in objects:
        for slot in obj.material_slots:
            if slot.material is None or slot.material.node_tree is None:
                continue
            for node in slot.material.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image is not None:
                    found.add(os.path.abspath(bpy.path.abspath(node.image.filepath)).replace("\\", "/").lower())
    return found


class PreviewPartsTests(BuildTest):
    # Builds the two floor fixture and keeps the result.
    def setUp(self):
        super().setUp()
        self.result = self.build(load_params("roof_two_parts"))
        self.assertTrue(self.result["ok"], self.result["error"])

    # Returns the path of a file in build/preview.
    def preview(self, name):
        return self.path("build", "preview", name)

    # The folder holds the two storeys, the roof of the top floor and parts.json, and no temporary file.
    def test_files(self):
        self.assertEqual(sorted(os.listdir(self.path("build", "preview"))),
                         ["f1.fbx", "f2.fbx", "f2_roof.fbx", "parts.json"])
        self.assertEqual(result_problems(self.result), [])

    # parts.json validates and lists the storey before the roof, floor by floor.
    def test_listing(self):
        with open(self.preview("parts.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(validate(data, load_schema("build-preview")), [])
        self.assertEqual([(p["floor"], p["kind"], p["file"]) for p in data["parts"]],
                         [("f1", "storey", "f1.fbx"), ("f2", "storey", "f2.fbx"), ("f2", "roof", "f2_roof.fbx")])

    # The result is valid, the step ran after the export and its time is known.
    def test_step(self):
        self.assert_result_valid(self.result)
        steps = [s["step"] for s in self.result["output"]["steps"]]
        self.assertEqual(steps[-2:], ["export_fbx", "preview_parts"])
        print("preview_parts step: %d ms" % self.result["output"]["steps"][-1]["ms"])

    # A storey part holds the walls and floors of its floor only, and no roof object.
    def test_storey_part(self):
        objects = import_parts(self.preview("f2.fbx"))
        names = sorted(o.name for o in objects)
        self.assertEqual(names, ["f2_Walls", "f2_r1_Ceiling", "f2_r1_Floor"])
        self.assertFalse([n for n in names if "Roof" in n])
        self.assertFalse([n for n in names if n.startswith("f1")])

    # The first floor's part holds no roof, and the roof part holds only the roof.
    def test_other_parts(self):
        objects = import_parts(self.preview("f1.fbx"))
        self.assertEqual(sorted(o.name for o in objects), ["f1_Walls", "f1_r1_Ceiling", "f1_r1_Floor"])
        objects = import_parts(self.preview("f2_roof.fbx"))
        self.assertEqual([o.name for o in objects], ["f2_Roof_00"])

    # The images of a part are the ones building.fbx uses.
    def test_images(self):
        whole = image_paths(import_parts(self.path("build", "building.fbx")))
        self.assertTrue(whole)
        for name in ("f1.fbx", "f2.fbx", "f2_roof.fbx"):
            with self.subTest(part=name):
                part = image_paths(import_parts(self.preview(name)))
                self.assertTrue(part)
                self.assertTrue(part <= whole, part - whole)
                for path in part:
                    self.assertTrue(os.path.isfile(path))


class PreviewSlabTests(BuildTest):
    # The slab has its own part between storey and roof, the storey has no slab object and a floor without a slab
    # lists none.
    def test_slab_part(self):
        result = self.build(load_params("two_floors_stair"), preview_folder="preview")
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result_problems(result), [])
        folder = self.path("build", "preview")
        with open(os.path.join(folder, "parts.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(validate(data, load_schema("build-preview")), [])
        listed = [(p["floor"], p["kind"], p["file"]) for p in data["parts"]]
        self.assertEqual(listed[:2], [("f1", "storey", "f1.fbx"), ("f1", "slab", "f1_slab.fbx")])
        self.assertNotIn("f2_slab.fbx", [p[2] for p in listed])
        self.assertEqual([o.name for o in import_parts(os.path.join(folder, "f1_slab.fbx"))], ["f1_Slab"])
        names = [o.name for o in import_parts(os.path.join(folder, "f1.fbx"))]
        self.assertTrue(names)
        self.assertFalse([n for n in names if "Slab" in n])


class PreviewTextureTests(BuildTest):
    # Writes a one colour png of the given size and returns its path.
    def write_png(self, name, size, color):
        path = self.path("library", name + ".png").replace("\\", "/")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        image = bpy.data.images.new(name, size, size)
        image.pixels = [c / 255.0 for c in color + (255,)] * (size * size)
        image.filepath_raw = path
        image.file_format = "PNG"
        image.save()
        bpy.data.images.remove(image)
        return path

    # Builds the gable roof with a 256 pixel roof tile material and a preview size of 64 pixels.
    def build_tiled(self):
        params = copy.deepcopy(load_params("roof_gable"))
        maps = {"base_color": self.write_png("roof_base", 256, (170, 80, 60)),
                "normal": self.write_png("roof_normal", 256, (128, 128, 255)),
                "roughness": self.write_png("roof_rough", 256, (200, 200, 200)),
                "metallic": self.write_png("roof_metal", 256, (0, 0, 0)), "opacity": None, "occlusion": None}
        params["materials"] = [{"id": "roof_tiles", "tile_size": {"width": 2.0, "height": 0.5}, "maps": maps}]
        for source in params["floors"][0]["roofs"][0]["faces"]:
            if source["part"] == "covering":
                source["material"] = "roof_tiles"
        upgrade(params)
        params["preview_texture_size"] = 64
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        return result

    # The roof part, like building.fbx, uses the 64 pixel copies in preview_textures, while building.blend keeps
    # the 256 pixel images.
    def test_roof_part_uses_copies(self):
        self.build_tiled()
        folder = os.path.abspath(self.path("build", "preview_textures")).replace("\\", "/").lower()
        whole = image_paths(import_parts(self.path("build", "building.fbx")))
        part = image_paths(import_parts(self.path("build", "preview", "f1_roof.fbx")))
        self.assertTrue(part)
        self.assertTrue(part <= whole, part - whole)
        copies = [p for p in part if p.startswith(folder)]
        self.assertTrue(copies, part)
        for path in copies:
            image = bpy.data.images.load(path)
            self.assertLessEqual(max(image.size), 64)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        sizes = [max(i.size) for i in bpy.data.images if i.filepath and "roof_" in i.filepath]
        self.assertTrue(sizes)
        self.assertEqual(set(sizes), {256})

    # No part holds a collision mesh, and the build still lists its parts.
    def test_parts_leave_out_collision(self):
        result = self.build(load_params("collision"), preview_folder="preview")
        self.assertTrue(result["ok"], result["error"])
        self.assertGreater(result["output"]["counts"]["collision"], 0)
        listed = os.listdir(self.path("build", "preview"))
        self.assertIn("parts.json", listed)
        for name in listed:
            if name.endswith(".fbx"):
                names = [o.name for o in import_parts(self.path("build", "preview", name))]
                self.assertFalse([n for n in names if "_Collision_" in n], name)


class PreviewOffTests(BuildTest):
    # With no preview folder nothing is written and the step does nothing.
    def test_null_folder(self):
        result = self.build(load_params("roof_two_parts"), preview_folder=None)
        self.assertTrue(result["ok"], result["error"])
        self.assertFalse(os.path.exists(self.path("build", "preview")))
        self.assertEqual(result_problems(result), [])

    # Without an FBX there is no preview either.
    def test_no_fbx(self):
        result = self.build(load_params("roof_two_parts"), fbx_file=None, materials_file=None, preview_folder=None)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(sorted(os.listdir(self.path("build"))), ["building.blend", "textures"])


class PreviewFailureTests(BuildTest):
    # A failing export gives preview_failed as a warning, no parts.json, and the rest of the build stays.
    def test_failed(self):
        from plynth import exporters
        original = exporters.write_file
        calls = []

        def fail_for_parts(exporter_id, objects, target, path_mode):
            if os.path.basename(os.path.dirname(target)) == "preview":
                calls.append(target)
                if len(calls) == 2:
                    raise RuntimeError("disk full")
            return original(exporter_id, objects, target, path_mode)

        with mock.patch.object(exporters, "write_file", fail_for_parts):
            result = self.build(load_params("roof_two_parts"))
        self.assertTrue(result["ok"], result["error"])
        problems = result["output"]["problems"]
        self.assertEqual([p["code"] for p in problems], ["preview_failed"])
        self.assertEqual(problems[0]["severity"], "warning")
        self.assertEqual(problems[0]["step"], "preview_parts")
        self.assertIn("disk full", problems[0]["message"])
        self.assertFalse(os.path.exists(self.path("build", "preview", "parts.json")))
        self.assertTrue(os.path.isfile(self.path("build", "building.fbx")))
        self.assertTrue(os.path.isfile(self.path("build", "building.blend")))
        self.assert_result_valid(result)

    # A floor with two roof sections writes one roof part with both.
    def test_two_sections_one_part(self):
        params = copy.deepcopy(load_params("roof_two_parts"))
        second = copy.deepcopy(params["floors"][1]["roofs"][0])
        second["index"] = 1
        params["floors"][1]["roofs"].append(second)
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["counts"]["roofs"], 2)
        objects = import_parts(self.path("build", "preview", "f2_roof.fbx"))
        self.assertEqual(sorted(o.name for o in objects), ["f2_Roof_00", "f2_Roof_01"])


# Returns the problem codes of a build result.
def result_problems(result):
    return [p["code"] for p in result["output"]["problems"]]


if __name__ == "__main__":
    unittest.main()
