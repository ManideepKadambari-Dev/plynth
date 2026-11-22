import copy
import math
import os
import shutil
import unittest

import bpy
from mathutils import Matrix, Vector

import helpers
from build_helpers import BuildTest, load_params
from plynth import scene

MODELS = os.path.join(helpers.REPO_FOLDER, "samples", "models")
BACKGROUND = [0.0549, 0.0667, 0.0863]

# id, asset, plan x, plan y, z, rotation in degrees
ITEMS = [("i1", "sofa", 1.5, 2.0, 0.0, 90.0), ("i2", "sofa", 3.0, 3.0, 0.0, 200.0), ("i3", "tv", 2.0, 0.5, 1.2, 0.0)]


class AssetBuildTests(BuildTest):
    # Cleans a sample model (then empties the session, since Blender cannot append from the open file) into an asset folder under the temporary folder and returns the blend file path.
    def make_asset(self, asset_id, source):
        folder = self.path("library", asset_id)
        name = os.path.basename(source)
        copy_folder = self.path("src", asset_id)
        shutil.copytree(os.path.dirname(source), copy_folder)
        params = {
            "source": {"file": copy_folder.replace("\\", "/") + "/" + name, "format": "obj"},
            "asset_id": asset_id,
            "output_folder": folder,
            "scale": 1.0,
            "rotate_z": 0,
            "triangle_warning": 500000,
            "thumbnail": {"render": False, "size": 64, "background": BACKGROUND},
        }
        result = self.run_task("cleanup_asset", params, job_id="clean-%s" % asset_id)
        self.assertTrue(result["ok"], result["error"])
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return folder.replace("\\", "/") + "/asset.blend"

    # Makes the job params: the square room with the three items and the given assets.
    def params(self, assets=None, **changes):
        params = copy.deepcopy(load_params("square_room"))
        params["settings"]["bevel_width"] = 0.0
        if assets is None:
            assets = [{"id": "sofa", "blend_file": self.make_asset("sofa", os.path.join(MODELS, "sofa", "sofa.obj")),
                       "collection": "sofa"},
                      {"id": "tv", "blend_file": self.make_asset("tv", os.path.join(MODELS, "tv", "tv.obj")),
                       "collection": "tv"}]
        params["assets"] = assets
        floor = params["floors"][0]
        plan_items = []
        for item_id, asset, x, y, z, rotation in ITEMS:
            floor["items"].append({"id": item_id, "asset": asset, "location": [x, -y, z],
                                   "rotation_z": -math.radians(rotation),
                                   "size": {"width": 1.0, "depth": 1.0, "height": 1.0}, "wall": None})
            plan_items.append({"id": item_id, "asset": asset, "x": x, "y": y, "z": z, "rotation": rotation})
        params["plan"] = {"version": 1, "floors": [{"id": "f1", "items": plan_items}]}
        params.update(changes)
        return params

    # Builds and returns the result.
    def run_build(self, params):
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result

    # Two sofas and a TV give one appended collection each and three instances.
    def test_collections_and_instances(self):
        result = self.run_build(self.params())
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["items"], 3)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertEqual(sorted(c.name for c in bpy.data.collections if c.name in ("sofa", "tv")), ["sofa", "tv"])
        scene_collections = [c.name for c in bpy.context.scene.collection.children_recursive]
        self.assertNotIn("sofa", scene_collections)
        empties = [o for o in bpy.data.objects if o.get("plynth_kind") == "item"]
        self.assertEqual(len(empties), 3)
        for empty in empties:
            self.assertEqual(empty.instance_type, "COLLECTION")
        self.assertEqual(len([o for o in bpy.data.objects if o.type == "MESH" and o.get("plynth_kind") == "item"]), 0)

    # Each instance matrix equals Translation(location) @ RotationZ(rotation_z).
    def test_matrices(self):
        params = self.params()
        self.run_build(params)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        bpy.context.view_layer.update()
        for item in params["floors"][0]["items"]:
            empty = bpy.data.objects["SM_%s_%s" % (item["asset"], item["id"])]
            wanted = Matrix.Translation(Vector(item["location"])) @ Matrix.Rotation(item["rotation_z"], 4, "Z")
            for row in range(4):
                for column in range(4):
                    self.assertAlmostEqual(empty.matrix_world[row][column], wanted[row][column], delta=1e-6)

    # Independent of core: the asset front (local -Y) points along the plan front turned to world, and the pivot
    # sits at (x, -y, level + z).
    def test_orientation_from_the_plan(self):
        params = self.params()
        self.run_build(params)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        bpy.context.view_layer.update()
        for item in params["plan"]["floors"][0]["items"]:
            empty = bpy.data.objects["SM_%s_%s" % (item["asset"], item["id"])]
            r = math.radians(item["rotation"])
            plan_front = (-math.sin(r), math.cos(r))
            front = empty.matrix_world.to_3x3() @ Vector((0.0, -1.0, 0.0))
            self.assertAlmostEqual(front.x, plan_front[0], delta=1e-6)
            self.assertAlmostEqual(front.y, -plan_front[1], delta=1e-6)
            self.assertAlmostEqual(front.z, 0.0, delta=1e-6)
            pivot = empty.matrix_world.translation
            self.assertAlmostEqual(pivot.x, item["x"], delta=1e-6)
            self.assertAlmostEqual(pivot.y, -item["y"], delta=1e-6)
            self.assertAlmostEqual(pivot.z, item["z"], delta=1e-6)

    # A missing asset.blend gives asset_load_failed for each item using it and the build goes on.
    def test_missing_asset(self):
        assets = [{"id": "sofa", "blend_file": self.path("nowhere", "asset.blend"), "collection": "sofa"},
                  {"id": "tv", "blend_file": self.make_asset("tv", os.path.join(MODELS, "tv", "tv.obj")),
                   "collection": "tv"}]
        result = self.run_build(self.params(assets))
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["severity"], p["object"], p["floor"], p["step"]) for p in problems],
                         [("asset_load_failed", "error", "i1", "f1", "assets"),
                          ("asset_load_failed", "error", "i2", "f1", "assets")])
        self.assertEqual(result["output"]["counts"]["items"], 1)
        self.assertTrue(os.path.isfile(self.path("build", "building.fbx")))

    # In the exported FBX the two sofas are separate objects that share one mesh, at the item locations.
    def test_fbx_instances(self):
        params = self.params()
        result = self.run_build(params)
        self.assertGreater(result["output"]["objects"], 6)
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("build", "building.fbx"))
        bpy.context.view_layer.update()
        sofas = {}
        for item_id in ("i1", "i2"):
            parts = [o for o in bpy.data.objects if o.type == "MESH" and o.parent is not None
                     and o.parent.name.startswith("SM_sofa_%s" % item_id)]
            self.assertGreater(len(parts), 0, item_id)
            sofas[item_id] = parts
        first = sorted(sofas["i1"], key=lambda o: o.name)
        second = sorted(sofas["i2"], key=lambda o: o.name)
        self.assertEqual(len(first), len(second))
        for a, b in zip(first, second):
            self.assertIsNot(a, b)
            self.assertIs(a.data, b.data)
        for item_id, wanted in (("i1", (1.5, -2.0)), ("i2", (3.0, -3.0))):
            low, high = scene.world_bounds(sofas[item_id])
            self.assertAlmostEqual(low.z, 0.0, delta=1e-3)
            centre = (low + high) / 2
            reach = max((high - low).x, (high - low).y)
            self.assertLessEqual(abs(centre.x - wanted[0]), reach / 2 + 1e-3)
            self.assertLessEqual(abs(centre.y - wanted[1]), reach / 2 + 1e-3)
            pivot = sofas[item_id][0].parent.matrix_world.translation
            self.assertAlmostEqual(pivot.x, wanted[0], delta=1e-3)
            self.assertAlmostEqual(pivot.y, wanted[1], delta=1e-3)

    # building.blend reopened still has instance empties, not real copies.
    def test_blend_keeps_instances(self):
        self.run_build(self.params())
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        items = [o for o in bpy.data.objects if o.get("plynth_kind") == "item"]
        self.assertEqual(len(items), 3)
        self.assertTrue(all(o.type == "EMPTY" and o.instance_type == "COLLECTION" for o in items))
        self.assertFalse([o for o in bpy.data.objects if o.parent is not None and o.parent.get("plynth_kind") == "item"])


if __name__ == "__main__":
    unittest.main()
