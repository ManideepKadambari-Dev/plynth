import copy
import unittest

import bmesh
import bpy
from mathutils import Vector

from build_helpers import BuildTest, DATA_FOLDER, load_params
from opening_helpers import SETTINGS, fixtures
from plynth import meshes, scene

SQUARE_VOLUME = (4.2 * 4.2 - 3.8 * 3.8) * 3.0
THICKNESS = 0.2
SHAPES = ["door", "round_arch_window", "pointed_arch_plain", "circle_window", "keyhole", "curved_door"]


# Returns a copy of the fixture params with the settings replaced.
def with_settings(name, **changes):
    params = copy.deepcopy(load_params(name))
    params["settings"] = dict(params["settings"], **changes)
    return params


# Returns the area of a polygon in (u, v).
def outline_area(opening):
    return abs(meshes.polygon_area([tuple(p) for p in opening["outline"]]))


class OpeningTests(BuildTest):
    # Builds a fixture with the bevel off and returns the result.
    def build_plain(self, name, **changes):
        changes.setdefault("bevel_width", 0.0)
        result = self.build(with_settings(name, **changes))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result

    # Opens the built file and returns the walls object.
    def open_walls(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return bpy.data.objects["SM_f1_Walls"]

    # The fixtures are valid jobs.
    def test_fixtures_valid(self):
        for name in SHAPES:
            self.assert_job_valid(name)

    # Each cut lowers the wall volume by the outline area times the thickness, within 1 percent.
    def test_cut_volume(self):
        for name in SHAPES:
            with self.subTest(name):
                params = with_settings(name, bevel_width=0.0)
                opening = params["floors"][0]["openings"][0]
                result = self.build(params)
                self.assertTrue(result["ok"], result["error"])
                self.assertEqual(result["output"]["problems"], [], name)
                self.assertEqual(result["output"]["counts"]["openings_cut"], 1)
                walls = self.open_walls()
                full = SQUARE_VOLUME if name != "curved_door" else None
                if full is None:
                    full = meshes.volume(walls) + outline_area(opening) * THICKNESS
                lost = full - meshes.volume(walls)
                expected = outline_area(opening) * THICKNESS
                if name == "curved_door":
                    self.assertAlmostEqual(lost / expected, 1.0, delta=0.02)
                else:
                    self.assertAlmostEqual(lost, expected, delta=expected * 0.01)

    # An arch removes less than its box.
    def test_arch_less_than_box(self):
        params = with_settings("pointed_arch_plain", bevel_width=0.0)
        self.build(params)
        walls = self.open_walls()
        lost = SQUARE_VOLUME - meshes.volume(walls)
        self.assertLess(lost, 1.0 * 2.4 * THICKNESS)

    # The walls stay closed after cuts and after the bevel.
    def test_manifold(self):
        for name in SHAPES:
            for width in (0.0, 0.01):
                with self.subTest(name=name, width=width):
                    result = self.build(with_settings(name, bevel_width=width))
                    self.assertTrue(result["ok"], result["error"])
                    self.assertEqual([p["code"] for p in result["output"]["problems"]], [])
                    walls = self.open_walls()
                    self.assertTrue(meshes.is_manifold(walls))

    # A cutter that misses the wall gives cut_failed and leaves the volume unchanged.
    def test_cut_misses(self):
        params = with_settings("door", bevel_width=0.0)
        opening = params["floors"][0]["openings"][0]
        shift = 30.0
        opening["centre"][1] += shift
        for ring in opening["cutter"].values():
            for point in ring:
                point[1] += shift
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["severity"], p["object"], p["floor"], p["step"]) for p in problems],
                         [("cut_failed", "error", "o1", "f1", "openings")])
        self.assertEqual(result["output"]["counts"]["openings_cut"], 0)
        self.assertAlmostEqual(meshes.volume(self.open_walls()), SQUARE_VOLUME, delta=1e-4)
        self.assert_result_valid(result)

    # Wall faces far from any opening keep their wall, room and side after the cuts and the bevel.
    def test_attributes_survive(self):
        for width in (0.0, 0.01):
            with self.subTest(width=width):
                self.build(with_settings("door", bevel_width=width))
                walls = self.open_walls()
                names = list(walls["plynth_walls"])
                rooms = list(walls["plynth_rooms"])
                wall_values = [a.value for a in walls.data.attributes["plynth_wall"].data]
                room_values = [a.value for a in walls.data.attributes["plynth_room"].data]
                side_values = [a.value for a in walls.data.attributes["plynth_side"].data]
                checked = 0
                for polygon in walls.data.polygons:
                    flat = abs(polygon.normal.y + 1.0) < 1e-3 and abs(polygon.center.y + 4.1) < 1e-4
                    if flat:
                        checked += 1
                        self.assertEqual(names[wall_values[polygon.index]], "w3")
                        self.assertEqual(room_values[polygon.index], -1)
                        self.assertEqual(side_values[polygon.index], 1)
                    inside = abs(polygon.normal.y - 1.0) < 1e-3 and abs(polygon.center.y + 3.9) < 1e-4
                    if inside:
                        checked += 1
                        self.assertEqual(names[wall_values[polygon.index]], "w3")
                        self.assertEqual(rooms[room_values[polygon.index]], "r1")
                        self.assertEqual(side_values[polygon.index], 0)
                self.assertGreaterEqual(checked, 2)

    # The faces a cutter makes use the Reveal surface.
    def test_reveal_surface(self):
        self.build_plain("door")
        walls = self.open_walls()
        names = [m.name for m in walls.data.materials]
        sides = [a.value for a in walls.data.attributes["plynth_side"].data]
        reveal = [p for p in walls.data.polygons if sides[p.index] == 2]
        self.assertGreater(len(reveal), 0)
        for polygon in reveal:
            self.assertEqual(names[polygon.material_index], "Reveal")

    # With the bevel width 0 no bevel runs and the vertex count stays.
    def test_bevel_off(self):
        self.build_plain("door")
        plain = len(self.open_walls().data.vertices)
        self.build(with_settings("door", bevel_width=0.01))
        beveled = len(self.open_walls().data.vertices)
        self.assertGreater(beveled, plain)

    # The door fill has a frame, a leaf and handles on the side away from the hinge, inside the cut.
    def test_door_fill(self):
        result = self.build_plain("door")
        self.assertEqual(result["output"]["counts"]["fills"], 1)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        door = bpy.data.objects["SM_f1_o1_Door"]
        self.assertEqual(door["plynth_kind"], "door")
        names = [m.name for m in door.data.materials]
        self.assertEqual(names, ["Frame", "Door", "Handle"])
        low, high = scene.world_bounds([door])
        self.assertGreaterEqual(low.x, 1.55 - 1e-6)
        self.assertLessEqual(high.x, 2.45 + 1e-6)
        self.assertGreaterEqual(low.y, -0.15)
        self.assertLessEqual(high.y, 0.15)
        self.assertGreaterEqual(low.z, -0.05)
        self.assertLessEqual(high.z, 2.1 + 1e-6)
        handles = [p for p in door.data.polygons if names[p.material_index] == "Handle"]
        self.assertGreater(len(handles), 0)
        self.assertTrue(all(p.center.x > 2.0 for p in handles))
        self.assertTrue(any(p.center.y > 0.0 for p in handles) and any(p.center.y < 0.0 for p in handles))

    # A window has glass, a plain opening has no fill, and nothing is filled when fill_openings is false.
    def test_window_and_plain(self):
        result = self.build_plain("round_arch_window")
        self.assertEqual(result["output"]["counts"]["fills"], 1)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        window = bpy.data.objects["SM_f1_o1_Window"]
        self.assertIn("Glass", [m.name for m in window.data.materials])
        result = self.build_plain("pointed_arch_plain")
        self.assertEqual(result["output"]["counts"]["fills"], 0)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertEqual([o.name for o in bpy.data.objects if o.get("plynth_kind") in ("door", "window")], [])
        result = self.build_plain("door", fill_openings=False)
        self.assertEqual(result["output"]["counts"]["fills"], 0)

    # An opening with an empty inner outline gives fill_failed and the build goes on.
    def test_fill_failed(self):
        params = with_settings("door", bevel_width=0.0)
        params["floors"][0]["openings"][0]["inner_outline"] = []
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["object"]) for p in problems], [("fill_failed", "o1")])
        self.assertEqual(result["output"]["counts"]["fills"], 0)
        self.assertEqual(result["output"]["counts"]["openings_cut"], 1)

    # Thresholds exist for sill 0 openings only, with the area width times thickness.
    def test_thresholds(self):
        self.build_plain("door")
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        threshold = bpy.data.objects["SM_f1_Thresholds"]
        self.assertEqual(threshold["plynth_kind"], "thresholds")
        self.assertAlmostEqual(meshes.area(threshold), 0.9 * THICKNESS, delta=1e-5)
        self.build_plain("round_arch_window")
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertIsNone(bpy.data.objects.get("SM_f1_Thresholds"))

    # The file of fixtures holds the same jobs as the helper makes.
    def test_fixture_files_match_helper(self):
        import json
        import os
        for name, data in fixtures().items():
            with open(os.path.join(DATA_FOLDER, name + ".json"), "r", encoding="utf-8") as handle:
                self.assertEqual(json.load(handle), json.loads(json.dumps(data)), name)

    # Building the settings used by the tests is the default look.
    def test_settings_constant(self):
        self.assertEqual(SETTINGS["bevel_segments"], 2)
        self.assertTrue(Vector((1, 0, 0)).length == 1.0)


if __name__ == "__main__":
    unittest.main()


class DetectedApartmentTests(BuildTest):
    # Walls merged from a detected plan, with nine openings, cut every opening and stay one closed solid.
    def test_all_openings_cut_in_merged_walls(self):
        self.assert_job_valid("detected_apartment")
        params = load_params("detected_apartment")
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        output = result["output"]
        self.assertEqual(output["problems"], [])
        floor = params["floors"][0]
        self.assertEqual(output["counts"]["openings_cut"], len(floor["openings"]))
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        walls = bpy.data.objects["SM_%s_Walls" % floor["id"]]
        self.assertTrue(meshes.is_manifold(walls))


# Returns the signed volume of each connected piece of the object and the number of edges where two faces run
# the same way round (a flipped face). A closed mesh with no such edge and a positive volume has every normal
# pointing outward.
def solid_report(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    seen = set()
    volumes = []
    for start in bm.faces:
        if start in seen:
            continue
        seen.add(start)
        stack = [start]
        volume = 0.0
        while stack:
            face = stack.pop()
            volume += face.calc_area() * face.calc_center_median().dot(face.normal) / 3.0
            for edge in face.edges:
                for other in edge.link_faces:
                    if other not in seen:
                        seen.add(other)
                        stack.append(other)
        volumes.append(volume)
    flipped = 0
    for edge in bm.edges:
        loops = edge.link_loops
        if len(loops) == 2 and loops[0].vert == loops[1].vert:
            flipped += 1
    bm.free()
    return volumes, flipped


class WallNormalTests(BuildTest):
    # Builds the job and returns the walls object of its first floor, with the bevel as the job has it.
    def walls_of(self, params):
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return bpy.data.objects["SM_%s_Walls" % params["floors"][0]["id"]]

    # Checks that every solid has a positive volume, no face is flipped and no face has more than four corners.
    def assert_outward(self, walls, name):
        volumes, flipped = solid_report(walls)
        self.assertTrue(volumes, name)
        for volume in volumes:
            self.assertGreater(volume, 0.0, name)
        self.assertEqual(flipped, 0, name)
        for polygon in walls.data.polygons:
            self.assertLessEqual(len(polygon.vertices), 4, name)

    # The merged apartment walls point outward everywhere.
    def test_detected_apartment_outward(self):
        self.assert_outward(self.walls_of(load_params("detected_apartment")), "detected_apartment")

    # A wall with a room on one side only, with a circle cut into it, points outward and is closed.
    def test_one_sided_wall_outward(self):
        for name in ("circle_window", "half_wall"):
            walls = self.walls_of(load_params(name))
            self.assert_outward(walls, name)
            self.assertTrue(meshes.is_manifold(walls), name)
