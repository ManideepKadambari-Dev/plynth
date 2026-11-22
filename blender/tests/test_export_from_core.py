import atexit
import json
import os
import shutil
import unittest

import bpy

import helpers
from build_helpers import BuildTest
from export_helpers import DATA_FOLDER, MODELS
from plynth import scene, shading
from schema_check import load_schema, validate

COLORS = {"white_paint": (235, 235, 230), "grey_tile": (120, 125, 130), "oak_planks": (170, 120, 70),
          "red_brick": (160, 70, 55), "painted_wood": (190, 190, 180), "brushed_steel": (180, 180, 185),
          "clear_glass": (200, 220, 230)}
ASSETS = ("sofa", "table")
_project = {}


# Reads one of the job files that plynth build and plynth export made on a scratch project.
def load_job(name):
    with open(os.path.join(DATA_FOLDER, name + ".json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


# Returns a copy of the data with the folder placeholders replaced.
def fill(data, project, library):
    if isinstance(data, dict):
        return {key: fill(value, project, library) for key, value in data.items()}
    if isinstance(data, list):
        return [fill(value, project, library) for value in data]
    if isinstance(data, str):
        return data.replace("@PROJECT@", project).replace("@LIBRARY@", library)
    return data


# Writes a one color image of the type the file name ends in.
def write_image(path, color):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if path.endswith(".png"):
        shading.write_constant_png(path, color)
        return
    image = bpy.data.images.new("fixture", 4, 4)
    image.pixels = [c / 255.0 for c in color + (255,)] * 16
    image.filepath_raw = path
    image.file_format = "JPEG"
    image.save()
    bpy.data.images.remove(image)


class Fixture(BuildTest):
    # Not a test. Makes the library and the project the way the scratch project had them, and builds it.
    def runTest(self):
        pass

    # Writes the map files of the library materials at the paths the export job names.
    def make_materials(self, export_job, library):
        for entry in export_job["params"]["materials"]:
            if entry["material"] is None:
                continue
            for channel, path in entry["maps"].items():
                if path and path.startswith(library):
                    write_image(path, COLORS[entry["material"]] if channel == "base_color" else (128, 128, 128))

    # Cleans one sample model into the library folder, as the asset import does.
    def make_asset(self, asset_id, library):
        copy_folder = self.path("src", asset_id)
        shutil.copytree(os.path.join(MODELS, asset_id), copy_folder)
        params = {
            "source": {"file": "%s/%s.obj" % (copy_folder, asset_id), "format": "obj"},
            "asset_id": asset_id,
            "output_folder": "%s/assets/items/%s" % (library, asset_id),
            "scale": 1.0,
            "rotate_z": 0,
            "triangle_warning": 500000,
            "bake_size": 256,
            "thumbnail": {"render": False, "size": 64, "background": [0.05, 0.07, 0.09]},
        }
        result = self.run_task("cleanup_asset", params, job_id="clean-" + asset_id)
        assert result["ok"], result["error"]
        bpy.ops.wm.read_factory_settings(use_empty=True)

    # Builds the project with the build job of the scratch project and returns the folders.
    def make_project(self):
        project = self.path("project")
        library = self.path("Library")
        export_job = fill(load_job("from_core_export"), project, library)
        self.make_materials(export_job, library)
        for asset_id in ASSETS:
            self.make_asset(asset_id, library)
        build_job = fill(load_job("from_core_build"), project, library)
        os.makedirs(project + "/build", exist_ok=True)
        result = self.run_task("build", build_job["params"], job_id="build-1")
        assert result["ok"], result["error"]
        return project, library


# Builds the shared project once and removes it when the tests end.
def shared_project():
    if "project" not in _project:
        fixture = Fixture()
        fixture.setUp()
        atexit.register(shutil.rmtree, fixture.folder, True)
        _project["project"], _project["library"] = fixture.make_project()
    return _project["project"], _project["library"]


class FromCoreExportTests(helpers.TempFolderTest):
    # Returns the export job made by plynth export on the scratch project, with the folders filled in and the
    # files going into a staging folder of this test.
    def job(self, write=True, force=False):
        project, library = shared_project()
        job = fill(load_job("from_core_export"), project, library)
        job["params"]["output_folder"] = self.path("export", ".staging")
        job["params"]["write"] = write
        job["params"]["force"] = force
        job["result_file"] = self.path("work", "result.json")
        return job

    # Runs the export task of a job and returns the result.
    def export(self, job):
        return self.run_task("export", job["params"], job_id=job["job_id"])

    # The fixtures are plain job files with placeholders for the folders, and they match the job schema.
    def test_job_validates(self):
        raw = load_job("from_core_export")
        self.assertEqual(validate(raw, load_schema("blender-job")), [])
        self.assertEqual(validate(load_job("from_core_build"), load_schema("blender-job")), [])
        self.assertIn("@PROJECT@", raw["params"]["project"]["folder"])
        job = self.job()
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        self.assertNotIn("@", json.dumps(job))
        self.assertEqual(job["params"]["groups"][0]["file"], "m9.fbx")
        self.assertEqual(raw["params"]["expected_bounds"], {"min": [5.0, -13.0, 0.0], "max": [19.0, -3.0, 3.0]})

    # Nothing fails, so the files are written. The UV check warns about the sample furniture and the end faces
    # of the walls, which is expected.
    def test_checks_pass(self):
        result = self.export(self.job())
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual(len(output["checks"]), 12)
        self.assertEqual([i for i, s in statuses.items() if s == "fail"], [])
        self.assertEqual([i for i, s in statuses.items() if s == "warning"], ["uvs"])
        self.assertEqual(statuses["fbx_scale"], "pass")
        self.assertEqual(statuses["materials"], "pass")
        self.assertTrue(output["written"])
        self.assertEqual(output["files"][0]["file"], "m9.fbx")
        self.assertTrue(os.path.isfile(self.path("export", ".staging", "m9.fbx")))
        self.assertGreater(len(output["textures"]), 10)

    # With write false the checks run and nothing is written.
    def test_check_only(self):
        result = self.export(self.job(write=False))
        self.assertTrue(result["ok"], result["error"])
        output = result["output"]
        self.assertFalse(output["written"])
        self.assertEqual([c["id"] for c in output["checks"] if c["status"] == "fail"], [])
        self.assertFalse(os.path.exists(self.path("export")))

    # The written FBX reads back with the bounds the plan gives, and the textures it names are beside it.
    def test_reimport_bounds(self):
        job = self.job()
        self.assertTrue(self.export(job)["ok"])
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("export", ".staging", "m9.fbx"))
        wanted = job["params"]["expected_bounds"]
        low, high = scene.world_bounds([bpy.data.objects["SM_f1_Walls"]])
        for got, want in zip(list(low) + list(high), wanted["min"] + wanted["max"]):
            self.assertAlmostEqual(got, want, delta=1e-3)
        # A door leaf stands a few millimetres outside the wall faces, so the openings are left out.
        shown = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("SM_") and "_o" not in o.name]
        low, high = scene.world_bounds(shown)
        for axis in range(3):
            self.assertGreaterEqual(low[axis], wanted["min"][axis] - 1e-3)
            self.assertLessEqual(high[axis], wanted["max"][axis] + 1e-3)
        names = {o.name for o in bpy.data.objects}
        for name in ("SM_sofa_i1", "SM_table_i2", "UCX_SM_f1_Walls_00"):
            self.assertIn(name, names)
        textured = [i for i in bpy.data.images if i.filepath]
        self.assertTrue(textured)
        folder = os.path.normcase(os.path.abspath(self.path("export", ".staging")))
        for image in textured:
            path = os.path.abspath(bpy.path.abspath(image.filepath))
            self.assertTrue(os.path.isfile(path), path)
            self.assertTrue(os.path.normcase(path).startswith(folder), path)


if __name__ == "__main__":
    unittest.main()
