import os
import unittest

import bpy

import export_helpers
import helpers
import test_export_from_core as base
import test_export_groups_from_core as grouped
import test_export_kit_from_core as kit
from export_helpers import ExportTest, fixture_project, read_names
from schema_check import load_schema, validate

ENGINE_PREFIXES = ("SM_", "UCX_")
PLAIN_CHECKS = ["build_current", "build_complete", "scale", "transforms", "naming", "uvs", "texel_density",
                "triangles", "materials", "groups", "fbx_scale"]
UNREAL_CHECKS = PLAIN_CHECKS[:9] + ["collision"] + PLAIN_CHECKS[9:] + ["kit_file"]


# Returns every sample project of the export fixtures as (export job name, function giving its folders).
def samples():
    return [("from_core_export", base.shared_project), ("from_core_house_export", base.shared_house),
            ("from_core_grouped_export", grouped.shared_project), ("from_core_kit_export", kit.shared_project)]


# Returns the check ids in the order of the output and those that failed or were skipped. A warning does not
# block an export, and the file check of a kit that was not asked for may say skipped.
def check_summary(output):
    ids = [c["id"] for c in output["checks"]]
    bad = [c["id"] for c in output["checks"] if c["status"] in ("fail", "skipped")
           and not (c["id"] == "kit_file" and c["status"] == "skipped")]
    return ids, bad


class SampleProfileTests(helpers.TempFolderTest):
    # Exports one sample with a profile into a staging folder of this test and returns the job and the output.
    def export_sample(self, name, folders, profile):
        project, library = folders()
        job = base.fill(base.load_job(name), project, library)
        params = job["params"]
        params["profile"] = profile
        params["output_folder"] = self.path(profile, name, ".staging")
        params["write"] = True
        params["force"] = False
        job["result_file"] = self.path("work", "result.json")
        self.assertEqual(validate(job, load_schema("blender-job")), [], name)
        result = self.run_task("export", params, job_id=job["job_id"])
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [], name)
        return params, result["output"]

    # The plain export of every sample writes files with no SM_ or UCX_ object and no collision, passes its
    # checks, and every file reads back with the objects it says.
    def test_plain_every_sample(self):
        for name, folders in samples():
            with self.subTest(sample=name):
                params, output = self.export_sample(name, folders, "plain")
                ids, bad = check_summary(output)
                self.assertEqual(ids, PLAIN_CHECKS)
                self.assertEqual(bad, [])
                self.assertTrue(output["written"])
                self.assertEqual(output["collision_objects"], 0)
                self.assertIsNone(output["kit"])
                self.assertFalse([t for t in output["textures"] if t.startswith("T_")])
                for entry in output["files"]:
                    names = read_names(os.path.join(params["output_folder"], entry["file"]))
                    self.assertEqual([n for n in names if n.startswith(ENGINE_PREFIXES) or "_Collision_" in n], [],
                                     entry["file"])
                    self.assertEqual(len(names), entry["objects"], entry["file"])

    # The unreal export of every sample writes one mesh per group, named after its file, with its UCX_ pieces,
    # and passes every check; the files read back with the objects they say.
    def test_unreal_every_sample(self):
        for name, folders in samples():
            with self.subTest(sample=name):
                params, output = self.export_sample(name, folders, "unreal")
                ids, bad = check_summary(output)
                self.assertEqual(ids, UNREAL_CHECKS)
                self.assertEqual(bad, [])
                self.assertTrue(output["written"])
                self.assertGreater(output["collision_objects"], 0)
                hulls = 0
                for entry in output["files"]:
                    names = read_names(os.path.join(params["output_folder"], entry["file"]))
                    mesh = "SM_" + os.path.splitext(entry["file"])[0]
                    self.assertEqual([n for n in names if not n.startswith("UCX_")], [mesh], entry["file"])
                    pieces = [n for n in names if n.startswith("UCX_")]
                    self.assertEqual(pieces, ["UCX_%s_%02d" % (mesh, n) for n in range(len(pieces))], entry["file"])
                    self.assertEqual(len(names), entry["objects"], entry["file"])
                    hulls += len(pieces)
                self.assertEqual(hulls, output["collision_objects"])
                self.assertEqual(len(output["files"]), len([g for g in params["groups"]
                                                            if g["id"] not in output["empty_groups"]]))


class OldBuildTests(ExportTest):
    # Saves a copy of the shared build named the way Plynth 0.1.1 named it: every built object SM_<name>, every
    # collision mesh UCX_<its object>_<NN> pointing at that SM_ name. Returns its path.
    def old_blend(self):
        project = fixture_project(clean=True)
        bpy.ops.wm.open_mainfile(filepath=project + "/build/building.blend")
        old_of = {}
        for obj in sorted(bpy.context.scene.collection.all_objects, key=lambda o: o.name):
            kind = obj.get("plynth_kind")
            if kind is None or kind in ("collision", "light"):
                continue
            old_of[obj.name] = "SM_" + obj.name
            obj.name = "SM_" + obj.name
        numbers = {}
        hulls = [o for o in bpy.context.scene.collection.all_objects if o.get("plynth_kind") == "collision"]
        for obj in sorted(hulls, key=lambda o: o.name):
            target = old_of.get(obj["plynth_collision_for"], obj["plynth_collision_for"])
            number = numbers.get(target, 0)
            numbers[target] = number + 1
            obj.name = "UCX_%s_%02d" % (target, number)
            obj["plynth_collision_for"] = target
        self.assertTrue(numbers)
        self.assertIn("SM_f1_Walls", bpy.data.objects)
        self.assertIn("UCX_SM_sofa_i1_00", bpy.data.objects)
        path = self.path("old", "building.blend")
        os.makedirs(os.path.dirname(path))
        bpy.ops.wm.save_as_mainfile(filepath=path, copy=True, relative_remap=False)
        return path

    # Exports with a profile and returns the output and the sorted mesh names of the written single file.
    def export_named(self, profile, blend_file=None):
        result = self.export(clean=True, profile=profile, blend_file=blend_file)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertEqual(check_summary(output)[1], [], output["checks"])
        self.assertTrue(output["written"])
        return output, read_names(self.path("export", ".staging", "Square.fbx"))

    # A building.blend made by 0.1.1 exports both ways like a new build: plain names with no SM_ and no collision,
    # and for unreal one SM_Square mesh with the same UCX_ pieces.
    def test_old_build_both_ways(self):
        old = self.old_blend()
        for profile in ("plain", "unreal"):
            with self.subTest(profile=profile):
                new_output, new_names = self.export_named(profile)
                old_output, old_names = self.export_named(profile, old)
                self.assertEqual(old_names, new_names)
                for key in ("objects", "triangles", "collision_objects", "textures"):
                    self.assertEqual(old_output[key], new_output[key], key)
                if profile == "plain":
                    self.assertIn("f1_Walls", old_names)
                    self.assertEqual([n for n in old_names if n.startswith(ENGINE_PREFIXES)], [])
                else:
                    self.assertEqual([n for n in old_names if not n.startswith("UCX_")], ["SM_Square"])
                    self.assertGreater(old_output["collision_objects"], 0)

    # The tracked export fixtures name a profile the worker knows.
    def test_fixture_profiles(self):
        for name in ("square", "check_only", "forced", "grouped_three", "grouped_empty_group", "kit_three",
                     "kit_bare_opening", "from_core_export", "from_core_grouped_export", "from_core_house_export",
                     "from_core_kit_export"):
            self.assertIn(export_helpers.load_job(name)["params"]["profile"], ("plain", "unreal"), name)


if __name__ == "__main__":
    unittest.main()
