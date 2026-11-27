import atexit
import copy
import json
import os
import shutil
import unittest

import bpy

import helpers
import test_export_from_core as base
from plynth import scene
from schema_check import load_schema, validate

_project = {}


# Builds the grouped scratch project once with the build job that plynth build made for it, and removes it when
# the tests end. Returns the project and library folders.
def shared_project():
    if "project" not in _project:
        fixture = base.Fixture()
        fixture.setUp()
        atexit.register(shutil.rmtree, fixture.folder, True)
        project = fixture.path("project")
        library = fixture.path("Library")
        export_job = base.fill(base.load_job("from_core_grouped_export"), project, library)
        fixture.make_materials(export_job, library)
        for asset_id in base.ASSETS:
            fixture.make_asset(asset_id, library)
        build_job = base.fill(base.load_job("from_core_grouped_build"), project, library)
        os.makedirs(project + "/build", exist_ok=True)
        result = fixture.run_task("build", build_job["params"], job_id="build-1")
        assert result["ok"], result["error"]
        _project["project"], _project["library"] = project, library
    return _project["project"], _project["library"]


# Returns the room type of each furniture group, in group order, from the rooms of the build job. The ids are
# random in a captured project, so a group is known by the type of the room its id names.
def furniture_room_types(groups):
    build = base.load_job("from_core_grouped_build")
    types = {room["id"]: room["type"] for floor in build["params"]["floors"] for room in floor["rooms"]}
    found = []
    for group in groups:
        if not group["id"].startswith("furniture_"):
            found.append(None)
            continue
        found.append(types.get(group["id"][len("furniture_"):]))
    return found


# Reads an FBX file into an empty scene and returns the sorted names of its mesh objects.
def read_names(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    return sorted(o.name for o in bpy.data.objects if o.type == "MESH")


class FromCoreGroupedTests(helpers.TempFolderTest):
    # Returns the grouped export job made by plynth export on the scratch project, with the folders filled in and
    # the files going into a staging folder of this test.
    def job(self, write=True):
        project, library = shared_project()
        job = base.fill(base.load_job("from_core_grouped_export"), project, library)
        job["params"]["output_folder"] = self.path("export", ".staging")
        job["params"]["write"] = write
        job["result_file"] = self.path("work", "result.json")
        return job

    # Runs the export task of a job and returns the result.
    def export(self, job):
        return self.run_task("export", job["params"], job_id=job["job_id"])

    # The fixtures are plain job files with placeholders, and the export job matches the job schema.
    def test_job_validates(self):
        raw = base.load_job("from_core_grouped_export")
        self.assertEqual(validate(raw, load_schema("blender-job")), [])
        self.assertEqual(validate(base.load_job("from_core_grouped_build"), load_schema("blender-job")), [])
        self.assertIn("@PROJECT@", raw["params"]["project"]["folder"])
        job = self.job()
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        self.assertNotIn("@", json.dumps(job))
        groups = job["params"]["groups"]
        self.assertEqual([g["id"] for g in groups[:2]], ["structure", "surfaces"])
        self.assertEqual(furniture_room_types(groups[2:]), ["living", "bedroom"])
        self.assertEqual([g["rest"] for g in groups].count(True), 1)

    # Every check passes, the groups check too, and each group gets its file.
    def test_checks_pass(self):
        job = self.job()
        result = self.export(job)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual([i for i, s in statuses.items() if s == "fail"], [])
        self.assertEqual(statuses["groups"], "pass")
        self.assertEqual(statuses["fbx_scale"], "pass")
        self.assertTrue(output["written"])
        self.assertEqual(output["empty_groups"], [])
        self.assertIsNone(output["kit"])
        self.assertEqual(sorted(f["file"] for f in output["files"]),
                         sorted(g["file"] for g in job["params"]["groups"]))

    # Every object of the blend is in exactly one written file.
    def test_every_object_in_one_file(self):
        job = self.job()
        output = self.export(job)["output"]
        seen = []
        for entry in output["files"]:
            seen += read_names(self.path("export", ".staging", entry["file"]))
        self.assertEqual(len(seen), len(set(seen)))
        project, _ = shared_project()
        bpy.ops.wm.open_mainfile(filepath=project + "/build/building.blend")
        built = {o.name for o in scene.scene_objects() if o.type == "MESH" and o.get("plynth_kind") != "collision"}
        items = {"%s_%s" % (o.get("plynth_asset"), o.get("plynth_id")) for o in bpy.data.objects
                 if o.get("plynth_kind") == "item"}
        bpy.ops.wm.read_factory_settings(use_empty=True)
        self.assertEqual(set(seen), built | items)
        self.assertEqual(len(items), 3)
        self.assertEqual([n for n in seen if n.startswith(("SM_", "UCX_")) or "_Collision_" in n], [])
        self.assertEqual(len(seen), output["objects"])

    # With write false the checks run and nothing is written, as the core made check only export does.
    def test_check_only(self):
        result = self.export(self.job(write=False))
        self.assertTrue(result["ok"], result["error"])
        self.assertFalse(result["output"]["written"])
        self.assertEqual(result["output"]["files"], [])
        self.assertEqual([c["id"] for c in result["output"]["checks"] if c["status"] == "fail"], [])


if __name__ == "__main__":
    unittest.main()
