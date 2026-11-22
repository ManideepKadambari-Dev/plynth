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
HOUSE_MATERIALS = ("concrete", "oak_planks", "roof_tiles")


# Returns the area of a ring of points, positive when anticlockwise.
def ring_area(ring):
    return meshes.polygon_area([tuple(p) for p in ring["points"]])


# Returns the area of a plain ring of plan points, without its sign.
def plain_area(points):
    return abs(meshes.polygon_area([tuple(p) for p in points]))


# Returns the problems of a materials.json against its schema.
def materials_problems(data):
    return validate(data, load_schema("build-materials"))


TARGET_NAMES = {"walls": "SM_%(floor)s_Walls", "floor": "SM_%(floor)s_%(id)s_Floor",
                "ceiling": "SM_%(floor)s_%(id)s_Ceiling", "thresholds": "SM_%(floor)s_Thresholds",
                "slab": "SM_%(floor)s_Slab", "stair": "SM_%(floor)s_%(id)s_Stair",
                "roof": "SM_%(floor)s_Roof_%(index)02d"}


# Returns the UCX object names the job's collision lists ask for, in order.
def expected_collision_names(floor):
    names = []
    for entry in floor["collision"]:
        target = entry["target"]
        base = TARGET_NAMES[target["kind"]] % {"floor": floor["id"], "id": target["id"], "index": entry["index"]}
        names += ["UCX_%s_%02d" % (base, n) for n in range(len(entry["pieces"]) + len(entry["hulls"]))]
    return names


# Checks the slot names of a job against MaterialSlots.h: every field names a listed slot of the right surface.
def check_slots(test, floor, slots):
    surface = {s["name"]: s["surface"] for s in slots}
    test.assertEqual([s["name"] for s in slots], sorted(surface))
    for room in floor["rooms"]:
        label = room["type"].capitalize()
        test.assertEqual(room["floor_slot"], label + "_Floor")
        test.assertEqual(room["ceiling_slot"], label + "_Ceiling")
        test.assertEqual(surface[room["floor_slot"]], "Floor")
        test.assertEqual(surface[room["ceiling_slot"]], "Ceiling")
    for solid in floor["wall_solids"]:
        test.assertEqual(solid["top_slot"], "Wall_Top")
        for ring in [solid["outline"]] + solid["holes"]:
            for edge in ring["edges"]:
                if edge["side"] is None:
                    continue
                if edge["room"] is None:
                    test.assertEqual(edge["slot"], "Exterior_Wall")
                else:
                    label = next(r["type"] for r in floor["rooms"] if r["id"] == edge["room"]).capitalize()
                    test.assertEqual(edge["slot"], label + "_Wall")
    for opening in floor["openings"]:
        for slot in opening["slots"].values():
            test.assertIn(slot, surface)
    check_part_slots(test, floor, surface)


# Checks the slot names of the slab, stairs and roof faces of a floor against MaterialSlots.h.
def check_part_slots(test, floor, surface):
    slab = floor["slab"]["slots"]
    for part, base, look in (("edge", "Exterior_Wall", "Wall_Outside"), ("well", "Stair_Well", "Wall_Inside"),
                             ("underside", "Soffit", "Soffit")):
        test.assertTrue(slab[part].startswith(base), slab[part])
        test.assertEqual(surface[slab[part]], look)
    for stair in floor["stairs"]:
        test.assertTrue(stair["slot"].startswith("Stair"))
        test.assertEqual(surface[stair["slot"]], "Stair")
    looks = {"underside": "Soffit", "edge": "Fascia", "gable": "Wall_Outside", "parapet": "Wall_Outside",
             "glazing": "Glass"}
    for roof in floor["roofs"]:
        for face in roof["faces"]:
            if face["part"] == "covering":
                test.assertEqual(surface[face["slot"]], "Flat_Roof" if roof["type"] == "flat" else "Roof")
            else:
                test.assertEqual(surface[face["slot"]], looks[face["part"]])


# Returns true when every vertex of the object lies on the inner side of every face plane.
def is_convex(obj):
    vertices = [v.co for v in obj.data.vertices]
    for polygon in obj.data.polygons:
        origin = polygon.center
        if any((v - origin).dot(polygon.normal) > 1e-5 for v in vertices):
            return False
    return True


# Checks that the built file has one closed UCX object for each collision piece the job lists and no others.
def check_collision(test, floor, counts):
    wanted = expected_collision_names(floor)
    found = sorted(o.name for o in bpy.data.objects if o.name.startswith("UCX_"))
    test.assertEqual(found, sorted(wanted))
    test.assertEqual(counts["collision"], len(wanted))
    walls = len(floor["collision"][0]["pieces"])
    test.assertEqual(len([n for n in wanted if n.startswith("UCX_SM_%s_Walls_" % floor["id"])]), walls)
    for name in wanted:
        test.assertTrue(meshes.is_manifold(bpy.data.objects[name]), name)
        test.assertTrue(is_convex(bpy.data.objects[name]), name)


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
                         ["building.blend", "building.fbx", "materials.json", "preview", "textures"])

    # The slab's ledges are built as upward faces at the top, with the area core gave them, and every side face
    # takes its side's material slot.
    def test_slab_tops(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        slab = self.params["floors"][0]["slab"]
        wanted = sum(plain_area(t["outline"]) - sum(plain_area(h) for h in t["holes"]) for t in slab["tops"])
        self.assertGreater(len(slab["tops"]), 0)
        obj = bpy.data.objects["SM_%s_Slab" % self.params["floors"][0]["id"]]
        ups = [p for p in obj.data.polygons if p.normal.z > 0.5]
        self.assertAlmostEqual(sum(p.area for p in ups), wanted, delta=1e-6)
        self.assertTrue(all(abs(obj.data.vertices[i].co.z - slab["top"]) < 1e-5 for p in ups for i in p.vertices))
        sides = [p for p in obj.data.polygons if abs(p.normal.z) < 1e-3]
        self.assertEqual(len(sides), sum(len(e["outline"]) + sum(len(h) for h in e["holes"]) for e in slab["sides"]))

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

    # The slot names are the ones MaterialSlots.h gives, and the door on w7 takes its reveal from the right side room.
    def test_slots(self):
        floor = self.params["floors"][0]
        check_slots(self, floor, self.params["slots"])
        o7 = next(o for o in floor["openings"] if o["id"] == "o7")
        self.assertEqual(o7["slots"]["reveal"], "Living_Wall")
        self.assertEqual(o7["slots"]["threshold"], "Living_Floor")

    # One UCX object for each wall piece, room slab, slab piece and roof hull, and nothing else.
    def test_collision(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertEqual(len(self.params["floors"][0]["collision"]), 8)
        check_collision(self, self.params["floors"][0], self.result["output"]["counts"])

    # With the default bevel the build still has no problems and the walls stay closed.
    def test_with_bevel(self):
        params = copy.deepcopy(load_params("from_core"))
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["problems"], [])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertTrue(meshes.is_manifold(bpy.data.objects["SM_f1_Walls"]))


class LibraryBuildTest(material_helpers.MaterialTest, BuildTest):
    # Cleans sample materials into a scratch library the way the catalog stores them and returns its folder.
    def make_library(self, names):
        library = self.path("library")
        now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        for material_id in names:
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

    # Returns the params of a job fixture with its project folder and library folder filled in.
    def make_params(self, name, materials):
        library = self.make_library(materials)
        path = os.path.join(helpers_data(), name + ".json")
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read().replace("@LIBRARY@", library).replace("@PROJECT@", self.path("project"))
        return json.loads(text)["params"]


class FromCoreMaterialsTests(LibraryBuildTest):
    # Builds the job, which uses white paint inside, red brick outside and grey tile floors, with the bevel off.
    def setUp(self):
        super().setUp()
        self.params = self.make_params("from_core_materials", LIBRARY_MATERIALS)
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

    # Same slot names, door slots and UCX objects as the plain job.
    def test_slots_and_collision(self):
        floor = self.params["floors"][0]
        check_slots(self, floor, self.params["slots"])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        check_collision(self, floor, self.result["output"]["counts"])

    # materials.json validates and names the three library materials.
    def test_materials_file(self):
        data = self.read_materials()
        self.assertEqual(materials_problems(data), [])
        shown = {m["material"] for m in data["materials"]}
        self.assertTrue(set(LIBRARY_MATERIALS) <= shown, shown)

    # Reads the materials.json the build wrote.
    def read_materials(self):
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            return json.load(handle)


class FromCoreHouseTests(LibraryBuildTest):
    # Builds the two floor house core made (a stair, a gable roof on the upper floor, flat roofs below). Only the
    # roof, floor and stair materials are in the scratch library.
    def setUp(self):
        super().setUp()
        self.params = self.make_params("from_core_house", HOUSE_MATERIALS)
        self.result = self.build(self.params)
        self.floors = self.params["floors"]

    # The build succeeds and has no problems but the materials missing from the scratch library, which fail to load.
    def test_build(self):
        self.assertTrue(self.result["ok"], self.result["error"])
        self.assert_result_valid(self.result)
        output = self.result["output"]
        self.assertEqual([p for p in output["problems"] if p["code"] != "material_failed"], [])
        missing = {p["message"].split()[2] for p in output["problems"]}
        used = {m["id"] for m in self.params["materials"]}
        self.assertEqual(missing, used - set(HOUSE_MATERIALS))
        counts = output["counts"]
        self.assertEqual(counts["slabs"], len(self.floors))
        self.assertEqual(counts["stairs"], sum(len(f["stairs"]) for f in self.floors))
        self.assertEqual(counts["roofs"], sum(len(f["roofs"]) for f in self.floors))
        self.assertEqual(counts["rooms"], sum(len(f["rooms"]) for f in self.floors))
        self.assertEqual(counts["wall_solids"], sum(len(f["wall_solids"]) for f in self.floors))

    # Every room's floor and ceiling area plus its hole areas is the area core gave the room. The stair of the house
    # clears the walls, so no room is notched by it and a room without a hole keeps its whole area.
    def test_room_areas(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        holed = 0
        for floor in self.floors:
            for room in floor["rooms"]:
                for kind, key in (("Floor", "polygons"), ("Ceiling", "ceiling_polygons")):
                    obj = bpy.data.objects["SM_%s_%s_%s" % (floor["id"], room["id"], kind)]
                    polygons = room[key]
                    holes = sum(plain_area(h) for polygon in polygons for h in polygon["holes"])
                    outlines = sum(plain_area(polygon["outline"]) for polygon in polygons)
                    self.assertAlmostEqual(meshes.area(obj), outlines - holes, delta=1e-3, msg=obj.name)
                    if holes > 0:
                        holed += 1
                        self.assertAlmostEqual(meshes.area(obj) + holes, room["area"], delta=1e-3, msg=obj.name)
                    else:
                        self.assertAlmostEqual(meshes.area(obj), room["area"], delta=1e-3, msg=obj.name)
        self.assertGreater(holed, 0)

    # Each floor has one slab, its top where core put it and its footprint the area of the job's polygons less the
    # holes (seen from above, the top face is never built, so the bottom faces carry the area).
    def test_slabs(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        slabs = [o for o in bpy.data.objects if o.get("plynth_kind") == "slab"]
        self.assertEqual(sorted(o.name for o in slabs), sorted("SM_%s_Slab" % f["id"] for f in self.floors))
        for floor in self.floors:
            obj = bpy.data.objects["SM_%s_Slab" % floor["id"]]
            top = max(v.co.z for v in obj.data.vertices)
            self.assertAlmostEqual(top, floor["slab"]["top"], delta=1e-4)
            bottom = min(v.co.z for v in obj.data.vertices)
            self.assertAlmostEqual(bottom, floor["slab"]["bottom"], delta=1e-4)

    # The stair is one object on its floor and its top step is at the height core gave it.
    def test_stair(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        for floor in self.floors:
            for stair in floor["stairs"]:
                obj = bpy.data.objects["SM_%s_%s_Stair" % (floor["id"], stair["id"])]
                top = max(v.co.z for v in obj.data.vertices)
                self.assertAlmostEqual(top, floor["elevation"] + stair["steps"][-1]["top"], delta=1e-4)

    # The slab, stair and roof slots of the house are the ones MaterialSlots.h gives, and each is in the table.
    def test_part_slots(self):
        surface = {s["name"]: s["surface"] for s in self.params["slots"]}
        for floor in self.floors:
            check_part_slots(self, floor, surface)

    # Every slab, stair and roof of the house has closed convex UCX objects, numbered pieces first and then hulls, and
    # the roof hulls are the ones core listed.
    def test_part_collision(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        counts = self.result["output"]["counts"]
        wanted = [name for floor in self.floors for name in expected_collision_names(floor)]
        found = sorted(o.name for o in bpy.data.objects if o.name.startswith("UCX_"))
        self.assertEqual(found, sorted(wanted))
        self.assertEqual(counts["collision"], len(wanted))
        for floor in self.floors:
            for entry in floor["collision"]:
                if entry["target"]["kind"] not in ("slab", "stair", "roof"):
                    continue
                self.assertGreater(len(entry["pieces"]) + len(entry["hulls"]), 0)
        for name in wanted:
            obj = bpy.data.objects[name]
            self.assertTrue(meshes.is_manifold(obj), name)
            self.assertTrue(is_convex(obj), name)
            self.assertEqual(obj["plynth_kind"], "collision")
        for kind in ("slab", "stair", "roof"):
            targets = [o for o in bpy.data.objects if o.get("plynth_kind") == kind]
            self.assertTrue(targets, kind)
            for target in targets:
                self.assertTrue([o for o in bpy.data.objects if o.get("plynth_collision_for") == target.name], target.name)

    # Every roof section has an object on its floor, and the gable and the flat roofs take their job's materials.
    def test_roofs(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        for floor in self.floors:
            for roof in floor["roofs"]:
                obj = bpy.data.objects["SM_%s_Roof_%02d" % (floor["id"], roof["index"])]
                self.assertEqual(obj["plynth_roof_index"], roof["index"])
                self.assertEqual([c.name for c in obj.users_collection], [floor["id"]])
        shown = {s["name"]: s["material"] for s in self.params["slots"]}
        self.assertEqual(shown["Roof"], "roof_tiles")
        self.assertEqual(shown["Flat_Roof"], "concrete")
        self.assertIn("Roof", [m.name for m in bpy.data.objects["SM_f2_Roof_00"].data.materials])
        self.assertIn("Flat_Roof", [m.name for m in bpy.data.objects["SM_f1_Roof_00"].data.materials])

    # The preview parts are written, one for each floor and one for each roof.
    def test_preview(self):
        names = sorted(os.listdir(self.path("build", "preview")))
        for floor in self.floors:
            self.assertIn("%s.fbx" % floor["id"], names)
        self.assertIn("parts.json", names)

    # materials.json validates and shows the roof, floor and stair materials.
    def test_materials_file(self):
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(materials_problems(data), [])
        shown = {m["material"] for m in data["materials"]}
        self.assertTrue(set(HOUSE_MATERIALS) <= shown, shown)


# Returns the folder of the job fixtures.
def helpers_data():
    from build_helpers import DATA_FOLDER
    return DATA_FOLDER


if __name__ == "__main__":
    unittest.main()
