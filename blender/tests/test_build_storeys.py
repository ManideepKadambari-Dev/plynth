import copy
import json
import os
import unittest

import bmesh
import bpy

from build_helpers import BuildTest, load_params

SLAB_FIXTURES = ("two_floors_stair", "overhang")
RISER = 3.2 / 17


# Returns a copy of fixture params with the bevel off.
def sharp(name):
    params = copy.deepcopy(load_params(name))
    params["settings"]["bevel_width"] = 0.0
    return params


OPEN = []


# Returns the bmesh of an object in the scene. Every one is freed at the end of the test, before another file
# is opened.
def mesh_of(name):
    bm = bmesh.new()
    bm.from_mesh(bpy.data.objects[name].data)
    OPEN.append(bm)
    return bm


# Frees every bmesh the test made.
def free_meshes():
    while OPEN:
        OPEN.pop().free()


# Returns the faces of an object that have area and whose UV area differs from it, as (face index, area, UV area).
# The neutral tile is one metre square, so a face with true metric UVs has the same area in both.
def uv_mismatches(name):
    bm = mesh_of(name)
    uv = bm.loops.layers.uv.get("UVMap")
    found = []
    for face in bm.faces:
        area = face.calc_area()
        if area <= 1e-8:
            continue
        points = [loop[uv].uv for loop in face.loops]
        flat = 0.5 * abs(sum(a.x * b.y - b.x * a.y for a, b in zip(points, points[1:] + points[:1])))
        if abs(flat - area) > 1e-6 + 1e-4 * area:
            found.append((face.index, area, flat))
    return found


class StoreyTest(BuildTest):
    # Opens the built blend file after freeing the bmeshes of the earlier checks.
    def open_blend(self):
        free_meshes()
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))

    # Frees the bmeshes before the scene goes away.
    def setUp(self):
        super().setUp()
        self.addCleanup(free_meshes)


# Returns the slot names of an object, in order.
def slot_names(obj):
    return [m.name for m in obj.data.materials]


# Returns the total face area of an object.
def area_of(name):
    return sum(face.calc_area() for face in mesh_of(name).faces)


class StairFixtureTests(StoreyTest):
    # Builds the two floor stair fixture once.
    def setUp(self):
        super().setUp()
        self.params = sharp("two_floors_stair")
        self.result = self.build(self.params)
        self.assertTrue(self.result["ok"], self.result["error"])
        self.open_blend()

    # Fixtures and the result match the schemas, the counts are reported and the steps run in order.
    def test_valid(self):
        for name in SLAB_FIXTURES:
            self.assert_job_valid(name)
        self.assert_result_valid(self.result)
        counts = self.result["output"]["counts"]
        self.assertEqual((counts["slabs"], counts["stairs"]), (1, 1))
        self.assertEqual(self.result["output"]["problems"], [])
        steps = [s["step"] for s in self.result["output"]["steps"]]
        self.assertLess(steps.index("floors"), steps.index("slabs"))
        self.assertLess(steps.index("slabs"), steps.index("stairs"))
        self.assertLess(steps.index("stairs"), steps.index("materials"))

    # The slab stands from 3.0 to 3.2 with side faces only: no top and no bottom.
    def test_slab(self):
        obj = bpy.data.objects["SM_f1_Slab"]
        self.assertEqual((obj["plynth_kind"], obj["plynth_id"]), ("slab", "f1"))
        bm = mesh_of("SM_f1_Slab")
        self.assertEqual(sorted({round(v.co.z, 5) for v in bm.verts}), [3.0, 3.2])
        self.assertTrue(all(abs(f.normal.z) < 1e-6 for f in bm.faces))
        self.assertEqual(len(bm.faces), 8)
        self.assertAlmostEqual(area_of("SM_f1_Slab"), (2 * 10.4 + 2 * 5.32) * 0.2, places=4)

    # The edge slot is on the outer ring and the well slot on the hole, each facing away from the slab.
    def test_slab_materials(self):
        params = sharp("two_floors_stair")
        params["floors"][0]["slab"]["slots"] = {"edge": "edge_mat", "well": "well_mat", "underside": "Soffit"}
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.open_blend()
        obj = bpy.data.objects["SM_f1_Slab"]
        by_slot = {}
        for face in mesh_of("SM_f1_Slab").faces:
            by_slot.setdefault(obj.data.materials[face.material_index].name, []).append(face)
        self.assertEqual(sorted(by_slot), ["edge_mat", "well_mat"])
        self.assertEqual(len(by_slot["edge_mat"]), 4)
        self.assertEqual(len(by_slot["well_mat"]), 4)
        for face in by_slot["well_mat"]:
            center = face.calc_center_median()
            self.assertTrue(0.6 - 1e-6 <= center.x <= 4.92 + 1e-6 and -2.0 - 1e-6 <= center.y <= -1.0 + 1e-6)
            inward = (2.76 - center.x, -1.5 - center.y)
            self.assertGreater(face.normal.x * inward[0] + face.normal.y * inward[1], 0.0)
        for face in by_slot["edge_mat"]:
            center = face.calc_center_median()
            outward = (center.x - 3.0, center.y + 2.0)
            self.assertGreater(face.normal.x * outward[0] + face.normal.y * outward[1], 0.0)

    # Upper walls start at 3.2 and ground walls at 0.
    def test_walls_start(self):
        low = [v.co.z for v in bpy.data.objects["SM_f1_Walls"].data.vertices]
        high = [v.co.z for v in bpy.data.objects["SM_f2_Walls"].data.vertices]
        self.assertAlmostEqual(min(low), 0.0, places=5)
        self.assertAlmostEqual(min(high), 3.2, places=5)
        self.assertAlmostEqual(max(high), 6.2, places=5)

    # The stair has 16 closed steps, step k topping at k times the riser, with world UVs on the treads.
    def test_stair(self):
        obj = bpy.data.objects["SM_f1_s1_Stair"]
        self.assertEqual((obj["plynth_kind"], obj["plynth_id"]), ("stair", "s1"))
        self.assertEqual(slot_names(obj), ["Stair"])
        bm = mesh_of("SM_f1_s1_Stair")
        self.assertTrue(all(len(e.link_faces) == 2 for e in bm.edges))
        self.assertEqual(len(bm.verts), 16 * 8)
        tops = sorted({round(v.co.z, 5) for v in bm.verts if v.co.z > 1e-6})
        self.assertEqual(len(tops), 16)
        for k, top in enumerate(tops, start=1):
            self.assertAlmostEqual(top, k * RISER, places=4)
        self.assertAlmostEqual(bm.calc_volume(), sum(0.27 * 1.0 * k * RISER for k in range(1, 17)), places=3)
        uv = bm.loops.layers.uv.get("UVMap")
        treads = [f for f in bm.faces if f.normal.z > 0.99]
        self.assertTrue(treads)
        for face in treads:
            for loop in face.loops:
                self.assertAlmostEqual(loop[uv].uv.x, loop.vert.co.x, places=4)
                self.assertAlmostEqual(loop[uv].uv.y, loop.vert.co.y, places=4)

    # Every face of the stair and the slab has UVs in metres: sides and risers as much as treads. A stair side
    # with no UV area smears the texture into streaks.
    def test_uv_areas(self):
        for name in ("SM_f1_s1_Stair", "SM_f1_Slab"):
            self.assertEqual(uv_mismatches(name), [], name)

    # The ground ceiling and the upper floor lose the stair footprint, the rest stay whole.
    def test_holes(self):
        room = 5.8 * 3.8
        self.assertAlmostEqual(area_of("SM_f1_r1_Ceiling"), room - 4.32, places=4)
        self.assertAlmostEqual(area_of("SM_f2_r1_Floor"), room - 4.32, places=4)
        self.assertAlmostEqual(area_of("SM_f2_r1_Ceiling"), room, places=4)
        self.assertAlmostEqual(area_of("SM_f1_r1_Floor"), room, places=4)

    # Faces with holes are tessellated: no face has more than four corners.
    def test_no_ngons(self):
        for name in ("SM_f1_Slab", "SM_f1_r1_Ceiling", "SM_f2_r1_Floor"):
            self.assertTrue(all(len(f.verts) <= 4 for f in mesh_of(name).faces), name)

    # The slab and the stair are listed for the preview.
    def test_listed(self):
        with open(self.path("build", "materials.json"), encoding="utf-8") as handle:
            names = [o["name"] for o in json.load(handle)["objects"]]
        self.assertIn("SM_f1_Slab", names)
        self.assertIn("SM_f1_s1_Stair", names)


class SlabFaceTests(StoreyTest):
    # Builds a slab of the given polygons, sides and tops at 3.0 to 3.2 with the edge and well materials told
    # apart, and returns the result. Opens the built file.
    def slab_of(self, polygons, sides, tops=()):
        params = sharp("two_floors_stair")
        slab = params["floors"][0]["slab"]
        slab["polygons"] = polygons
        slab["sides"] = sides
        slab["tops"] = list(tops)
        slab["undersides"] = []
        slab["slots"] = {"edge": "edge_mat", "well": "well_mat", "underside": "Soffit"}
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.open_blend()
        return result

    # Returns the material name of each face of the slab with the face centre, as (name, centre) pairs.
    def painted(self):
        obj = bpy.data.objects["SM_f1_Slab"]
        return [(obj.data.materials[f.material_index].name, f.calc_center_median(), f.normal.copy())
                for f in mesh_of("SM_f1_Slab").faces]

    # A courtyard's inner band is the outside material, facing into the courtyard.
    def test_courtyard_inner_band_is_outside(self):
        square = lambda a, b: [[a, a], [b, a], [b, b], [a, b]]
        hole = square(3.0, 7.0)[::-1]
        result = self.slab_of([{"outline": square(0.0, 10.0), "holes": [hole]}],
                              [{"outline": ["edge"] * 4, "holes": [["edge"] * 4]}])
        self.assertEqual(result["output"]["problems"], [])
        faces = self.painted()
        self.assertEqual(sorted({name for name, _, _ in faces}), ["edge_mat"])
        self.assertEqual(len(faces), 8)
        for _, centre, normal in faces:
            if 2.5 < centre.x < 7.5 and 2.5 < centre.y < 7.5:
                self.assertGreater(normal.x * (5.0 - centre.x) + normal.y * (5.0 - centre.y), 0.0)

    # A well that reaches the edge is part of the outline and keeps the inside material, the rest of the
    # outline the outside material.
    def test_well_touching_the_edge(self):
        outline = [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [6.0, 10.0], [6.0, 6.0], [4.0, 6.0], [4.0, 10.0], [0.0, 10.0]]
        names = ["edge", "edge", "edge", "well", "well", "well", "edge", "edge"]
        self.slab_of([{"outline": outline, "holes": []}], [{"outline": names, "holes": []}])
        wells = [c for name, c, _ in self.painted() if name == "well_mat"]
        edges = [c for name, c, _ in self.painted() if name == "edge_mat"]
        self.assertEqual((len(wells), len(edges)), (3, 5))
        self.assertEqual(sorted((round(c.x, 3), round(c.y, 3)) for c in wells), [(4.0, 8.0), (5.0, 6.0), (6.0, 8.0)])

    # Names stay with their edges when a ring is given clockwise (outline) or anticlockwise (hole), so it is turned.
    def test_turned_rings_keep_their_names(self):
        outline = [[0.0, 0.0], [0.0, 10.0], [6.0, 10.0], [6.0, 6.0], [4.0, 6.0], [4.0, 10.0], [10.0, 10.0], [10.0, 0.0]]
        names = ["edge", "edge", "well", "well", "well", "edge", "edge", "edge"]
        self.slab_of([{"outline": outline, "holes": []}], [{"outline": names, "holes": []}])
        wells = sorted((round(c.x, 3), round(c.y, 3)) for name, c, _ in self.painted() if name == "well_mat")
        self.assertEqual(wells, [(4.0, 8.0), (5.0, 6.0), (6.0, 8.0)])
        hole = [[3.0, 3.0], [7.0, 3.0], [7.0, 7.0], [3.0, 7.0]]
        self.slab_of([{"outline": [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0]], "holes": [hole]}],
                     [{"outline": ["edge"] * 4, "holes": [["well", "edge", "edge", "edge"]]}])
        wells = [c for name, c, _ in self.painted() if name == "well_mat"]
        self.assertEqual([(round(c.x, 3), round(c.y, 3)) for c in wells], [(5.0, 3.0)])

    # A ledge as thin as 1e-6 square metres has an upward face at the top with the edge material, welded to the
    # top of the band along the shared edge so that no gap is left.
    def test_hairline_ledge_meets_the_band(self):
        square = [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0]]
        ledge = {"outline": [[0.0, 0.0], [10.0, 0.0], [10.0, 1e-7], [0.0, 1e-7]], "holes": []}
        result = self.slab_of([{"outline": square, "holes": []}], [{"outline": ["edge"] * 4, "holes": []}], [ledge])
        self.assertEqual(result["output"]["problems"], [])
        bm = mesh_of("SM_f1_Slab")
        ups = [f for f in bm.faces if f.normal.z > 0.5]
        self.assertTrue(ups)
        self.assertAlmostEqual(sum(f.calc_area() for f in ups), 1e-6, delta=1e-8)
        self.assertTrue(all(abs(v.co.z - 3.2) < 1e-6 for f in ups for v in f.verts))
        obj = bpy.data.objects["SM_f1_Slab"]
        self.assertEqual({obj.data.materials[f.material_index].name for f in ups}, {"edge_mat"})
        shared = [e for e in bm.edges if len(e.link_faces) == 2 and
                  any(f.normal.z > 0.5 for f in e.link_faces) and any(abs(f.normal.z) < 1e-6 for f in e.link_faces)]
        self.assertEqual(len(shared), 1)
        self.assertTrue(all(abs(v.co.y) < 1e-6 and abs(v.co.z - 3.2) < 1e-6 for v in shared[0].verts))

    # The top faces take world X and Y as UVs.
    def test_ledge_uvs(self):
        square = [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0]]
        ledge = {"outline": [[1.0, 1.0], [3.0, 1.0], [3.0, 1.5], [1.0, 1.5]], "holes": []}
        self.slab_of([{"outline": square, "holes": []}], [{"outline": ["edge"] * 4, "holes": []}], [ledge])
        bm = mesh_of("SM_f1_Slab")
        layer = bm.loops.layers.uv.active
        for face in bm.faces:
            if face.normal.z > 0.5:
                for loop in face.loops:
                    self.assertAlmostEqual(loop[layer].uv[0], loop.vert.co.x, places=5)
                    self.assertAlmostEqual(loop[layer].uv[1], loop.vert.co.y, places=5)

    # Side lists that do not fit the rings are reported as slab_failed and nothing is built for the slab.
    def test_lengths_must_match(self):
        square = [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0]]
        hole = [[3.0, 3.0], [3.0, 7.0], [7.0, 7.0], [7.0, 3.0]]
        cases = {
            "ring": ([{"outline": ["edge"] * 3, "holes": []}], square, []),
            "hole count": ([{"outline": ["edge"] * 4, "holes": []}], square, [hole]),
            "polygon count": ([], square, []),
            "hole ring": ([{"outline": ["edge"] * 4, "holes": [["edge"] * 3]}], square, [hole]),
        }
        for name, (sides, outline, holes) in cases.items():
            params = sharp("two_floors_stair")
            slab = params["floors"][0]["slab"]
            slab["polygons"] = [{"outline": outline, "holes": holes}]
            slab["sides"] = sides
            result = self.build(params)
            self.assertTrue(result["ok"], result["error"])
            self.assertEqual([p["code"] for p in result["output"]["problems"]], ["slab_failed"], name)
            self.assertEqual(result["output"]["counts"]["slabs"], 0, name)


class OverhangTests(StoreyTest):
    # The overhang fixture has an underside strip of 1 m facing down at z 3.0 and no top face.
    def test_underside(self):
        result = self.build(sharp("overhang"))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.open_blend()
        bm = mesh_of("SM_f1_Slab")
        down = [f for f in bm.faces if f.normal.z < -0.99]
        self.assertTrue(down)
        self.assertAlmostEqual(sum(f.calc_area() for f in down), 4.2 * 1.0, places=4)
        for face in down:
            for vert in face.verts:
                self.assertAlmostEqual(vert.co.z, 3.0, places=5)
        self.assertFalse([f for f in bm.faces if f.normal.z > 0.99])
        self.assertEqual(slot_names(bpy.data.objects["SM_f1_Slab"]), ["Exterior_Wall", "Soffit"])
        self.assertEqual(result["output"]["counts"]["slabs"], 1)
        self.assertEqual(result["output"]["counts"]["stairs"], 0)

    # A floor without a slab makes no slab object.
    def test_no_slab(self):
        result = self.build(sharp("two_floors"))
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["counts"]["slabs"], 0)
        self.open_blend()
        self.assertFalse([o for o in bpy.data.objects if o.name.endswith("_Slab")])


class FailureTests(StoreyTest):
    # A slab with a ring of two points is an error that names the floor, and the build carries on.
    def test_slab_failed(self):
        params = sharp("two_floors_stair")
        params["floors"][0]["slab"]["polygons"][0]["outline"] = [[0.0, 0.0], [1.0, 1.0]]
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["severity"], p["floor"], p["object"]) for p in problems],
                         [("slab_failed", "error", "f1", "f1")])
        self.assertEqual(problems[0]["step"], "slabs")
        counts = result["output"]["counts"]
        self.assertEqual((counts["slabs"], counts["stairs"]), (0, 1))

    # A slab ring with no area is refused the same way.
    def test_slab_flat_ring(self):
        params = sharp("two_floors_stair")
        params["floors"][0]["slab"]["polygons"][0]["outline"] = [[0.0, 0.0], [1.0, 0.0], [2.0, 0.0]]
        result = self.build(params)
        self.assertEqual([p["code"] for p in result["output"]["problems"]], ["slab_failed"])

    # A stair with a bad step is reported and makes no object, and the other stairs still build.
    def test_stair_failed(self):
        params = sharp("two_floors_stair")
        other = copy.deepcopy(params["floors"][0]["stairs"][0])
        other["id"] = "s2"
        params["floors"][0]["stairs"][0]["steps"][3]["outline"] = [[0.0, 0.0], [1.0, 1.0]]
        params["floors"][0]["stairs"].append(other)
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["object"], p["floor"]) for p in problems], [("stair_failed", "s1", "f1")])
        self.assertEqual(problems[0]["severity"], "error")
        self.assertEqual(result["output"]["counts"]["stairs"], 1)
        self.open_blend()
        self.assertNotIn("SM_f1_s1_Stair", bpy.data.objects)
        self.assertIn("SM_f1_s2_Stair", bpy.data.objects)

    # A step with no height is a stair failure.
    def test_stair_no_height(self):
        params = sharp("two_floors_stair")
        params["floors"][0]["stairs"][0]["steps"][0]["top"] = 0.0
        result = self.build(params)
        self.assertEqual([p["code"] for p in result["output"]["problems"]], ["stair_failed"])


class SourceTests(unittest.TestCase):
    # The Blender code never names the second renderer.
    def test_no_second_renderer(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        word = "cyc" + "les"
        for folder, _, files in os.walk(root):
            for name in files:
                if name.endswith(".py"):
                    with open(os.path.join(folder, name), encoding="utf-8") as handle:
                        self.assertNotIn(word, handle.read().lower(), os.path.join(folder, name))


if __name__ == "__main__":
    unittest.main()
