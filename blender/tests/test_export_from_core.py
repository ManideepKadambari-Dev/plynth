import atexit
import json
import os
import shutil
import unittest

import bpy

import helpers
from build_helpers import DATA_FOLDER as BUILD_DATA_FOLDER, BuildTest
from export_helpers import DATA_FOLDER, MODELS, read_names
from plynth import scene, shading
from schema_check import load_schema, validate

COLORS = {"white_paint": (235, 235, 230), "grey_tile": (120, 125, 130), "oak_planks": (170, 120, 70),
          "red_brick": (160, 70, 55), "painted_wood": (190, 190, 180), "brushed_steel": (180, 180, 185),
          "clear_glass": (200, 220, 230), "concrete": (150, 150, 148), "roof_tiles": (170, 80, 60)}
ASSETS = ("sofa", "table")
PLAIN_CHECKS = 11
UNREAL_CHECKS = 13
SURFACE_KINDS = ("floor", "ceiling", "thresholds", "slab", "stair", "roof")
_project = {}


# Reads one of the job files that plynth build and plynth export made on a scratch project: from the export
# fixtures, or from the build fixtures when it is not there.
def load_job(name):
    path = os.path.join(DATA_FOLDER, name + ".json")
    if not os.path.isfile(path):
        path = os.path.join(BUILD_DATA_FOLDER, name + ".json")
    with open(path, "r", encoding="utf-8") as handle:
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
    def make_project(self, export="from_core_export", build="from_core_build", assets=ASSETS, drop_collision=None,
                     break_part=None):
        project = self.path("project")
        library = self.path("Library")
        export_job = fill(load_job(export), project, library)
        self.make_materials(export_job, library)
        for asset_id in assets:
            self.make_asset(asset_id, library)
        build_job = fill(load_job(build), project, library)
        for floor in build_job["params"]["floors"]:
            floor["collision"] = [c for c in floor["collision"] if c["target"]["kind"] != drop_collision]
        if break_part:
            floors = build_job["params"]["floors"]
            if break_part == "slab":
                floors[0]["slab"]["polygons"][0]["outline"] = [[0.0, 0.0], [1.0, 1.0]]
            elif break_part == "stair":
                floors[0]["stairs"][0]["steps"][3]["outline"] = [[0.0, 0.0], [1.0, 1.0]]
            else:
                floors[0]["roofs"][0]["faces"][0]["outline"] = [[0, 0, 0], [1, 0, 0], [2, 0, 0]]
        os.makedirs(project + "/build", exist_ok=True)
        result = self.run_task("build", build_job["params"], job_id="build-1")
        assert result["ok"], result["error"]
        self.problems = result["output"]["problems"]
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
    # Returns the export job made by plynth export on the scratch project, with the folders filled in, the
    # files going into a staging folder of this test and the profile of the fixture unless another is asked for.
    def job(self, write=True, force=False, profile=None):
        project, library = shared_project()
        job = fill(load_job("from_core_export"), project, library)
        job["params"]["output_folder"] = self.path("export", ".staging")
        job["params"]["write"] = write
        job["params"]["force"] = force
        if profile is not None:
            job["params"]["profile"] = profile
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
        self.assertEqual(raw["params"]["profile"], "plain")
        self.assertEqual(raw["params"]["expected_bounds"], {"min": [5.0, -13.0, 0.0], "max": [19.0, -3.0, 3.0]})

    # Nothing fails or warns, so the files are written. The sample furniture and the rounded wall corners have
    # usable UVs, so the UV check passes too. The plain export has no collision and no engine names.
    def test_checks_pass(self):
        result = self.export(self.job())
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual(len(output["checks"]), PLAIN_CHECKS)
        self.assertEqual(output["collision_objects"], 0)
        self.assertEqual([i for i, s in statuses.items() if s == "fail"], [])
        self.assertEqual([i for i, s in statuses.items() if s == "warning"], [])
        self.assertEqual(statuses["fbx_scale"], "pass")
        self.assertEqual(statuses["materials"], "pass")
        self.assertTrue(output["written"])
        self.assertEqual(output["files"][0]["file"], "m9.fbx")
        self.assertTrue(os.path.isfile(self.path("export", ".staging", "m9.fbx")))
        self.assertGreater(len(output["textures"]), 10)
        names = read_names(self.path("export", ".staging", "m9.fbx"))
        self.assertEqual([n for n in names if n.startswith(("SM_", "UCX_")) or "_Collision_" in n], [])
        self.assertEqual(len(names), output["files"][0]["objects"])

    # The unreal export of the same project is one mesh SM_m9 with its UCX_ pieces, and every check passes.
    def test_unreal_checks_pass(self):
        result = self.export(self.job(profile="unreal"))
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual(len(output["checks"]), UNREAL_CHECKS)
        self.assertEqual([i for i, s in statuses.items() if s in ("fail", "warning")], [])
        self.assertTrue(output["written"])
        names = read_names(self.path("export", ".staging", "m9.fbx"))
        self.assertEqual([n for n in names if not n.startswith("UCX_")], ["SM_m9"])
        self.assertEqual(len([n for n in names if n.startswith("UCX_SM_m9_")]), output["collision_objects"])
        self.assertGreater(output["collision_objects"], 0)
        self.assertTrue(all(t.startswith("T_") for t in output["textures"]))
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
        low, high = scene.world_bounds([bpy.data.objects["f1_Walls"]])
        for got, want in zip(list(low) + list(high), wanted["min"] + wanted["max"]):
            self.assertAlmostEqual(got, want, delta=1e-3)
        # A door leaf stands a few millimetres outside the wall faces, so the openings are left out.
        shown = [o for o in bpy.data.objects if o.type == "MESH" and "_o" not in o.name]
        low, high = scene.world_bounds(shown)
        for axis in range(3):
            self.assertGreaterEqual(low[axis], wanted["min"][axis] - 1e-3)
            self.assertLessEqual(high[axis], wanted["max"][axis] + 1e-3)
        names = {o.name for o in bpy.data.objects}
        for name in ("sofa_i1", "table_i2", "f1_Walls"):
            self.assertIn(name, names)
        self.assertEqual([n for n in names if n.startswith(("SM_", "UCX_"))], [])
        textured = [i for i in bpy.data.images if i.filepath]
        self.assertTrue(textured)
        folder = os.path.normcase(os.path.abspath(self.path("export", ".staging")))
        for image in textured:
            path = os.path.abspath(bpy.path.abspath(image.filepath))
            self.assertTrue(os.path.isfile(path), path)
            self.assertTrue(os.path.normcase(path).startswith(folder), path)


# Builds the house project once (two floors, a stair, a gable roof and flat roofs, no furniture) and returns its
# folders. Removed when the tests end.
def shared_house():
    if "house" not in _project:
        fixture = Fixture()
        fixture.setUp()
        atexit.register(shutil.rmtree, fixture.folder, True)
        _project["house"] = fixture.make_project("from_core_house_export", "from_core_house", ())
    return _project["house"]


# Returns true when every vertex lies on the inner side of every face plane.
def convex(obj):
    vertices = [v.co for v in obj.data.vertices]
    return all((v - p.center).dot(p.normal) <= 1e-5 for p in obj.data.polygons for v in vertices)


# Builds the house project with the collision of one kind of target left out, once per kind, and returns its folders.
def house_without(kind):
    key = "house without " + kind
    if key not in _project:
        fixture = Fixture()
        fixture.setUp()
        atexit.register(shutil.rmtree, fixture.folder, True)
        _project[key] = fixture.make_project("from_core_house_export", "from_core_house", (), drop_collision=kind)
    return _project[key]


# Builds the house with one part's outline broken and returns its folders and the problems of the build.
def house_broken(kind):
    key = "house broken " + kind
    if key not in _project:
        fixture = Fixture()
        fixture.setUp()
        atexit.register(shutil.rmtree, fixture.folder, True)
        folders = fixture.make_project("from_core_house_export", "from_core_house", (), break_part=kind)
        _project[key] = folders + (fixture.problems,)
    return _project[key]


class HouseExportTests(helpers.TempFolderTest):
    # Returns the export job of the house with the files going into a staging folder of this test, with the
    # profile of the fixture (unreal) unless another is asked for.
    def job(self, profile=None):
        project, library = shared_house()
        job = fill(load_job("from_core_house_export"), project, library)
        job["params"]["output_folder"] = self.path("export", ".staging")
        job["params"]["write"] = True
        job["params"]["force"] = False
        if profile is not None:
            job["params"]["profile"] = profile
        job["result_file"] = self.path("work", "result.json")
        return job

    # Every quality check passes on the whole house, with the slab, stair and roofs included: the collision check
    # finds their UCX_ meshes and the materials check finds a slot for every face. Nothing warns either.
    def test_checks_pass(self):
        job = self.job()
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        result = self.run_task("export", job["params"], job_id=job["job_id"])
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual(len(output["checks"]), UNREAL_CHECKS)
        self.assertEqual([i for i, s in statuses.items() if s == "fail"], [])
        self.assertEqual([i for i, s in statuses.items() if s == "warning"], [])
        for check in ("collision", "materials", "naming", "scale", "transforms", "uvs"):
            self.assertEqual(statuses[check], "pass", check)
        self.assertTrue(output["written"])

    # The plain export of the house passes its checks and holds every slab, stair and roof under its plain name,
    # with no collision and no engine names.
    def test_plain_checks_pass(self):
        job = self.job(profile="plain")
        result = self.run_task("export", job["params"], job_id=job["job_id"])
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual(len(output["checks"]), PLAIN_CHECKS)
        self.assertEqual([i for i, s in statuses.items() if s in ("fail", "warning")], [])
        names = read_names(self.path("export", ".staging", job["params"]["groups"][0]["file"]))
        for target in ("f1_Slab", "f2_Slab", "f1_s1_Stair", "f1_Roof_00", "f2_Roof_00"):
            self.assertIn(target, names)
        self.assertEqual([n for n in names if n.startswith(("SM_", "UCX_")) or "_Collision_" in n], [])
        self.assertEqual(output["collision_objects"], 0)

    # In a grouped unreal export with a kit, each group is one mesh named after its file: the Floors mesh with the
    # UCX_ meshes of every slab, stair and roof and the Walls mesh with the rest. The totals count the building
    # and not the kit.
    def test_grouped_with_kit(self):
        job = self.job()
        single = self.run_task("export", job["params"], job_id=job["job_id"])
        self.assertTrue(single["ok"], single["error"])
        plain = single["output"]
        build = fill(load_job("from_core_house"), *shared_house())
        pieces = {"structure": 0, "surfaces": 0}
        for floor in build["params"]["floors"]:
            for entry in floor["collision"]:
                group = "surfaces" if entry["target"]["kind"] in SURFACE_KINDS else "structure"
                pieces[group] += len(entry["pieces"]) + len(entry["hulls"])

        from test_export_kit import make_kit, wall_piece
        piece = wall_piece()
        for edge in piece["wall_solids"][0]["outline"]["edges"]:
            if edge["slot"] == "Living_Wall":
                edge["slot"] = "Room_Wall"
        job = self.job()
        params = job["params"]
        params["output_folder"] = self.path("grouped", ".staging")
        params["groups"] = [
            {"id": "structure", "file": "House_Walls.fbx", "kinds": [], "ids": [], "name": "Walls", "rest": True,
             "members": [{"kind": kind, "id": None} for kind in ("walls", "door", "window")]},
            {"id": "surfaces", "file": "House_Floors.fbx", "kinds": [], "ids": [], "name": "Floors",
             "members": [{"kind": kind, "id": None}
                         for kind in ("floor", "ceiling", "thresholds", "slab", "stair", "roof")]}]
        params["kit"] = make_kit([piece])
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        result = self.run_task("export", params, job_id=job["job_id"])
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        for check in ("collision", "groups", "fbx_scale", "kit_file"):
            self.assertEqual(statuses[check], "pass", check)
        for key in ("triangles", "collision_objects"):
            self.assertEqual(output[key], plain[key], key)
        self.assertEqual(output["objects"], plain["objects"] + 1)
        self.assertEqual(output["kit"]["objects"], 1 + len(piece["collision"]))

        for entry in output["files"]:
            names = read_names(self.path("grouped", ".staging", entry["file"]))
            mesh = "SM_" + os.path.splitext(entry["file"])[0]
            self.assertEqual([n for n in names if not n.startswith("UCX_")], [mesh], entry["file"])
            hulls = [n for n in names if n.startswith("UCX_")]
            self.assertEqual(hulls, ["UCX_%s_%02d" % (mesh, n) for n in range(len(hulls))], entry["file"])
            self.assertEqual(len(hulls), pieces[entry["group"]], entry["file"])

    # Takes the collision of every slab, then every stair, then every roof out of the build in turn: the export then
    # fails the collision check and names each of those objects.
    def test_missing_part_collision_fails(self):
        for kind, names in (("slab", {"SM_f1_Slab", "SM_f2_Slab"}), ("stair", {"SM_f1_s1_Stair"}),
                            ("roof", {"SM_f1_Roof_00", "SM_f2_Roof_00"})):
            with self.subTest(kind=kind):
                project, library = house_without(kind)
                job = fill(load_job("from_core_house_export"), project, library)
                job["params"]["output_folder"] = self.path("export", ".staging")
                job["params"]["write"] = False
                job["result_file"] = self.path("work", "result.json")
                result = self.run_task("export", job["params"], job_id=job["job_id"])
                self.assertTrue(result["ok"], result["error"])
                check = next(c for c in result["output"]["checks"] if c["id"] == "collision")
                self.assertEqual(check["status"], "fail")
                self.assertEqual({f["object"] for f in check["findings"]}, names)
                self.assertTrue(all(f["plan_id"] for f in check["findings"]))

    # A bad outline of a slab, a stair or a roof section is a build error with a plan id, and the export of that
    # build fails build_complete and names it.
    def test_broken_part_blocks_export(self):
        cases = (("slab", "slab_failed", "f1"), ("stair", "stair_failed", "s1"), ("roof", "roof_failed", "f1"))
        for kind, code, plan_id in cases:
            with self.subTest(kind=kind):
                project, library, problems = house_broken(kind)
                errors = [p for p in problems if p["severity"] == "error"]
                self.assertEqual([(p["code"], p["object"]) for p in errors if p["code"] == code], [(code, plan_id)])
                self.assertTrue(all(p["object"] and not p["object"].startswith("SM_") for p in errors), errors)
                job = fill(load_job("from_core_house_export"), project, library)
                job["params"]["build"]["problems"] = problems
                job["params"]["output_folder"] = self.path("export", ".staging")
                job["params"]["write"] = False
                job["result_file"] = self.path("work", "result.json")
                result = self.run_task("export", job["params"], job_id=job["job_id"])
                self.assertTrue(result["ok"], result["error"])
                check = next(c for c in result["output"]["checks"] if c["id"] == "build_complete")
                self.assertEqual(check["status"], "fail")
                self.assertIn(plan_id, {f["plan_id"] for f in check["findings"]})

    # Before joining, every slab, stair and roof has its own convex UCX_ meshes, 16 for the stair's steps. The
    # written FBX then holds one mesh with every one of them, all convex.
    def test_fbx_has_part_collision(self):
        job = self.job()
        job["params"]["write"] = False
        self.assertTrue(self.run_task("export", job["params"], job_id=job["job_id"])["ok"])
        names = {o.name for o in bpy.data.objects}
        for target in ("SM_f1_Slab", "SM_f2_Slab", "SM_f1_s1_Stair", "SM_f1_Roof_00", "SM_f2_Roof_00"):
            self.assertIn(target, names)
            found = sorted(n for n in names if n.startswith("UCX_%s_" % target))
            self.assertTrue(found, target)
            for name in found:
                obj = bpy.data.objects[name]
                self.assertTrue(len(obj.data.polygons) >= 4 and convex(obj), name)
        self.assertEqual(len([n for n in names if n.startswith("UCX_SM_f1_s1_Stair_")]), 16)
        build = fill(load_job("from_core_house"), *shared_house())
        wanted = sum(len(c["pieces"]) + len(c["hulls"]) for f in build["params"]["floors"] for c in f["collision"])
        self.assertEqual(len([n for n in names if n.startswith("UCX_")]), wanted)
        job = self.job()
        self.assertTrue(self.run_task("export", job["params"], job_id=job["job_id"])["ok"])
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("export", ".staging", job["params"]["groups"][0]["file"]))
        meshes = [o for o in bpy.data.objects if o.type == "MESH"]
        hulls = [o for o in meshes if o.name.startswith("UCX_")]
        self.assertEqual(len(meshes) - len(hulls), 1)
        self.assertEqual(len(hulls), wanted)
        for obj in hulls:
            self.assertTrue(len(obj.data.polygons) >= 4 and convex(obj), obj.name)


if __name__ == "__main__":
    unittest.main()
