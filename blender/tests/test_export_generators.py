import copy
import json
import os
import unittest

import bpy

from export_helpers import job_params
from generator_helpers import SAMPLES, GeneratorTest
from plynth import scene
from schema_check import load_schema, validate
from slot_fixtures import upgrade
from test_build_collision import collision_params
from test_generators_build import isolate

COLUMN = "SM_column_i5"
RAILING = "SM_railing_i6"


# Returns the world bounds of the vertices themselves, so a turned object and its baked copy agree.
def vertex_bounds(obj):
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    return ([min(point[axis] for point in points) for axis in range(3)],
            [max(point[axis] for point in points) for axis in range(3)])


class GeneratorExportTests(GeneratorTest):
    # Starts with user folders of its own.
    def setUp(self):
        super().setUp()
        isolate(self)

    # Builds the square room with a column and a turned railing. A name in neutral gets no material. preview asks
    # the build for its preview parts, which it writes only when the job names a preview folder.
    def build_room(self, neutral=(), furniture=False, preview=False):
        paint = self.make_material("paint_a")
        params = upgrade(copy.deepcopy(collision_params()))
        params["materials"] = [paint]
        for slot in params["slots"]:
            slot["material"] = "paint_a"
        items = []
        files = []
        for item_id, generator, slot, values, location, rotation, size in (
                ("i5", "column", "Column_Material",
                 {"diameter": 0.3, "height": 3.0, "shape": "round", "sides": 24.0, "plinth": True},
                 [2.0, -2.0, 0.0], 0.0, (0.3, 0.3, 3.0)),
                ("i6", "railing", "Railing_Material", {"length": 1.5, "height": 0.9, "spacing": 0.9},
                 [1.5, -1.0, 0.0], -0.5, (1.5, 0.05, 0.9))):
            material = None if slot in neutral else "paint_a"
            values["material"] = material
            params["slots"].append({"name": slot, "material": material, "surface": "Generated"})
            items.append({"id": item_id, "generator": generator, "params": values, "location": location,
                          "rotation_z": rotation, "size": dict(zip(("width", "depth", "height"), size)),
                          "room": None, "wall": None, "slots": {"material": slot}})
            files.append({"id": generator, "file": os.path.join(SAMPLES, generator + ".py").replace("\\", "/"),
                          "addons": []})
        params["slots"].sort(key=lambda row: row["name"])
        params["generators"] = files
        params["floors"][0]["generator_items"] = items
        if furniture:
            params["assets"] = [self.make_stool()]
            params["floors"][0]["items"] = [{"id": "i7", "asset": "stool", "location": [3.0, -3.0, 0.0],
                                             "rotation_z": 0.0, "wall": None,
                                             "size": {"width": 0.4, "depth": 0.4, "height": 0.4}}]
        params["output_folder"] = self.path("project", "build")
        params["blend_file"] = "building.blend"
        if preview:
            params["preview_folder"] = "preview"
        result = self.run_task("build", params, job_id="build-1")
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["problems"], [])
        self.blend = self.path("project", "build", "building.blend")
        with open(self.path("project", "build", "materials.json"), "r", encoding="utf-8") as handle:
            self.built_materials = json.load(handle)["materials"]
        bpy.ops.wm.open_mainfile(filepath=self.blend)
        bpy.context.view_layer.update()
        self.bounds = {name: vertex_bounds(bpy.data.objects[name]) for name in (COLUMN, RAILING)}

    # Writes a stool whose mapped material has the name of the slot of the column.
    def make_stool(self):
        maps = self.make_material("stool_maps")["maps"]
        scene.clear_scene()
        collection = bpy.data.collections.new("stool")
        bpy.context.scene.collection.children.link(collection)
        bpy.ops.mesh.primitive_cube_add(size=0.4, location=(0.0, 0.0, 0.2))
        cube = bpy.context.view_layer.objects.active
        for old in list(cube.users_collection):
            old.objects.unlink(cube)
        collection.objects.link(cube)
        cube.data.materials.append(bpy.data.materials.new("Column_Material"))
        path = self.path("library", "stool", "asset.blend")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=path)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return {"id": "stool", "blend_file": path, "collection": "stool",
                "materials": [{"name": "Column_Material", "maps": maps}]}

    # Runs the export of the built room and returns its checks by id.
    def export_room(self, groups=None, blend=None):
        params = job_params()
        params["project"]["folder"] = self.path("project")
        params["blend_file"] = blend or self.blend
        params["output_folder"] = self.path("project", "export", ".staging")
        params["materials"] = self.built_materials
        if groups is not None:
            params["groups"] = groups
        job = {"format": "plynth-blender-job", "version": 1, "task": "export", "job_id": "export-1",
               "result_file": "result.json", "params": params}
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        result = self.run_task("export", params, job_id="export-1")
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.output = result["output"]
        self.staging = params["output_folder"]
        return {check["id"]: check for check in self.output["checks"]}

    # Returns the ids of the checks that did not pass. The kit file check may be skipped, as these jobs ask for
    # no kit, but it must then say skipped and nothing else.
    def not_passed(self, checks):
        return [check_id for check_id, check in checks.items()
                if check["status"] != "pass" and not (check_id == "kit_file" and check["status"] == "skipped")]

    # Fails unless the object has the bounds it had in the build.
    def assert_bounds(self, obj):
        low, high = vertex_bounds(obj)
        for axis in range(3):
            self.assertAlmostEqual(low[axis], self.bounds[obj.name][0][axis], delta=1e-3)
            self.assertAlmostEqual(high[axis], self.bounds[obj.name][1][axis], delta=1e-3)

    # Column and railing export with identity transforms, their slot names and every check passing.
    def test_export(self):
        self.build_room()
        checks = self.export_room()
        self.assertEqual(self.not_passed(checks), [], checks)
        self.assertTrue(self.output["written"])
        bpy.context.view_layer.update()
        for name, slot in ((COLUMN, "Column_Material"), (RAILING, "Railing_Material")):
            obj = bpy.data.objects[name]
            for hull in (obj, bpy.data.objects["UCX_%s_00" % name]):
                self.assertIsNone(hull.parent)
                for row in range(4):
                    for column in range(4):
                        self.assertAlmostEqual(hull.matrix_world[row][column], 1.0 if row == column else 0.0, places=6)
            self.assertEqual([m.name for m in obj.data.materials], [slot])
            self.assert_bounds(obj)
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=os.path.join(self.staging, "Square.fbx"))
        bpy.context.view_layer.update()
        for name in (COLUMN, RAILING):
            self.assertIn("UCX_%s_00" % name, bpy.data.objects)
            self.assert_bounds(bpy.data.objects[name])

    # An empty material fails materials and names the slot.
    def test_no_material(self):
        self.build_room(neutral=("Railing_Material",))
        checks = self.export_room()
        self.assertEqual(checks["materials"]["status"], "fail")
        self.assertEqual([f["object"] for f in checks["materials"]["findings"]], ["Railing_Material"])
        self.assertIn("Railing_Material", checks["materials"]["findings"][0]["message"])
        self.assertEqual(checks["collision"]["status"], "pass")
        self.assertFalse(self.output["written"])

    # A generator without its hull fails collision with the item.
    def test_no_hull(self):
        self.build_room()
        bpy.data.objects.remove(bpy.data.objects["UCX_%s_00" % COLUMN], do_unlink=True)
        changed = self.path("project", "build", "changed.blend")
        bpy.ops.wm.save_as_mainfile(filepath=changed)
        checks = self.export_room(blend=changed)
        self.assertEqual(checks["collision"]["status"], "fail")
        self.assertEqual([(f["object"], f["plan_id"]) for f in checks["collision"]["findings"]], [(COLUMN, "i5")])
        self.assertFalse(self.output["written"])

    # A group of generators takes their hulls with them.
    def test_group_of_generators(self):
        self.build_room()
        groups = [{"id": "building", "file": "Square.fbx", "kinds": [], "ids": [], "rest": True},
                  {"id": "generators", "file": "Generators.fbx", "kinds": ["generator"], "ids": []}]
        self.export_room(groups=groups)
        self.assertTrue(self.output["written"])
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=os.path.join(self.staging, "Generators.fbx"))
        self.assertEqual(sorted(o.name for o in bpy.data.objects if o.type == "MESH"),
                         sorted([COLUMN, RAILING, "UCX_%s_00" % COLUMN, "UCX_%s_00" % RAILING]))

    # The preview shows the generators and not their hulls.
    def test_preview(self):
        self.build_room(preview=True)
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("project", "build", "preview", "f1.fbx"))
        names = [o.name for o in bpy.data.objects]
        self.assertIn(COLUMN, names)
        self.assertIn(RAILING, names)
        self.assertEqual([name for name in names if name.startswith("UCX_")], [])

    # A furniture material with the name of a slot exports under its asset, and the slot under its own name.
    def test_furniture_export_name(self):
        self.build_room(furniture=True)
        checks = self.export_room()
        self.assertEqual(checks["materials"]["status"], "pass", checks["materials"])
        self.assertEqual([m.name for m in bpy.data.objects["SM_stool_i7"].data.materials], ["Stool_Column_Material"])
        self.assertEqual([m.name for m in bpy.data.objects[COLUMN].data.materials], ["Column_Material"])


if __name__ == "__main__":
    unittest.main()
