import copy
import os
import unittest

import bpy
from mathutils import Matrix

import export_helpers
from export_helpers import ExportTest, grouped_groups, groups_with_empty
from plynth import scene
from plynth.exporting import groups as groups_module
from plynth.quality import collision
from schema_check import load_schema, validate

SURFACES = {"f1_r1_Floor", "f1_r1_Ceiling", "f1_Thresholds"}
STEPS = ["open_blend", "prepare", "name_objects", "kit_pieces", "textures", "checks", "finish_objects", "write_fbx",
         "write_kit", "file_checks"]


# Reads an FBX file into an empty scene and returns the sorted names of its mesh objects and the objects.
def read_file(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    objects = [o for o in bpy.data.objects if o.type == "MESH"]
    return sorted(o.name for o in objects), objects


# Returns the checks of an output by id.
def statuses(output):
    return {c["id"]: c for c in output["checks"]}


class FakeObject:
    # A stand in for a Blender object: a name and custom properties.
    def __init__(self, name, kind, plan_id, collision_for=None):
        self.name = name
        self.properties = {"plynth_kind": kind, "plynth_id": plan_id}
        if collision_for:
            self.properties["plynth_collision_for"] = collision_for

    # Reads a custom property like an object does.
    def get(self, key, default=None):
        return self.properties.get(key, default)


class ExportGroupsTests(ExportTest):
    # Returns the params of the shared project with the given groups.
    def grouped(self, groups=None, **kwargs):
        params = self.params(clean=True, **kwargs)
        params["groups"] = copy.deepcopy(groups if groups is not None else grouped_groups())
        return params

    # Exports with the given groups and returns the output after checking the result against the schema.
    def run_groups(self, groups=None, **kwargs):
        result = self.export(params=self.grouped(groups, **kwargs))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result["output"]

    # Returns the written file entries of an output by group id.
    def written(self, output):
        return {f["group"]: f for f in output["files"]}

    # Reads one written file of this test's staging folder.
    def read(self, name):
        return read_file(self.path("export", ".staging", name))

    # The fixture job files are the ones the helper writes, and they match the job schema.
    def test_fixture_files(self):
        for name, groups in (("grouped_three", grouped_groups()), ("grouped_empty_group", groups_with_empty())):
            job = export_helpers.load_job(name)
            self.assertEqual(validate(job, load_schema("blender-job")), [], name)
            self.assertEqual(job["params"]["groups"], groups)
        self.assert_job_valid(self.grouped())

    # Three groups write three files and each file holds exactly its objects, with no collision.
    def test_three_files(self):
        output = self.run_groups()
        files = self.written(output)
        self.assertEqual(sorted(files), ["furniture_r1", "structure", "surfaces"])
        self.assertEqual(output["empty_groups"], [])
        self.assertIsNone(output["kit"])
        self.assertEqual([c["id"] for c in output["checks"] if c["status"] == "fail"], [])
        self.assertEqual(statuses(output)["groups"]["status"], "pass")
        self.assertTrue(output["written"])
        staging = self.path("export", ".staging")
        self.assertEqual(sorted(f for f in self.files_under(staging) if f.endswith(".fbx")),
                         ["Square_Floors.fbx", "Square_Furniture_Living.fbx", "Square_Walls.fbx"])
        names, _ = self.read("Square_Furniture_Living.fbx")
        self.assertEqual(names, ["sofa_i1", "sofa_i2"])
        self.assertEqual(files["furniture_r1"]["objects"], 2)
        names, _ = self.read("Square_Floors.fbx")
        self.assertEqual(set(names), SURFACES)
        names, _ = self.read("Square_Walls.fbx")
        self.assertIn("f1_Walls", names)
        self.assertEqual([n for n in names if n in SURFACES or n.startswith("sofa")], [])
        self.assertEqual([n for n in names if n.startswith(("SM_", "UCX_"))], [])

    # The unreal profile joins each of the three groups into one mesh named after its file, SM_Square_<Group>,
    # with the collision of its objects as UCX_ meshes, and every check passes.
    def test_three_files_unreal(self):
        output = self.run_groups(profile="unreal")
        files = self.written(output)
        self.assertEqual(sorted(files), ["furniture_r1", "structure", "surfaces"])
        self.assertEqual([c["id"] for c in output["checks"] if c["status"] in ("fail", "warning")], [])
        self.assertEqual(statuses(output)["fbx_scale"]["status"], "pass")
        names, _ = self.read("Square_Furniture_Living.fbx")
        self.assertEqual(names, ["SM_Square_Furniture_Living", "UCX_SM_Square_Furniture_Living_00",
                                 "UCX_SM_Square_Furniture_Living_01"])
        total = 0
        for group, mesh in (("surfaces", "SM_Square_Floors"), ("structure", "SM_Square_Walls")):
            names, _ = self.read(files[group]["file"])
            self.assertEqual([n for n in names if not n.startswith("UCX_")], [mesh], group)
            hulls = [n for n in names if n.startswith("UCX_")]
            self.assertTrue(hulls, group)
            self.assertEqual(hulls, ["UCX_%s_%02d" % (mesh, n) for n in range(len(hulls))], group)
            self.assertEqual(files[group]["objects"], len(names), group)
            total += len(hulls)
        self.assertEqual(output["collision_objects"], total + 2)
        self.assertEqual(output["objects"], 3 + output["collision_objects"])

    # Every object of the build is in exactly one written file, and every file keeps identity transforms.
    def test_every_object_once_with_identity(self):
        output = self.run_groups()
        seen = []
        for entry in output["files"]:
            names, objects = self.read(entry["file"])
            seen += names
            for obj in objects:
                for row in range(4):
                    for column in range(4):
                        self.assertAlmostEqual(obj.matrix_world[row][column], Matrix.Identity(4)[row][column],
                                               delta=1e-5)
        self.assertEqual(len(seen), len(set(seen)))
        self.assertEqual(len(seen), output["objects"])

    # The walls in the structure file have the bounds of the single export.
    def test_walls_bounds_match_single(self):
        single = self.export(params=self.params(clean=True))["output"]
        self.assertTrue(single["written"])
        _, objects = self.read("Square.fbx")
        wanted = scene.world_bounds([o for o in objects if o.name == "f1_Walls"])
        self.run_groups()
        _, objects = self.read("Square_Walls.fbx")
        got = scene.world_bounds([o for o in objects if o.name == "f1_Walls"])
        for side in range(2):
            for axis in range(3):
                self.assertAlmostEqual(got[side][axis], wanted[side][axis], delta=1e-5)

    # An object added by hand to the blend goes to the group with rest, and an SM_ the user gave it is dropped.
    def test_hand_made_cube_goes_to_rest(self):
        plain = self.written(self.run_groups())["structure"]["objects"]
        project = export_helpers.fixture_project(clean=True)
        blend = self.path("hand.blend")
        bpy.ops.wm.open_mainfile(filepath=project + "/build/building.blend")
        bpy.ops.mesh.primitive_cube_add(size=0.5, location=(2.0, -2.0, 0.25))
        bpy.context.object.name = "SM_hand_cube"
        bpy.context.object.data.name = "SM_hand_cube"
        bpy.context.object.data.materials.append(bpy.data.materials["Living_Wall"])
        bpy.ops.wm.save_as_mainfile(filepath=blend, copy=True)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        output = self.run_groups(blend_file=blend)
        entry = self.written(output)["structure"]
        self.assertEqual(entry["objects"], plain + 1)
        names, _ = self.read(entry["file"])
        self.assertIn("hand_cube", names)
        self.assertNotIn("SM_hand_cube", names)

    # An exact member beats the kind filter of a group without members.
    def test_exact_member_beats_filter(self):
        plain = self.written(self.run_groups())["structure"]["objects"]
        groups = grouped_groups()
        groups[0]["members"].append({"kind": "item", "id": "i2"})
        groups[2] = {"id": "furniture_r1", "file": "Square_Furniture_Living.fbx", "kinds": ["item"], "ids": [],
                     "name": "Furniture_Living"}
        output = self.run_groups(groups)
        files = self.written(output)
        self.assertEqual(files["furniture_r1"]["objects"], 1)
        self.assertEqual(files["structure"]["objects"], plain + 1)
        names, _ = self.read("Square_Furniture_Living.fbx")
        self.assertIn("sofa_i1", names)
        self.assertNotIn("sofa_i2", names)

    # The assignment order of Exporter.h on stand in objects, with collision following its object.
    def test_assignment_order(self):
        found, left = groups_module.assign(
            [{"id": "a", "members": [{"kind": "item", "id": "i1"}]},
             {"id": "b", "members": [{"kind": "item", "id": None}]},
             {"id": "c", "kinds": ["walls"], "ids": []},
             {"id": "d", "kinds": [], "ids": [], "members": [], "rest": True}],
            [FakeObject("m1", "item", "i1"), FakeObject("m2", "item", "i2"), FakeObject("m3", "walls", "f1"),
             FakeObject("m4", "floor", "r1")],
            [FakeObject("UCX_m2_00", "collision", "i2", "m2"), FakeObject("UCX_m3_00", "collision", "f1", "m3")])
        self.assertEqual({k: [o.name for o in v] for k, v in found.items()},
                         {"a": ["m1"], "b": ["UCX_m2_00", "m2"], "c": ["UCX_m3_00", "m3"], "d": ["m4"]})
        self.assertEqual(left, [])

    # Without a rest group, objects nobody takes come back as unassigned, and kit objects are ignored.
    def test_unassigned_and_kit(self):
        found, left = groups_module.assign([{"id": "a", "kinds": ["walls"], "ids": []}],
                                           [FakeObject("k", "kit", "Wall_1"), FakeObject("m", "walls", "f1"),
                                            FakeObject("n", "floor", "r1")])
        self.assertEqual([o.name for o in found["a"]], ["m"])
        self.assertEqual([o.name for o in left], ["n"])

    # With a rest group, a group with no members and no filters takes nothing, so an unknown object and a
    # collision mesh of no object land in rest. Without a rest group the same group takes everything, as in a
    # single export.
    def test_empty_group_takes_nothing_beside_rest(self):
        objects = [FakeObject("m1", "item", "i1"), FakeObject("hand", "cube", None)]
        orphan = [FakeObject("UCX_gone_00", "collision", "x", "gone")]
        with_rest = [{"id": "a", "members": [{"kind": "item", "id": "i1"}]},
                     {"id": "hero", "kinds": [], "ids": [], "members": []},
                     {"id": "walls", "kinds": [], "ids": [], "members": [], "rest": True}]
        found, left = groups_module.assign(with_rest, objects, orphan)
        self.assertEqual({k: sorted(o.name for o in v) for k, v in found.items()},
                         {"a": ["m1"], "hero": [], "walls": ["UCX_gone_00", "hand"]})
        self.assertEqual(left, [])
        single = [{"id": "building", "kinds": [], "ids": [], "members": []}]
        found, left = groups_module.assign(single, objects, orphan)
        self.assertEqual(sorted(o.name for o in found["building"]), ["UCX_gone_00", "hand", "m1"])
        self.assertEqual(left, [])
        with_filter = [{"id": "hero", "kinds": ["cube"], "ids": [], "members": []},
                       {"id": "walls", "kinds": [], "ids": [], "members": [], "rest": True}]
        found, _ = groups_module.assign(with_filter, objects, orphan)
        self.assertEqual([o.name for o in found["hero"]], ["hand"])

    # A group whose members match nothing is not written, is in empty_groups, and the groups check warns.
    def test_empty_group(self):
        output = self.run_groups(groups_with_empty())
        self.assertEqual(output["empty_groups"], ["lights"])
        self.assertEqual(sorted(self.written(output)), ["furniture_r1", "structure", "surfaces"])
        self.assertFalse(os.path.exists(self.path("export", ".staging", "Square_Lights.fbx")))
        answer = statuses(output)["groups"]
        self.assertEqual(answer["status"], "warning")
        self.assertIn("lights", answer["findings"][0]["message"])
        self.assertTrue(output["written"])

    # With no group taking an object, the groups check fails naming it and nothing is written.
    def test_unassigned_fails(self):
        groups = grouped_groups()
        groups[0]["rest"] = False
        groups[0]["members"] = [m for m in groups[0]["members"] if m["kind"] != "door"]
        output = self.run_groups(groups)
        answer = statuses(output)["groups"]
        self.assertEqual(answer["status"], "fail")
        self.assertTrue(answer["findings"])
        for entry in answer["findings"]:
            self.assertFalse(entry["object"].startswith(("SM_", "UCX_")), entry["object"])
        self.assertFalse(output["written"])
        self.assertEqual(output["files"], [])
        params = self.grouped(groups)
        params["force"] = True
        forced = self.export(params=params)["output"]
        self.assertTrue(forced["written"])
        self.assertEqual(statuses(forced)["groups"]["status"], "fail")

    # The scale check passes over several files, also when the walls sit in another file.
    def test_fbx_scale_over_files(self):
        self.assertEqual(statuses(self.run_groups())["fbx_scale"]["status"], "pass")
        groups = grouped_groups()
        groups[0]["members"] = [m for m in groups[0]["members"] if m["kind"] != "walls"]
        groups[1]["members"].append({"kind": "walls", "id": "f1"})
        output = self.run_groups(groups)
        self.assertEqual(statuses(output)["fbx_scale"]["status"], "pass")
        self.assertIn("f1_Walls", self.read("Square_Floors.fbx")[0])
        output = self.run_groups(groups, profile="unreal")
        self.assertEqual(statuses(output)["fbx_scale"]["status"], "pass")
        self.assertIn("SM_Square_Floors", self.read("Square_Floors.fbx")[0])

    # The single group gives the same file as before, with no kit and no empty groups.
    def test_single_unchanged(self):
        output = self.export(params=self.params(clean=True))["output"]
        self.assertEqual([f["file"] for f in output["files"]], ["Square.fbx"])
        self.assertEqual(output["empty_groups"], [])
        self.assertIsNone(output["kit"])
        self.assertEqual([s["step"] for s in output["steps"]], STEPS)
        self.assertEqual(statuses(output)["groups"]["status"], "pass")

    # The collision check asks for a UCX_ mesh of every kit piece.
    def test_collision_names_kit(self):
        self.assertIn("kit", collision.NEEDS_COLLISION)

    # The job check refuses a bad member and a bad rest switch.
    def test_bad_members(self):
        params = self.grouped()
        params["groups"][0]["members"] = [{"kind": 5}]
        result = self.export(params=params)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "bad_job")
        params = self.grouped()
        params["groups"][0]["rest"] = "yes"
        self.assertEqual(self.export(params=params)["error"]["code"], "bad_job")


if __name__ == "__main__":
    unittest.main()
