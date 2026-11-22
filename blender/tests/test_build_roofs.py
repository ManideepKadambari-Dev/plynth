import copy
import json
import math
import os
import unittest

import bpy
from mathutils import Vector

import roof_helpers as roofs
from build_helpers import BuildTest, load_params
from material_helpers import SAMPLES, MaterialTest
from plynth import meshes, shading
from plynth.materials import read_pixels, read_size

FIXTURES = ("roof_flat", "roof_gable", "roof_two_parts")
BASE = roofs.base_of(0.0)


# Returns the (u, v) of every corner of a polygon.
def corner_uvs(obj, polygon):
    layer = obj.data.uv_layers["UVMap"]
    return [tuple(layer.uv[i].vector) for i in polygon.loop_indices]


# Returns the polygons of an object that use the named material.
def polygons_of(obj, name):
    names = [m.name for m in obj.data.materials]
    return [p for p in obj.data.polygons if names[p.material_index] == name]


class RoofBuildTest(BuildTest):
    # Builds a fixture and opens building.blend.
    def build_open(self, name, params=None, **overrides):
        result = self.build(params or load_params(name), **overrides)
        self.assertTrue(result["ok"], result["error"])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return result


class RoofFixtureTests(RoofBuildTest):
    # Every roof fixture is a valid job.
    def test_fixtures_valid(self):
        for name in FIXTURES:
            with self.subTest(fixture=name):
                self.assert_job_valid(name)

    # Every face of the hand worked roofs is planar within 1e-6 and runs anticlockwise from its side.
    def test_faces_planar(self):
        for name in FIXTURES:
            params = load_params(name)
            for floor in params["floors"]:
                for roof in floor["roofs"]:
                    for source in roof["faces"]:
                        self.assertLess(roofs.planarity(source), 1e-6, name)
                        self.assertGreater(roofs.face_area(source), 0.0)

    # Every fixture builds, the roofs step runs between the floors and the materials, and the roofs are counted.
    def test_builds(self):
        wanted = {"roof_flat": 1, "roof_gable": 1, "roof_two_parts": 1}
        for name in FIXTURES:
            with self.subTest(fixture=name):
                self.setUp()
                result = self.build(load_params(name))
                self.assertTrue(result["ok"], result["error"])
                steps = [s["step"] for s in result["output"]["steps"]]
                self.assertLess(steps.index("floors"), steps.index("roofs"))
                self.assertLess(steps.index("roofs"), steps.index("materials"))
                self.assertEqual(result["output"]["counts"]["roofs"], wanted[name])
                self.assertEqual(result["output"]["problems"], [])
                self.assert_result_valid(result)

    # The roof object has the names and properties the contract gives, in the collection of its floor.
    def test_object(self):
        self.build_open("roof_two_parts")
        obj = bpy.data.objects["SM_f2_Roof_00"]
        self.assertEqual(obj["plynth_kind"], "roof")
        self.assertEqual(obj["plynth_id"], "f2")
        self.assertEqual(obj["plynth_roof_index"], 0)
        self.assertEqual(obj["plynth_roof_section"], "")
        self.assertEqual([c.name for c in obj.users_collection], ["f2"])
        self.assertNotIn("SM_f1_Roof_00", bpy.data.objects)

    # A roof section that is anchored to an entry keeps its id.
    def test_section_id(self):
        self.build_open("roof_gable")
        self.assertEqual(bpy.data.objects["SM_f1_Roof_00"]["plynth_roof_section"], "rf1")

    # Material slots use the neutral surfaces of each part.
    def test_neutral_slots(self):
        self.build_open("roof_gable")
        names = sorted(m.name for m in bpy.data.objects["SM_f1_Roof_00"].data.materials)
        self.assertEqual(names, ["Fascia", "Roof", "Soffit", "Wall_Outside"])
        self.build_open("roof_flat")
        names = sorted(m.name for m in bpy.data.objects["SM_f1_Roof_00"].data.materials)
        self.assertEqual(names, ["Fascia", "Flat_Roof", "Soffit", "Wall_Outside"])

    # The mesh area equals the area of the job faces.
    def test_area(self):
        for name in ("roof_flat", "roof_gable"):
            with self.subTest(fixture=name):
                self.setUp()
                self.build_open(name)
                wanted = sum(roofs.face_area(f) for f in load_params(name)["floors"][0]["roofs"][0]["faces"])
                self.assertAlmostEqual(meshes.area(bpy.data.objects["SM_f1_Roof_00"]), wanted, delta=1e-4)


class FlatRoofTests(RoofBuildTest):
    # The parapet stands 0.6 above the layer, and the layer is 0.15 above the slab.
    def test_parapet_height(self):
        self.build_open("roof_flat")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        covering = polygons_of(obj, "Flat_Roof")
        self.assertGreater(len(covering), 0)
        layer = covering[0].center.z
        for polygon in covering:
            self.assertAlmostEqual(polygon.center.z, layer, delta=1e-5)
        self.assertAlmostEqual(layer, BASE + roofs.THICKNESS, delta=1e-5)
        top = max(v.co.z for v in obj.data.vertices)
        self.assertAlmostEqual(top - layer, 0.6, delta=1e-5)
        low = min(v.co.z for v in obj.data.vertices)
        self.assertAlmostEqual(low, BASE, delta=1e-5)

    # The parapet top is a face with a hole, tessellated: its area is the ring and no triangle covers the hole.
    def test_parapet_ring(self):
        self.build_open("roof_flat")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        top = max(v.co.z for v in obj.data.vertices)
        ring = [p for p in obj.data.polygons if abs(p.center.z - top) < 1e-6]
        self.assertTrue(all(len(p.vertices) == 3 for p in ring))
        area = sum(p.area for p in ring)
        self.assertAlmostEqual(area, 6.0 * 4.0 - 5.6 * 3.6, delta=1e-4)
        for polygon in ring:
            self.assertFalse(0.2 < polygon.center.x < 5.8 and -3.8 < polygon.center.y < -0.2)

    # The covering spans the whole section, so edges that do not join two faces are only at the covering's level (points in line leave triangles with no area).
    def test_closed(self):
        self.build_open("roof_flat")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        mesh = obj.data
        counts = {}
        for polygon in mesh.polygons:
            for key in polygon.edge_keys:
                counts[key] = counts.get(key, 0) + 1
        layer = BASE + roofs.THICKNESS
        for key, number in counts.items():
            if number == 2:
                continue
            for index in key:
                co = mesh.vertices[index].co
                self.assertAlmostEqual(co.z, layer, delta=1e-5)

    # The covering faces up and the underside faces down.
    def test_normals(self):
        self.build_open("roof_flat")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        for polygon in polygons_of(obj, "Flat_Roof"):
            if polygon.area > 1e-9:
                self.assertGreater(polygon.normal.z, 0.999)
        for polygon in polygons_of(obj, "Soffit"):
            if polygon.area > 1e-9:
                self.assertLess(polygon.normal.z, -0.999)


class GableRoofTests(RoofBuildTest):
    # The covering is two planes of 30 degrees that meet at the ridge.
    def test_slopes(self):
        self.build_open("roof_gable")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        covering = polygons_of(obj, "Roof")
        self.assertEqual(len(covering), 4)
        for polygon in covering:
            angle = math.degrees(math.acos(polygon.normal.z))
            self.assertAlmostEqual(angle, 30.0, delta=1e-3)
        ridge = max(v.co.z for v in obj.data.vertices)
        self.assertAlmostEqual(ridge, BASE + roofs.SLOPE * roofs.HALF_SPAN, delta=1e-5)

    # On the covering the UV v runs up the slope in metres and the UVs keep real distances.
    def test_covering_uvs(self):
        self.build_open("roof_gable")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        vertices = obj.data.vertices
        for polygon in polygons_of(obj, "Roof"):
            corners = [vertices[i].co for i in polygon.vertices]
            uvs = corner_uvs(obj, polygon)
            for a in range(3):
                for b in range(a + 1, 3):
                    dz = corners[b].z - corners[a].z
                    dv = uvs[b][1] - uvs[a][1]
                    self.assertAlmostEqual(dv, dz / math.sin(roofs.PITCH), delta=1e-4)
                    distance = (corners[b] - corners[a]).length
                    self.assertAlmostEqual(math.hypot(uvs[b][0] - uvs[a][0], dv), distance, delta=1e-4)

    # The underside faces down and the gable fills stand vertical between the slab and the underside.
    def test_underside_and_gables(self):
        self.build_open("roof_gable")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        for polygon in polygons_of(obj, "Soffit"):
            self.assertLess(polygon.normal.z, 0.0)
        fills = [p for p in polygons_of(obj, "Wall_Outside")]
        self.assertEqual(len(fills), 2)
        for polygon in fills:
            self.assertAlmostEqual(polygon.normal.z, 0.0, delta=1e-6)
            self.assertAlmostEqual(abs(polygon.normal.x), 1.0, delta=1e-6)
            low = min(obj.data.vertices[i].co.z for i in polygon.vertices)
            self.assertAlmostEqual(low, BASE, delta=1e-5)

    # The roof reaches past the section by the overhang on every side.
    def test_extent(self):
        self.build_open("roof_gable")
        obj = bpy.data.objects["SM_f1_Roof_00"]
        xs = [v.co.x for v in obj.data.vertices]
        ys = [v.co.y for v in obj.data.vertices]
        self.assertAlmostEqual(min(xs), -0.3, delta=1e-5)
        self.assertAlmostEqual(max(xs), 6.3, delta=1e-5)
        self.assertAlmostEqual(min(ys), -4.3, delta=1e-5)
        self.assertAlmostEqual(max(ys), 0.3, delta=1e-5)

    # A library material on the covering gets its slot, and its UVs are divided by the tile size.
    def test_covering_material(self):
        folder = self.path("library", "roof_tiles")
        os.makedirs(folder, exist_ok=True)
        maps = {}
        for channel in ("base_color", "normal", "roughness", "metallic"):
            path = folder + "/" + channel + ".png"
            shading.write_constant_png(path, (128, 128, 255) if channel == "normal" else (120, 80, 70))
            maps[channel] = path
        maps["opacity"] = None
        maps["occlusion"] = None
        params = copy.deepcopy(load_params("roof_gable"))
        params["materials"] = [{"id": "roof_tiles", "tile_size": {"width": 2.0, "height": 0.5}, "maps": maps}]
        for source in params["floors"][0]["roofs"][0]["faces"]:
            if source["part"] == "covering":
                source["material"] = "roof_tiles"
        self.build_open("roof_gable", params)
        obj = bpy.data.objects["SM_f1_Roof_00"]
        covering = polygons_of(obj, "roof_tiles")
        self.assertEqual(len(covering), 4)
        vertices = obj.data.vertices
        for polygon in covering:
            corners = [vertices[i].co for i in polygon.vertices]
            uvs = corner_uvs(obj, polygon)
            dz = corners[1].z - corners[0].z
            dv = uvs[1][1] - uvs[0][1]
            self.assertAlmostEqual(dv, dz / math.sin(roofs.PITCH) / 0.5, delta=1e-4)
        self.assertNotIn("Roof", [m.name for m in obj.data.materials])


class RoofFailureTests(RoofBuildTest):
    # A section with a broken face is reported and left out, and the other sections are still built.
    def test_roof_failed(self):
        params = copy.deepcopy(load_params("roof_gable"))
        good = params["floors"][0]["roofs"][0]
        bad = copy.deepcopy(good)
        bad["index"] = 1
        bad["faces"][0]["outline"] = [[0, 0, 0], [1, 0, 0], [2, 0, 0]]
        params["floors"][0]["roofs"].insert(0, bad)
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        problems = result["output"]["problems"]
        self.assertEqual([p["code"] for p in problems], ["roof_failed"])
        self.assertEqual(problems[0]["severity"], "warning")
        self.assertEqual(problems[0]["floor"], "f1")
        self.assertEqual(problems[0]["step"], "roofs")
        self.assertEqual(result["output"]["counts"]["roofs"], 1)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertIn("SM_f1_Roof_00", bpy.data.objects)
        self.assertNotIn("SM_f1_Roof_01", bpy.data.objects)

    # A job without roofs builds as before and counts none.
    def test_no_roofs(self):
        params = copy.deepcopy(load_params("roof_gable"))
        params["floors"][0]["roofs"] = []
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["counts"]["roofs"], 0)


class RoofSampleMaterialTests(MaterialTest):
    # The roof and concrete samples clean without a warning, keep their tile sizes and stay under the size limit.
    def test_samples_clean(self):
        for material_id, tile in (("roof_tiles", (1.0, 1.0)), ("concrete", (2.0, 2.0))):
            with self.subTest(material_id):
                result = self.clean_ok(os.path.join(SAMPLES, material_id), output_folder=self.path("out", material_id))
                self.assertEqual(result["warnings"], [])
                output = result["output"]
                self.assertEqual(output["source_info"]["id"], material_id)
                self.assertEqual(output["source_info"]["license"], "own_work")
                self.assertEqual((output["tile_size"]["width"], output["tile_size"]["height"]), tile)
                for channel in ("base_color", "roughness"):
                    self.assertTrue(os.path.isfile(self.map_path(result, channel)), channel)
        self.assertIn("normal", self.clean_ok(os.path.join(SAMPLES, "roof_tiles"),
                                              output_folder=self.path("again"))["output"]["maps"])

    # All sample materials together stay under 6 MB.
    def test_size_limit(self):
        total = sum(os.path.getsize(os.path.join(root, name)) for root, _, files in os.walk(SAMPLES) for name in files)
        self.assertLess(total, 6 * 1024 * 1024)


PLANS = os.path.join(SAMPLES, "..", "plans")


# Reads an expected.json of a sample plan.
def read_expected(name):
    with open(os.path.join(PLANS, name + ".expected.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


class HousePlanTests(unittest.TestCase):
    # Both plans have the same size, 100 px per metre, and outer corners that line up so the offset is 0, 0.
    def test_alignment(self):
        ground = read_expected("house-01-ground")
        upper = read_expected("house-01-upper")
        self.assertEqual(read_size(os.path.join(PLANS, "house-01-ground.png")),
                         read_size(os.path.join(PLANS, "house-01-upper.png")))
        self.assertEqual((ground["pixels_per_metre"], upper["pixels_per_metre"]), (100, 100))
        g = ground["points"][:4]
        u = upper["points"][:4]
        self.assertEqual(g[0][0], u[0][0])
        self.assertEqual(g[3], u[3])
        self.assertEqual(g[2][1], u[2][1])
        self.assertEqual(u[0][1], g[0][1] - 100)
        self.assertEqual((g[1][0] - g[0][0], g[2][1] - g[0][1]), (970, 770))
        self.assertAlmostEqual((u[1][0] - u[0][0]) / (g[1][0] - g[0][0]), 2.0 / 3.0, delta=0.01)

    # The expected walls are dark at their middle, the rooms light, and every opening is a gap in its wall.
    def test_expected_matches_drawing(self):
        for name in ("house-01-ground", "house-01-upper"):
            with self.subTest(name):
                expected = read_expected(name)
                grey = read_pixels(os.path.join(PLANS, name + ".png"))[:, :, 0]
                for wall in expected["walls"]:
                    (x0, y0), (x1, y1) = wall["from"], wall["to"]
                    mid = ((x0 + x1) // 2, (y0 + y1) // 2)
                    gap = any(True for o in expected["openings"]
                              if abs(o["x"] - mid[0]) <= o["width"] / 2 and abs(o["y"] - mid[1]) <= o["width"] / 2)
                    if not gap:
                        self.assertLess(grey[mid[1], mid[0]], 0.1, wall)
                for room in expected["rooms"]:
                    x, y = room["inside"]
                    self.assertGreater(grey[y, x], 0.9, room)
                for box in (n["box"] for n in expected["not_walls"] if n["kind"] == "door_arc"):
                    band = grey[box[1]:box[3], box[0]:box[2]]
                    self.assertTrue((band < 0.5).any())


if __name__ == "__main__":
    unittest.main()
