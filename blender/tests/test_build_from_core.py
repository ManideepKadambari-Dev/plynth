import copy
import datetime
import json
import os
import unittest

import bpy

import material_helpers
from build_helpers import BuildTest, load_params
from plynth import meshes
from schema_check import load_schema, validate

LIBRARY_MATERIALS = ("grey_tile", "red_brick", "white_paint")


# Returns the area of a ring of points, positive when anticlockwise.
def ring_area(ring):
    return meshes.polygon_area([tuple(p) for p in ring["points"]])


class FromCoreTests(BuildTest):
    # Builds the job core made (two rooms, a curved wall, a rounded corner, all five shapes) with the bevel off.
    def setUp(self):
        super().setUp()
        self.params = copy.deepcopy(load_params("from_core"))
        self.params["settings"]["bevel_width"] = 0.0
        self.result = self.build(self.params)

    # The build succeeds with no problems and the counts match the job.
    def test_counts(self):
        self.assertTrue(self.result["ok"], self.result["error"])
        self.assert_result_valid(self.result)
        output = self.result["output"]
        self.assertEqual(output["problems"], [])
        floor = self.params["floors"][0]
        counts = output["counts"]
        self.assertEqual(counts["openings_cut"], len(floor["openings"]))
        self.assertEqual(counts["rooms"], len(floor["rooms"]))
        self.assertEqual(counts["wall_solids"], len(floor["wall_solids"]))
        fills = len([o for o in floor["openings"] if o["type"] in ("door", "window")])
        self.assertEqual(counts["fills"], fills)
        self.assertEqual(sorted(os.listdir(self.path("build"))),
                         ["building.blend", "building.fbx", "materials.json", "textures"])

    # Every room floor has the area core gave it, and the walls lost the volume of the openings.
    def test_geometry(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        floor = self.params["floors"][0]
        for room in floor["rooms"]:
            obj = bpy.data.objects["SM_%s_%s_Floor" % (floor["id"], room["id"])]
            self.assertAlmostEqual(meshes.area(obj), room["area"], delta=1e-3)
            ceiling = bpy.data.objects["SM_%s_%s_Ceiling" % (floor["id"], room["id"])]
            self.assertAlmostEqual(meshes.area(ceiling), room["area"], delta=1e-3)
        solid = floor["wall_solids"][0]
        full = (ring_area(solid["outline"]) + sum(ring_area(h) for h in solid["holes"])) * solid["height"]
        lost_expected = sum(abs(meshes.polygon_area([tuple(p) for p in o["outline"]])) * o["thickness"]
                            for o in floor["openings"])
        walls = bpy.data.objects["SM_%s_Walls" % floor["id"]]
        self.assertTrue(meshes.is_manifold(walls))
        lost = full - meshes.volume(walls)
        self.assertAlmostEqual(lost / lost_expected, 1.0, delta=0.03)
        for opening in floor["openings"]:
            if opening["threshold"]:
                self.assertIn("SM_%s_Thresholds" % floor["id"], bpy.data.objects)
            kind = {"door": "Door", "window": "Window"}.get(opening["type"])
            name = "SM_%s_%s_%s" % (floor["id"], opening["id"], kind)
            self.assertEqual(name in bpy.data.objects, kind is not None, name)

    # With the default bevel the build still has no problems and the walls stay closed.
    def test_with_bevel(self):
        params = copy.deepcopy(load_params("from_core"))
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["problems"], [])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertTrue(meshes.is_manifold(bpy.data.objects["SM_f1_Walls"]))


class FromCoreMaterialsTests(material_helpers.MaterialTest, BuildTest):
    # Cleans three sample materials into a scratch library the way the catalog stores them and returns its folder.
    def make_library(self):
        library = self.path("library")
        now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        for material_id in LIBRARY_MATERIALS:
            folder = self.path("library", "materials", "items", material_id)
            params = self.clean_params(os.path.join(material_helpers.SAMPLES, material_id), output_folder=folder)
            result = self.run_task("cleanup_material", params, job_id="clean-" + material_id)
            self.assertTrue(result["ok"], result["error"])
            output = result["output"]
            info = {
                "format": "plynth-material", "version": 1, "id": material_id, "name": material_id,
                "groups": [], "tags": [], "license": "own_work",
                "source": {"kind": "texture_set", "reference": material_id, "imported_at": now},
                "tile_size": output["tile_size"], "maps": output["maps"], "texture_size": output["texture_size"],
                "files": {"bytes": 0, "modified": now},
                "thumbnail": {"file": None, "error": None, "engine": None, "rendered_at": None},
                "cleanup": {"blender_version": bpy.app.version_string, "warnings": result["warnings"]},
            }
            self.assertEqual(validate(info, load_schema("material-info")), [], material_id)
            with open(os.path.join(folder, "info.json"), "w", encoding="utf-8") as handle:
                json.dump(info, handle)
        return library

    # Returns the materials job with its project folder and library folder filled in.
    def make_params(self):
        library = self.make_library()
        path = os.path.join(helpers_data(), "from_core_materials.json")
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read().replace("@LIBRARY@", library).replace("@PROJECT@", self.path("project"))
        return json.loads(text)["params"]

    # Builds the job, which uses white paint inside, red brick outside and grey tile floors, with the bevel off.
    def setUp(self):
        super().setUp()
        self.params = self.make_params()
        self.params["settings"]["bevel_width"] = 0.0
        self.result = self.build(self.params)

    # The build has no problems and every map file of the job exists.
    def test_build(self):
        self.assertTrue(self.result["ok"], self.result["error"])
        self.assert_result_valid(self.result)
        output = self.result["output"]
        self.assertEqual([p for p in output["problems"] if p["code"] != "missing_material"], [])
        for entry in self.params["materials"]:
            for path in entry["maps"].values():
                self.assertTrue(path is None or os.path.isfile(path), path)
        self.assertEqual(output["counts"]["materials"], len(self.read_materials()["materials"]))

    # Every room's floor has the area core gave it.
    def test_floor_areas(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        floor = self.params["floors"][0]
        for room in floor["rooms"]:
            obj = bpy.data.objects["SM_%s_%s_Floor" % (floor["id"], room["id"])]
            self.assertAlmostEqual(meshes.area(obj), room["area"], delta=1e-3)
            self.assertEqual([m.name for m in obj.data.materials], ["%s_Floor" % ("Living" if room["id"] == "r1" else "Bedroom")])

    # Every inside wall face uses its room's wall slot and every outside face Exterior_Wall (wall ends, tops and reveals are left out).
    def test_wall_faces(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        walls = bpy.data.objects["SM_f1_Walls"]
        names = [m.name for m in walls.data.materials]
        sides = [item.value for item in walls.data.attributes["plynth_side"].data]
        rooms = [item.value for item in walls.data.attributes["plynth_room"].data]
        inside = outside = 0
        for polygon in walls.data.polygons:
            if sides[polygon.index] >= 2:
                continue
            name = names[polygon.material_index]
            if rooms[polygon.index] != -1:
                self.assertIn(name, ("Living_Wall", "Bedroom_Wall"))
                inside += 1
            else:
                self.assertEqual(name, "Exterior_Wall")
                outside += 1
        self.assertGreater(inside, 0)
        self.assertGreater(outside, 0)

    # materials.json validates and names the three library materials.
    def test_materials_file(self):
        data = self.read_materials()
        self.assertEqual(validate(data, load_schema("build-materials")), [])
        shown = {m["material"] for m in data["materials"]}
        self.assertTrue(set(LIBRARY_MATERIALS) <= shown, shown)

    # Reads the materials.json the build wrote.
    def read_materials(self):
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            return json.load(handle)


# Returns the folder of the job fixtures.
def helpers_data():
    from build_helpers import DATA_FOLDER
    return DATA_FOLDER


if __name__ == "__main__":
    unittest.main()
