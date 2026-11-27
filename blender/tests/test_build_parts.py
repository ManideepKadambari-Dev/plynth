import copy
import json
import math
import os
import unittest

import bpy
from mathutils import Vector

from build_helpers import BuildTest, load_params
from plynth import meshes
from plynth.exporting import groups as groups_module
from schema_check import load_schema, validate

BOX = "f1_bx1_Box"
HOUSING = "f1_ls1_LightStrip"
BEAM = "f1_bm1_Beam"
LAWN = "Site_Lawn"
STRIP_LIGHT = "Light_ls1_00"
PART_SLOTS = [{"name": "Box_Top", "material": None, "surface": "Deck"},
              {"name": "Box_Side", "material": None, "surface": "Foundation"},
              {"name": "Light_Strip", "material": None, "surface": "Light_Strip"},
              {"name": "Ground", "material": None, "surface": "Ground"}]


# Returns a square outline in X and Y, anticlockwise from its low corner.
def square(x, y, size):
    return [[x, y], [x + size, y], [x + size, y + size], [x, y + size]]


# Returns a part of the job with no prisms, faces, sweeps or collision yet.
def part(obj, kind, plan_id, bevel=True, uv="world"):
    return {"object": obj, "plynth_kind": kind, "plynth_id": plan_id, "bevel": bevel, "uv": uv, "prisms": [],
            "faces": [], "sweeps": [], "collision": [], "hulls": []}


# Returns a prism of the job.
def prism(outline, bottom, top, slot, side_slot=None):
    return {"outline": outline, "holes": [], "bottom": bottom, "top": top, "slot": slot, "side_slot": side_slot}


# The box part: one metre square and half a metre high outside the room, with its own top and side slots and a
# collision piece the size of the box.
def box_part():
    made = part(BOX, "test_box", "bx1")
    made["prisms"] = [prism(square(6.0, -2.0, 1.0), 0.0, 0.5, "Box_Top", "Box_Side")]
    made["collision"] = [{"outline": square(6.0, -2.0, 1.0), "bottom": 0.0, "top": 0.5}]
    return made


# The housing of the strip light: a thin bar under the ceiling with the Light_Strip slot and no collision.
def housing_part():
    made = part(HOUSING, "light_strip", "ls1", bevel=False)
    outline = [[0.5, -0.51], [3.5, -0.51], [3.5, -0.49], [0.5, -0.49]]
    made["prisms"] = [prism(outline, 2.95, 2.96, "Light_Strip")]
    return made


# A beam two metres long at 45 degrees with UVs along it.
def beam_part():
    c = math.sqrt(0.5)
    x, y = 8.0, 2.0
    outline = [[x, y], [x + 2 * c, y + 2 * c], [x + 2 * c - 0.1 * c, y + 2 * c + 0.1 * c], [x - 0.1 * c, y + 0.1 * c]]
    made = part(BEAM, "test_beam", "bm1", bevel=False, uv="along")
    made["prisms"] = [prism(outline, 0.0, 0.1, "Box_Top")]
    return made


# The lawn of the site: one face just under the ground floor with a thin collision piece under it.
def lawn_part():
    z = -0.02
    made = part(LAWN, "ground", "ss1")
    made["faces"] = [{"outline": [[-5.0, -10.0, z], [12.0, -10.0, z], [12.0, 6.0, z], [-5.0, 6.0, z]],
                      "holes": [], "slot": "Ground"}]
    made["collision"] = [{"outline": [[-5.0, -10.0], [12.0, -10.0], [12.0, 6.0], [-5.0, 6.0]], "bottom": z - 0.1,
                          "top": z}]
    return made


# One straight piece of a light strip along X under the ceiling, shining down.
def strip_light():
    return {"id": "ls1", "start": [0.5, -0.5, 2.95], "end": [3.5, -0.5, 2.95], "facing": [0.0, 0.0, -1.0],
            "width": 0.02, "power": 12.0, "colour": [1.0, 0.9, 0.8]}


# Returns the square room job with the parts, the strip light, the site lawn and their slots.
def parts_params():
    params = copy.deepcopy(load_params("square_room"))
    params["slots"] = params["slots"] + copy.deepcopy(PART_SLOTS)
    floor = params["floors"][0]
    floor["parts"] = [box_part(), housing_part(), beam_part()]
    floor["strip_lights"] = [strip_light()]
    params["site_parts"] = [lawn_part()]
    return params


# Returns the groups of a grouped export where the box kind and the lights go to surfaces and the rest to
# structure.
def part_groups():
    return [
        {"id": "structure", "file": "Square_Walls.fbx", "kinds": [], "ids": [], "name": "Walls", "rest": True,
         "members": [{"kind": "walls", "id": "f1"}]},
        {"id": "surfaces", "file": "Square_Floors.fbx", "kinds": [], "ids": [], "name": "Floors",
         "members": [{"kind": "test_box", "id": None}, {"kind": "light", "id": None}]},
    ]


class BuildPartsTests(BuildTest):
    # Builds the job with the parts once for the checks.
    def setUp(self):
        super().setUp()
        self.params = parts_params()
        self.result = self.build(self.params, preview_folder="preview")
        self.assertTrue(self.result["ok"], self.result.get("error"))

    # Reads materials.json of the build.
    def materials_file(self):
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            return json.load(handle)

    # The parts in the job match the parts schema, the result is valid and the parts step ran after the roofs and
    # before the materials, with no problem.
    def test_job_and_result(self):
        floor = self.params["floors"][0]
        job = {"parts": floor["parts"], "strip_lights": floor["strip_lights"], "site_parts": self.params["site_parts"]}
        self.assertEqual(validate(job, load_schema("blender-job-parts")), [])
        self.assert_result_valid(self.result)
        steps = [s["step"] for s in self.result["output"]["steps"]]
        self.assertLess(steps.index("roofs"), steps.index("parts"))
        self.assertLess(steps.index("parts"), steps.index("materials"))
        self.assertEqual(self.result["output"]["problems"], [])

    # The box is one closed object in its floor's collection, with its kind, id and two slots, the top in the first
    # and the sides in the second, and rounded edges that keep its volume.
    def test_box(self):
        box = bpy.data.objects[BOX]
        self.assertEqual((box["plynth_kind"], box["plynth_id"]), ("test_box", "bx1"))
        self.assertEqual([c.name for c in box.users_collection], ["f1"])
        self.assertEqual([m.name for m in box.data.materials], ["Box_Top", "Box_Side"])
        self.assertTrue(meshes.is_manifold(box))
        self.assertAlmostEqual(meshes.volume(box), 0.5, delta=0.01)
        top = [p for p in box.data.polygons if p.normal.z > 0.99 and p.center.z > 0.499]
        sides = [p for p in box.data.polygons if abs(p.normal.z) < 0.01 and p.area > 0.1]
        self.assertTrue(top and sides)
        self.assertEqual({p.material_index for p in top}, {0})
        self.assertEqual({p.material_index for p in sides}, {1})
        self.assertGreater(len(box.data.vertices), 8)

    # The box's collision is one closed prism named after it, in the Collision collection, and the housing and the
    # beam, which list none, have none.
    def test_collision(self):
        hull = bpy.data.objects[BOX + "_Collision_00"]
        self.assertEqual(hull["plynth_kind"], "collision")
        self.assertEqual(hull["plynth_collision_for"], BOX)
        self.assertEqual(hull["plynth_id"], "bx1")
        self.assertEqual([c.name for c in hull.users_collection], ["Collision"])
        self.assertTrue(meshes.is_manifold(hull))
        self.assertAlmostEqual(meshes.volume(hull), 0.5, places=4)
        self.assertNotIn(BOX + "_Collision_01", bpy.data.objects)
        for name in (HOUSING, BEAM):
            self.assertFalse([o for o in bpy.data.objects if o.get("plynth_collision_for") == name], name)

    # A group that takes the box's kind gets the box, its collision and the strip light; the walls go to the rest.
    def test_group(self):
        objects = list(bpy.data.objects)
        built = [o for o in objects if o.type in ("MESH", "LIGHT") and o.get("plynth_kind") not in (None, "collision")]
        collision = [o for o in objects if o.get("plynth_kind") == "collision"]
        found, _ = groups_module.assign(part_groups(), built, collision)
        surfaces = [o.name for o in found["surfaces"]]
        self.assertIn(BOX, surfaces)
        self.assertIn(BOX + "_Collision_00", surfaces)
        self.assertIn(STRIP_LIGHT, surfaces)
        self.assertIn("f1_Walls", [o.name for o in found["structure"]])
        self.assertIn(HOUSING, [o.name for o in found["structure"]])

    # The strip light is an area light as long as the piece and as wide as the strip, in its middle, shining down
    # with the power and colour of the job, and stays out of building.fbx.
    def test_strip_light(self):
        light = bpy.data.objects[STRIP_LIGHT]
        self.assertEqual(light.type, "LIGHT")
        self.assertEqual((light.data.type, light.data.shape), ("AREA", "RECTANGLE"))
        self.assertAlmostEqual(light.data.size, 3.0, places=5)
        self.assertAlmostEqual(light.data.size_y, 0.02, places=6)
        self.assertAlmostEqual(light.data.energy, 12.0, places=5)
        self.assertEqual((light["plynth_kind"], light["plynth_id"], light["plynth_light"]), ("light", "ls1", "strip"))
        self.assertEqual([c.name for c in light.users_collection], ["f1"])
        for got, want in zip(light.matrix_world.translation, (2.0, -0.5, 2.95)):
            self.assertAlmostEqual(got, want, places=5)
        rotation = light.matrix_world.to_3x3()
        self.assertLess(((rotation @ Vector((0.0, 0.0, -1.0))) - Vector((0.0, 0.0, -1.0))).length, 1e-5)
        self.assertLess(((rotation @ Vector((1.0, 0.0, 0.0))) - Vector((1.0, 0.0, 0.0))).length, 1e-5)
        self.assertEqual(self.result["output"]["counts"].get("lights"), 1)
        self.assertNotIn(STRIP_LIGHT, [o["name"] for o in self.materials_file()["objects"]])

    # The housing's slot shows the neutral Light_Strip look, and the other part slots their own looks.
    def test_neutral_looks(self):
        written = {m["name"]: m for m in self.materials_file()["materials"]}
        for name, surface in (("Light_Strip", "Light_Strip"), ("Box_Top", "Deck"), ("Box_Side", "Foundation"),
                              ("Ground", "Ground")):
            self.assertEqual(written[name]["surface"], surface, name)
        housing = bpy.data.objects[HOUSING]
        self.assertEqual([m.name for m in housing.data.materials], ["Light_Strip"])
        self.assertEqual(housing["plynth_kind"], "light_strip")

    # The along UVs of the beam run along its length: its top spans two metres in u and its width in v.
    def test_along_uvs(self):
        beam = bpy.data.objects[BEAM]
        layer = beam.data.uv_layers["UVMap"].data
        us = []
        vs = []
        for polygon in beam.data.polygons:
            if polygon.normal.z > 0.99:
                for index in polygon.loop_indices:
                    us.append(layer[index].uv.x)
                    vs.append(layer[index].uv.y)
        self.assertAlmostEqual(max(us) - min(us), 2.0, places=4)
        self.assertAlmostEqual(max(vs) - min(vs), 0.1, places=4)

    # The lawn is in the Site collection with its collision, and the preview has a site file listed under the ground
    # floor that holds the lawn only.
    def test_site(self):
        lawn = bpy.data.objects[LAWN]
        self.assertEqual([c.name for c in lawn.users_collection], ["Site"])
        self.assertEqual((lawn["plynth_kind"], lawn["plynth_id"]), ("ground", "ss1"))
        self.assertEqual([m.name for m in lawn.data.materials], ["Ground"])
        self.assertIn(LAWN + "_Collision_00", bpy.data.objects)
        with open(self.path("build", "preview", "parts.json"), "r", encoding="utf-8") as handle:
            listing = json.load(handle)
        self.assertEqual(listing["parts"][-1], {"floor": "f1", "kind": "site", "file": "site.fbx"})
        self.assertNotIn("site.fbx", [p["file"] for p in listing["parts"][:-1]])
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.ops.import_scene.fbx(filepath=self.path("build", "preview", "site.fbx"))
        self.assertEqual([o.name for o in bpy.context.scene.objects if o.type == "MESH"], [LAWN])


class BuildPartsProblemTests(BuildTest):
    # A part with a prism of no height gives part_failed and no object or collision, and the other parts are built.
    def test_bad_part_is_a_warning(self):
        params = parts_params()
        bad = box_part()
        bad["object"] = "f1_bx2_Box"
        bad["plynth_id"] = "bx2"
        bad["prisms"][0]["top"] = 0.0
        params["floors"][0]["parts"].append(bad)
        result = self.build(params)
        self.assertTrue(result["ok"], result.get("error"))
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["object"], p["step"]) for p in problems], [("part_failed", "bx2", "parts")])
        self.assertNotIn("f1_bx2_Box", bpy.data.objects)
        self.assertNotIn("f1_bx2_Box_Collision_00", bpy.data.objects)
        self.assertIn(BOX, bpy.data.objects)

    # A job without parts, strip lights or site parts makes no Site collection and no site preview file.
    def test_job_without_parts(self):
        result = self.build(load_params("square_room"), preview_folder="preview")
        self.assertTrue(result["ok"], result.get("error"))
        self.assertNotIn("Site", bpy.data.collections)
        self.assertNotIn("site.fbx", os.listdir(self.path("build", "preview")))


if __name__ == "__main__":
    unittest.main()
