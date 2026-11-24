import copy
import json
import math
import os
import unittest

import bmesh
import bpy

from build_helpers import BuildTest, load_params
from plynth import meshes
from plynth.building import dressing, lights, opening_assets, trim

BOX = [(0.0, 0.0), (0.02, 0.0), (0.02, 0.1), (0.0, 0.1)]


# A closed ring of sections round a square, the profile standing in toward its middle.
def square_ring(low, high, z):
    corners = [(low, -low), (low, -high), (high, -high), (high, -low)]
    sections = []
    for x, y in corners:
        dx = 1.0 if x == low else -1.0
        dy = -1.0 if y == -low else 1.0
        sections.append([(x + dx * u, y + dy * u, z + v) for u, v in BOX])
    return {"closed": True, "sections": sections}


# A straight open sweep along X.
def straight(x0, x1, y, z):
    return {"closed": False, "sections": [[(x0, y - u, z + v) for u, v in BOX], [(x1, y - u, z + v) for u, v in BOX]]}


# A trim entry of the job.
def trim_entry(kind, obj, sweeps, plynth_kind="trim", plan_id="f1"):
    return {"kind": kind, "plan_id": plan_id, "object": obj, "plynth_kind": plynth_kind, "slot": "Living_Wall",
            "material": None, "sweeps": sweeps}


class FakeContext:
    # Holds the params and state a build step reads, with one floor collection.
    def __init__(self, floors, collections):
        self.params = {"floors": floors}
        self.state = {"collections": {"f1": bpy.data.collections.new("f1_test")}, "asset_collections": collections}


class FinishBuildTests(BuildTest):
    # Makes a collection holding one cube, standing in for an appended asset.
    def cube_asset(self, name):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        mesh = bpy.data.meshes.new(name)
        bm.to_mesh(mesh)
        bm.free()
        collection = bpy.data.collections.new(name)
        collection.objects.link(bpy.data.objects.new(name, mesh))
        return collection

    def test_job_without_finishing_fields_builds_as_before(self):
        result = self.build(load_params("square_room"))
        self.assertTrue(result["ok"], result.get("error"))
        counts = result["output"].get("counts", {})
        for name in ("trim", "lights", "dressing", "opening_assets"):
            self.assertNotIn(name, counts)

    def test_trim_meshes_are_closed_named_and_unwrapped_in_metres(self):
        params = copy.deepcopy(load_params("square_room"))
        params["floors"][0]["trim"] = [
            trim_entry("skirting", "SM_f1_Skirting", [square_ring(0.1, 3.9, 0.0)]),
            trim_entry("railing", "SM_f1_s1_Railing", [straight(0.5, 2.5, -1.0, 0.9)], "railing", "s1"),
        ]
        result = self.build(params)
        self.assertTrue(result["ok"], result.get("error"))
        self.assertEqual(result["output"]["counts"].get("trim"), 2)
        skirting = bpy.data.objects["SM_f1_Skirting"]
        self.assertEqual(skirting["plynth_kind"], "trim")
        self.assertEqual(skirting["plynth_id"], "f1")
        self.assertEqual(skirting["plynth_trim"], "skirting")
        self.assertTrue(meshes.is_manifold(skirting))
        self.assertGreater(meshes.volume(skirting), 0.0)
        railing = bpy.data.objects["SM_f1_s1_Railing"]
        self.assertEqual(railing["plynth_kind"], "railing")
        self.assertEqual(railing["plynth_id"], "s1")
        self.assertTrue(meshes.is_manifold(railing))
        self.assertAlmostEqual(meshes.volume(railing), 2.0 * 0.02 * 0.1, places=6)
        self.assertEqual(len(skirting.data.materials), 1)
        self.assertIsNotNone(skirting.data.uv_layers.get("UVMap"))

    def test_a_bad_sweep_is_a_warning_and_the_build_goes_on(self):
        params = copy.deepcopy(load_params("square_room"))
        bad = {"closed": False, "sections": [[(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 0, 1), (1, 0, 1)]]}
        params["floors"][0]["trim"] = [trim_entry("sill", "SM_f1_Sill", [bad]),
                                       trim_entry("casing", "SM_f1_Casing", [straight(0.0, 1.0, 0.0, 0.0)])]
        result = self.build(params)
        self.assertTrue(result["ok"], result.get("error"))
        codes = [p["code"] for p in result["output"]["problems"]]
        self.assertIn("trim_failed", codes)
        self.assertNotIn("SM_f1_Sill", bpy.data.objects)
        self.assertIn("SM_f1_Casing", bpy.data.objects)

    def test_lights_stand_at_their_markers(self):
        params = copy.deepcopy(load_params("square_room"))
        params["floors"][0]["lights"] = [{"id": "l1", "kind": "ceiling", "location": [2.0, -2.0, 2.9], "power": 60,
                                          "color": [1.0, 0.9, 0.8]}]
        result = self.build(params)
        self.assertTrue(result["ok"], result.get("error"))
        self.assertEqual(result["output"]["counts"].get("lights"), 1)
        obj = bpy.data.objects["Light_f1_l1"]
        self.assertEqual(obj.type, "LIGHT")
        self.assertEqual(obj.data.type, "POINT")
        self.assertAlmostEqual(obj.data.energy, 60.0)
        self.assertEqual(obj["plynth_kind"], "light")
        self.assertEqual(obj["plynth_id"], "l1")
        for got, want in zip(obj.location, (2.0, -2.0, 2.9)):
            self.assertAlmostEqual(got, want, places=6)

    def test_opening_assets_are_placed_and_a_missing_one_warns(self):
        door = self.cube_asset("door_01")
        asset = {"id": "door_01", "location": [1.0, -0.0, 0.0], "rotation_z": math.pi / 2, "scale": [-1.02, 1.0, 0.99]}
        missing = {"id": "door_gone", "location": [3.0, 0.0, 0.0], "rotation_z": 0.0, "scale": [1.0, 1.0, 1.0]}
        floors = [{"id": "f1", "openings": [{"id": "o1", "type": "door", "asset": asset},
                                            {"id": "o2", "type": "window", "asset": missing},
                                            {"id": "o3", "type": "door"}]}]
        ctx = FakeContext(floors, {"door_01": door})
        opening_assets.run(ctx)
        obj = bpy.data.objects["SM_door_01_o1"]
        self.assertEqual(obj["plynth_kind"], "door")
        self.assertEqual(obj["plynth_id"], "o1")
        self.assertEqual(obj["plynth_asset"], "door_01")
        self.assertIs(obj.instance_collection, door)
        self.assertAlmostEqual(obj.scale.x, -1.02, places=6)
        self.assertAlmostEqual(obj.rotation_euler.z, math.pi / 2, places=6)
        problems = ctx.state.get("problems", [])
        self.assertEqual([p["code"] for p in problems], ["fill_asset_failed"])
        self.assertEqual(problems[0]["object"], "o2")
        self.assertEqual(ctx.state["counts"]["opening_assets"], 1)

    def test_dressing_shares_asset_collections(self):
        books = self.cube_asset("books")
        prop = {"room": "r1", "rule": "books", "host": "i4", "asset": "books", "rotation_z": 0.0,
                "size": {"width": 0.3, "depth": 0.2, "height": 0.25}}
        floors = [{"id": "f1", "dressing": [dict(prop, id="r1_d01", location=[1.0, -1.0, 0.4]),
                                            dict(prop, id="r1_d02", location=[1.3, -1.0, 0.4]),
                                            dict(prop, id="r1_d03", asset="vase", location=[2.0, -1.0, 0.8])]}]
        ctx = FakeContext(floors, {"books": books})
        dressing.run(ctx)
        first = bpy.data.objects["SM_books_r1_D01"]
        second = bpy.data.objects["SM_books_r1_D02"]
        self.assertIs(first.instance_collection, second.instance_collection)
        self.assertEqual(first["plynth_kind"], "dressing")
        self.assertEqual(first["plynth_id"], "r1")
        self.assertEqual(ctx.state["counts"]["dressing"], 2)
        self.assertEqual([p["code"] for p in ctx.state["problems"]], ["dressing_failed"])

    def test_trim_gets_its_material_after_the_materials_step(self):
        params = copy.deepcopy(load_params("square_room"))
        entry = trim_entry("skirting", "SM_f1_Skirting", [square_ring(0.1, 3.9, 0.0)])
        entry["slot"] = "Trim"
        params["floors"][0]["trim"] = [entry]
        params["floors"][0]["lights"] = [{"id": "l1", "kind": "wall", "location": [1.0, -1.0, 1.8], "power": 30,
                                          "color": [1.0, 0.9, 0.8]}]
        result = self.build(params)
        self.assertTrue(result["ok"], result.get("error"))
        skirting = bpy.data.objects["SM_f1_Skirting"]
        self.assertEqual(skirting.data.materials[0].name, "Trim")
        self.assertTrue(skirting.data.materials[0].use_nodes)
        with open(os.path.join(self.path("build"), result["output"]["materials_file"]), encoding="utf-8") as handle:
            written = json.load(handle)
        names = [m["name"] for m in written["materials"]]
        self.assertIn("Trim", names)
        objects = [o["name"] for o in written["objects"]]
        self.assertIn("SM_f1_Skirting", objects)
        self.assertNotIn("Light_f1_l1", objects)

    def test_steps_skip_floors_without_their_fields(self):
        ctx = FakeContext([{"id": "f1"}], {})
        for module in (trim, dressing, lights, opening_assets):
            module.run(ctx)
        self.assertNotIn("problems", ctx.state)


if __name__ == "__main__":
    unittest.main()
