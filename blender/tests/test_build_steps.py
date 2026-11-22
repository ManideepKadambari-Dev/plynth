import copy
from collections import defaultdict
from unittest import mock

import bmesh
import bpy

from build_helpers import BuildTest, load_params
from plynth import meshes, registry
from plynth.building import Step, openings, triangulate

SQUARE_VOLUME = (4.2 * 4.2 - 3.8 * 3.8) * 3.0
DOOR_VOLUME = 0.9 * 2.1 * 0.2


# Returns a copy of the fixture params with the bevel off.
def plain_params(name):
    params = copy.deepcopy(load_params(name))
    params["settings"] = dict(params["settings"], bevel_width=0.0)
    return params


# Adds up the face area of an object per side tag, per room tag and per material index, and returns the most
# corners any face has.
def face_stats(obj):
    sides = [a.value for a in obj.data.attributes["plynth_side"].data]
    rooms = [a.value for a in obj.data.attributes["plynth_room"].data]
    by_side, by_room, by_material = defaultdict(float), defaultdict(float), defaultdict(float)
    for polygon in obj.data.polygons:
        by_side[sides[polygon.index]] += polygon.area
        by_room[rooms[polygon.index]] += polygon.area
        by_material[polygon.material_index] += polygon.area
    corners = max(len(p.vertices) for p in obj.data.polygons)
    return {"side": dict(by_side), "room": dict(by_room), "material": dict(by_material), "corners": corners}


# Swaps a registered build step for one that wraps it, and puts the original back afterwards.
def wrap_step(test, step_id, wrapper):
    original = registry.BUILD_STEPS.get(step_id)
    registry.BUILD_STEPS._entries[step_id] = Step(original.order, original.message, wrapper(original.run))
    test.addCleanup(registry.BUILD_STEPS._entries.__setitem__, step_id, original)


class TriangulateTests(BuildTest):
    # After the step no wall face has more than four corners and the tags and materials keep their area.
    def test_triangulate_keeps_attributes(self):
        seen = []

        def wrapper(real):
            def run(ctx):
                walls = ctx.state["walls_objects"]["f1"]
                before = face_stats(walls)
                real(ctx)
                seen.append((before, face_stats(walls)))
            return run

        wrap_step(self, "triangulate", wrapper)
        result = self.build(plain_params("door"))
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(len(seen), 1)
        before, after = seen[0]
        self.assertGreater(before["corners"], triangulate.MAX_SIDES)
        self.assertLessEqual(after["corners"], triangulate.MAX_SIDES)
        for key in ("side", "room", "material"):
            self.assertEqual(set(before[key]), set(after[key]), key)
            for tag, area in before[key].items():
                self.assertAlmostEqual(after[key][tag], area, delta=area * 1e-4 + 1e-6, msg="%s %s" % (key, tag))


class CutFallbackTests(BuildTest):
    # Builds the door fixture with boolean_cut replaced by a function that gets the solver and the real function.
    def build_with(self, replacement):
        real = openings.boolean_cut
        calls = []

        def patched(walls, cutter, solver):
            calls.append((solver, meshes.volume(walls), len(walls.data.vertices)))
            return replacement(real, walls, cutter, solver)

        with mock.patch.object(openings, "boolean_cut", patched):
            result = self.build(plain_params("door"))
        self.assertTrue(result["ok"], result["error"])
        return result, calls

    # Opens the built file and returns the walls object.
    def open_walls(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return bpy.data.objects["SM_f1_Walls"]

    # Throws away half of the faces, as a solver that returned a broken result would.
    @staticmethod
    def break_walls(walls):
        bm = bmesh.new()
        bm.from_mesh(walls.data)
        bmesh.ops.delete(bm, geom=bm.faces[:len(bm.faces) // 2], context="FACES")
        bm.to_mesh(walls.data)
        bm.free()

    # A first solver that raises falls back to Exact and the cut succeeds.
    def test_first_solver_raises(self):
        def replacement(real, walls, cutter, solver):
            if solver == "MANIFOLD":
                raise RuntimeError("solver failed")
            return real(walls, cutter, solver)

        result, calls = self.build_with(replacement)
        self.assertEqual([c[0] for c in calls], ["MANIFOLD", "EXACT"])
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["openings_cut"], 1)
        volume = meshes.volume(self.open_walls())
        self.assertAlmostEqual(volume, SQUARE_VOLUME - DOOR_VOLUME, delta=DOOR_VOLUME * 0.01)

    # A first solver that leaves a broken result is undone before Exact runs.
    def test_first_solver_broken(self):
        def replacement(real, walls, cutter, solver):
            if solver == "MANIFOLD":
                real(walls, cutter, solver)
                self.break_walls(walls)
                return False
            return real(walls, cutter, solver)

        result, calls = self.build_with(replacement)
        self.assertEqual([c[0] for c in calls], ["MANIFOLD", "EXACT"])
        self.assertEqual(calls[0][1:], calls[1][1:])
        self.assertAlmostEqual(calls[1][1], SQUARE_VOLUME, delta=1e-4)
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["openings_cut"], 1)
        walls = self.open_walls()
        self.assertTrue(meshes.is_manifold(walls))
        self.assertAlmostEqual(meshes.volume(walls), SQUARE_VOLUME - DOOR_VOLUME, delta=DOOR_VOLUME * 0.01)

    # When both solvers fail the opening is reported as cut_failed and the walls are as they were.
    def test_both_solvers_fail(self):
        def replacement(real, walls, cutter, solver):
            if solver == "MANIFOLD":
                raise RuntimeError("solver failed")
            real(walls, cutter, solver)
            self.break_walls(walls)
            return False

        result, calls = self.build_with(replacement)
        self.assertEqual([c[0] for c in calls], ["MANIFOLD", "EXACT"])
        self.assertEqual(calls[0][1:], calls[1][1:])
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["object"]) for p in problems], [("cut_failed", "o1")])
        self.assertEqual(result["output"]["counts"]["openings_cut"], 0)
        walls = self.open_walls()
        self.assertAlmostEqual(meshes.volume(walls), SQUARE_VOLUME, delta=1e-4)
        self.assertEqual(len(walls.data.vertices), calls[0][2])
