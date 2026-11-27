import copy
import json
import os
import types
import unittest

import bpy
from mathutils import Matrix

import export_helpers
import helpers
from build_helpers import load_params
from export_helpers import DATA_FOLDER, ExportTest, grouped_groups
from plynth import scene
from plynth.quality import kit_file
from schema_check import load_schema, validate

WALL = "Wall_50x300x20_a1b2c3"
CORNER = "Corner_20x300x20_d4e5f6"
OPENING = "Opening_100x300x20_0a1b2c"
BARE = "Opening_100x300x20_ffeedd"
FILE = "Square_Kit.fbx"
HEIGHT = 3.0
KIT_FIXTURES = ("kit_three", "kit_bare_opening")


# Returns one ring edge of a kit solid.
def edge(wall, side, room, slot, cut=False):
    found = {"wall": wall, "side": side, "room": room, "material": None, "slot": slot}
    if cut:
        found["kit_cut"] = True
    return found


# Returns a solid from its corner points and edges, 3 m high.
def solid(points, edges):
    return {"height": HEIGHT, "top_material": None, "top_slot": "Wall_Top",
            "outline": {"points": [list(p) for p in points], "edges": edges}, "holes": []}


# Returns the collision prisms of a rectangle.
def prism(low, high, bottom=0.0, top=HEIGHT):
    (x0, y0), (x1, y1) = low, high
    return {"outline": [[x0, y0], [x1, y0], [x1, y1], [x0, y1]], "bottom": bottom, "top": top}


# Returns the wall piece of 0.5 m: the room side at y -0.1, the outside at y 0.1, a cut at each end.
def wall_piece():
    points = [(0.0, -0.1), (0.5, -0.1), (0.5, 0.1), (0.0, 0.1)]
    edges = [edge("w1", "right", "r1", "Living_Wall"), edge("w1", None, None, "Living_Wall", True),
             edge("w1", "left", None, "Exterior_Wall"), edge("w1", None, None, "Living_Wall", True)]
    return {"id": WALL, "mesh": "SM_Kit_" + WALL, "kind": "wall", "wall_solids": [solid(points, edges)],
            "openings": [], "fills": [], "collision": [prism((0.0, -0.1), (0.5, 0.1))]}


# Returns a corner piece: a 0.2 m square around its origin with two cuts.
def corner_piece():
    points = [(-0.1, -0.1), (0.1, -0.1), (0.1, 0.1), (-0.1, 0.1)]
    edges = [edge("w1", "right", "r1", "Living_Wall"), edge("w1", None, None, "Living_Wall", True),
             edge("w1", "left", None, "Exterior_Wall"), edge("w1", None, None, "Living_Wall", True)]
    return {"id": CORNER, "mesh": "SM_Kit_" + CORNER, "kind": "corner", "wall_solids": [solid(points, edges)],
            "openings": [], "fills": [], "collision": [prism((-0.1, -0.1), (0.1, 0.1))]}


# Returns the door of the square room moved into the frame of an opening piece 1 m long.
def door_in_frame():
    door = copy.deepcopy(load_params("door")["floors"][0]["openings"][0])
    shift = 1.5
    door["centre"][0] -= shift
    for side in ("right", "left"):
        for point in door["cutter"][side]:
            point[0] -= shift
    door["threshold"] = []
    return door


# Returns an opening piece 1 m long holding the door; fills lists the openings that get their fill.
def opening_piece(piece_id, fills):
    door = door_in_frame()
    points = [(0.0, -0.1), (1.0, -0.1), (1.0, 0.1), (0.0, 0.1)]
    edges = [edge("w1", "right", "r1", "Living_Wall"), edge("w1", None, None, "Living_Wall", True),
             edge("w1", "left", None, "Exterior_Wall"), edge("w1", None, None, "Living_Wall", True)]
    return {"id": piece_id, "mesh": "SM_Kit_" + piece_id, "kind": "opening", "wall_solids": [solid(points, edges)],
            "openings": [door], "fills": fills,
            "collision": [prism((0.0, -0.1), (0.05, 0.1)), prism((0.95, -0.1), (1.0, 0.1)),
                          prism((0.05, -0.1), (0.95, 0.1), 2.1, HEIGHT)]}


# Returns a kit of the given pieces.
def make_kit(pieces, fill_openings=True, width=0.01):
    return {"file": FILE, "fill_openings": fill_openings, "bevel": {"width": width, "segments": 2},
            "pieces": pieces}


# Writes the kit fixture job files into the folder.
def write_kit_fixtures(folder):
    os.makedirs(folder, exist_ok=True)
    kits = {"kit_three": make_kit([wall_piece(), corner_piece(), opening_piece(OPENING, ["o1"])]),
            "kit_bare_opening": make_kit([opening_piece(BARE, [])])}
    for name, kit in kits.items():
        params = export_helpers.job_params(profile="unreal")
        params["kit"] = kit
        data = {"format": "plynth-blender-job", "version": 1, "task": "export", "job_id": "export-1",
                "result_file": "result.json", "params": params}
        with open(os.path.join(folder, name + ".json"), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(data, indent=1) + "\n")


# Reads an FBX file into an empty scene and returns its mesh objects by name.
def read_file(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    return {o.name: o for o in bpy.data.objects if o.type == "MESH"}


# Returns the checks of an output by id.
def statuses(output):
    return {c["id"]: c for c in output["checks"]}


class ExportKitTests(ExportTest):
    profile = "unreal"

    # Returns the params of the clean shared project with a kit.
    def with_kit(self, pieces=None, groups=None, **kit_options):
        params = self.params(clean=True)
        params["kit"] = make_kit(pieces if pieces is not None else
                                 [wall_piece(), corner_piece(), opening_piece(OPENING, ["o1"])], **kit_options)
        if groups is not None:
            params["groups"] = copy.deepcopy(groups)
        return params

    # Exports with a kit and returns the output after checking the result against the schema.
    def run_kit(self, **kwargs):
        result = self.export(params=self.with_kit(**kwargs))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result["output"]

    # Returns the mesh objects of a piece and its collision still in the worker's scene, by name.
    def piece_objects(self, mesh):
        return bpy.data.objects[mesh], sorted(o.name for o in bpy.data.objects if o.name.startswith("UCX_" + mesh))

    # The tracked fixture job files are what the helper writes, and they match the job schema.
    def test_fixture_files(self):
        folder = self.path("fixtures")
        write_kit_fixtures(folder)
        for name in KIT_FIXTURES:
            with open(os.path.join(DATA_FOLDER, name + ".json"), "r", encoding="utf-8") as handle:
                job = json.load(handle)
            with open(os.path.join(folder, name + ".json"), "r", encoding="utf-8") as handle:
                self.assertEqual(job, json.load(handle), name)
            self.assertEqual(validate(job, load_schema("blender-job")), [], name)
        self.assert_job_valid(self.with_kit())

    # The kit file holds exactly the pieces and their collision, each at the origin with the extent of its solids.
    def test_kit_file_contents(self):
        output = self.run_kit()
        objects = read_file(self.path("export", ".staging", FILE))
        meshes = ["SM_Kit_" + WALL, "SM_Kit_" + CORNER, "SM_Kit_" + OPENING]
        wanted = set(meshes) | {"UCX_%s_%02d" % (m, n) for m in meshes[:2] for n in range(1)} | {
            "UCX_%s_%02d" % (meshes[2], n) for n in range(3)}
        self.assertEqual(set(objects), wanted)
        self.assertEqual(output["kit"]["file"], FILE)
        for obj in objects.values():
            for row in range(4):
                for column in range(4):
                    self.assertAlmostEqual(obj.matrix_world[row][column], Matrix.Identity(4)[row][column],
                                           delta=1e-5)
        extents = {"SM_Kit_" + WALL: ((0.0, -0.1, 0.0), (0.5, 0.1, HEIGHT)),
                   "SM_Kit_" + CORNER: ((-0.1, -0.1, 0.0), (0.1, 0.1, HEIGHT))}
        for name, (low, high) in extents.items():
            bounds = scene.world_bounds([objects[name]])
            for axis in range(3):
                self.assertAlmostEqual(bounds[0][axis], low[axis], delta=1e-4, msg=name)
                self.assertAlmostEqual(bounds[1][axis], high[axis], delta=1e-4, msg=name)
        door = scene.world_bounds([objects["SM_Kit_" + OPENING]])
        for axis, low, high in ((0, 0.0, 1.0), (2, 0.0, HEIGHT)):
            self.assertAlmostEqual(door[0][axis], low, delta=1e-4)
            self.assertAlmostEqual(door[1][axis], high, delta=1e-4)
        self.assertAlmostEqual(door[1][1], 0.105, delta=1e-4)

    # The faces on the cut lines are flat and not bevelled while the top edges are.
    def test_cuts_flat_and_top_bevelled(self):
        self.run_kit()
        obj, _ = self.piece_objects("SM_Kit_" + WALL)
        middle = sorted({round(v.co.x, 6) for v in obj.data.vertices if 0.0095 < v.co.z < HEIGHT - 0.0095})
        self.assertEqual(middle, [0.0, 0.5], "an upright edge on a cut line is bevelled")
        ys = []
        for polygon in obj.data.polygons:
            if abs(polygon.normal.x) > 0.999:
                plane = {round(obj.data.vertices[i].co.x, 6) for i in polygon.vertices}
                self.assertEqual(plane, {0.0 if polygon.normal.x < 0 else 0.5})
                ys += [obj.data.vertices[i].co.y for i in polygon.vertices]
        self.assertAlmostEqual(min(ys), -0.1, delta=1e-6)
        self.assertAlmostEqual(max(ys), 0.1, delta=1e-6)
        tops = [v for v in obj.data.vertices if HEIGHT - 0.02 < v.co.z < HEIGHT - 1e-6]
        self.assertTrue(tops, "the top edges are not bevelled")
        self.assertTrue(any(abs(abs(v.co.y) - 0.0971) < 1e-3 for v in tops), "no rounded profile")

    # The opening piece is cut and holds the door when it is in fills, and holds none when it is not.
    def test_opening_cut_and_fill(self):
        self.run_kit(pieces=[opening_piece(OPENING, ["o1"]), opening_piece(BARE, [])])
        door, _ = self.piece_objects("SM_Kit_" + OPENING)
        bare, _ = self.piece_objects("SM_Kit_" + BARE)
        solid_volume = 1.0 * 0.2 * HEIGHT - 0.9 * 2.1 * 0.2
        self.assertAlmostEqual(abs(helpers_volume(bare)), solid_volume, delta=0.01)
        door_slots = {m.name for m in door.data.materials}
        bare_slots = {m.name for m in bare.data.materials}
        self.assertTrue({"Door", "Frame", "Handle"} <= door_slots)
        self.assertFalse({"Door", "Frame", "Handle"} & bare_slots)
        self.assertGreater(len(door.data.polygons), len(bare.data.polygons))

    # With fill_openings off no piece gets a door even when it lists one.
    def test_fill_openings_off(self):
        self.run_kit(pieces=[opening_piece(OPENING, ["o1"])], fill_openings=False)
        door, _ = self.piece_objects("SM_Kit_" + OPENING)
        self.assertFalse({"Door", "Frame", "Handle"} & {m.name for m in door.data.materials})

    # Slots are the blend's materials and the UVs follow the kit rules, divided by the material's tile size.
    def test_materials_and_uvs(self):
        entry = next(m for m in self.with_kit()["materials"] if m["name"] == "Living_Wall")
        tile = entry["tile_size"]
        self.run_kit()
        obj, _ = self.piece_objects("SM_Kit_" + WALL)
        self.assertEqual({m.name for m in obj.data.materials}, {"Living_Wall", "Exterior_Wall", "Wall_Top"})
        for material in obj.data.materials:
            self.assertIs(material, bpy.data.materials[material.name])
        uv = obj.data.uv_layers["UVMap"].data
        mesh = obj.data
        checked = 0
        for polygon in mesh.polygons:
            normal = polygon.normal
            for index in polygon.loop_indices:
                co = mesh.vertices[mesh.loops[index].vertex_index].co
                u, v = uv[index].uv
                if abs(normal.y + 1.0) < 1e-4:
                    self.assertAlmostEqual(u * tile["width"], co.x, delta=1e-5)
                    self.assertAlmostEqual(v * tile["height"], co.z, delta=1e-5)
                    checked += 1
                elif abs(normal.y - 1.0) < 1e-4:
                    self.assertAlmostEqual(u * tile["width"], -co.x, delta=1e-5)
                    self.assertAlmostEqual(v * tile["height"], co.z, delta=1e-5)
                    checked += 1
                elif abs(abs(normal.z) - 1.0) < 1e-4:
                    self.assertAlmostEqual(u * tile["width"], co.x, delta=1e-5)
                    self.assertAlmostEqual(v * tile["height"], co.y, delta=1e-5)
                    checked += 1
        self.assertGreater(checked, 12)

    # A slot the blend has no material for gets the neutral look and the materials check says so.
    def test_missing_slot(self):
        piece = wall_piece()
        for ring_edge in piece["wall_solids"][0]["outline"]["edges"]:
            if ring_edge["slot"] == "Exterior_Wall":
                ring_edge["slot"] = "Hall_Wall"
        output = self.run_kit(pieces=[piece])
        answer = statuses(output)["materials"]
        self.assertEqual(answer["status"], "fail")
        self.assertTrue(any("Hall_Wall" in f["message"] for f in answer["findings"]))
        self.assertEqual([f for f in answer["findings"] if "Hall_Wall" not in f["message"]], [])

    # No kit object is in a group file, and the groups check passes with the kit.
    def test_not_in_group_files(self):
        output = self.run_kit(groups=grouped_groups())
        self.assertEqual(statuses(output)["groups"]["status"], "pass")
        for entry in output["files"]:
            objects = read_file(self.path("export", ".staging", entry["file"]))
            self.assertEqual([n for n in objects if "Kit_" in n], [])
        self.assertTrue(os.path.isfile(self.path("export", ".staging", FILE)))

    # The kit output counts what was written and every check passes with the kit.
    def test_output_and_checks(self):
        output = self.run_kit()
        kit = output["kit"]
        self.assertEqual(kit["pieces"], 3)
        self.assertEqual(kit["objects"], 3 + 1 + 1 + 3)
        self.assertGreater(kit["triangles"], 0)
        self.assertEqual([c["id"] for c in output["checks"] if c["status"] == "fail"], [])
        found = statuses(output)
        self.assertEqual(found["kit_file"]["status"], "pass")
        for name in ("naming", "transforms", "collision"):
            self.assertEqual(found[name]["status"], "pass", name)
        self.assertIn("kit_pieces", [s["step"] for s in output["steps"]])
        self.assertIn("write_kit", [s["step"] for s in output["steps"]])

    # The kit file check fails when a piece is missing from the file.
    def test_kit_file_check_fails_for_missing_piece(self):
        self.run_kit()
        kit = self.with_kit()["kit"]
        extra = copy.deepcopy(kit["pieces"][0])
        extra["id"] = "Wall_60x300x20_999999"
        extra["mesh"] = "SM_Kit_" + extra["id"]
        kit["pieces"].append(extra)
        ctx = types.SimpleNamespace(params={"kit": kit}, state={"written": True,
                                                                "kit_path": self.path("export", ".staging", FILE)})
        answer = kit_file.run(ctx)
        self.assertEqual(answer["status"], "fail")
        self.assertIn(extra["mesh"], answer["findings"][0]["message"])
        ctx.state["written"] = False
        self.assertEqual(kit_file.run(ctx)["status"], "skipped")

    # Without a kit nothing changes: no kit output, no kit objects, the same object count.
    def test_without_kit(self):
        plain = self.export(params=self.params(clean=True))["output"]
        self.assertIsNone(plain["kit"])
        self.assertIsNone(bpy.data.collections.get("Kit"))
        self.assertEqual(statuses(plain)["kit_file"]["status"], "skipped")
        from plynth.quality import triangles
        seen = []
        original = triangles.triangles_of
        triangles.triangles_of = lambda obj: seen.append(obj.name) or original(obj)
        self.addCleanup(setattr, triangles, "triangles_of", original)
        with_kit = self.run_kit()
        for key in ("objects", "triangles", "collision_objects", "files"):
            self.assertEqual(with_kit[key], plain[key], key)
        self.assertEqual(statuses(with_kit)["triangles"]["message"], statuses(plain)["triangles"]["message"])
        self.assertIn("SM_Kit_" + WALL, seen, "the kit pieces are still checked")

    # The plain profile makes no kit even when the job has one: no kit output, no kit objects, no kit file.
    def test_plain_makes_no_kit(self):
        params = self.with_kit()
        params["profile"] = "plain"
        result = self.export(params=params)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertIsNone(output["kit"])
        self.assertIsNone(bpy.data.collections.get("Kit"))
        self.assertNotIn("kit_file", statuses(output))
        self.assertFalse(os.path.exists(self.path("export", ".staging", FILE)))

    # A check only run builds and checks the pieces but writes no kit file.
    def test_check_only(self):
        params = self.with_kit()
        params["write"] = False
        output = self.export(params=params)["output"]
        self.assertIsNone(output["kit"])
        self.assertFalse(os.path.exists(self.path("export", ".staging", FILE)))
        self.assertEqual(statuses(output)["kit_file"]["status"], "skipped")
        self.assertIsNotNone(bpy.data.objects.get("SM_Kit_" + WALL))

    # A piece with a cutter that fails is export_failed naming the piece.
    def test_bad_piece_is_export_failed(self):
        piece = opening_piece(OPENING, ["o1"])
        piece["collision"] = [{"outline": [[0.0, 0.0], [1.0, 0.0], [1.0, 0.0]], "bottom": 0.0, "top": 1.0}]
        result = self.export(params=self.with_kit(pieces=[piece]))
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "export_failed")
        self.assertIn(OPENING, result["error"]["message"])

    # No second renderer and no bake operator in the kit code.
    def test_no_second_renderer_no_bake(self):
        word = "cyc" + "les"
        for name in ("exporting/kit_pieces.py", "exporting/write_kit.py", "quality/kit_file.py"):
            with open(os.path.join(helpers.BLENDER_FOLDER, "plynth", name), "r", encoding="utf-8") as handle:
                text = handle.read().lower()
            self.assertNotIn(word, text)
            self.assertNotIn("bake", text)


# Returns the signed volume of a mesh object.
def helpers_volume(obj):
    from plynth import meshes
    return meshes.volume(obj)


if __name__ == "__main__":
    unittest.main()
