import atexit
import copy
import json
import math
import os
import shutil
import tempfile

import bpy

import helpers
from build_helpers import BuildTest, load_params
from plynth import shading
from schema_check import load_schema, validate

DATA_FOLDER = os.path.join(helpers.BLENDER_FOLDER, "tests", "data", "export")
MODELS = os.path.join(helpers.REPO_FOLDER, "samples", "models")
BACKGROUND = [0.0549, 0.0667, 0.0863]
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
LIMITS = {"object_triangles": 1000000, "total_triangles": 10000000, "low_texel_density": 256,
          "texel_warning_ratio": 0.5, "texel_fail_ratio": 0.25, "bounds_tolerance": 0.001}
BOUNDS = {"min": [-0.1, -4.1, 0.0], "max": [4.1, 0.1, 3.0]}
SOFAS = [("i1", 1.5, 2.0, 90.0), ("i2", 3.0, 3.0, 200.0)]

_fixture = {}
_clean_fixture = {}


# Returns the export job of the hand written fixture files with the given placeholders left as they are.
def job_params(write=True, force=False):
    return {
        "project": {"name": "Square", "folder": "project"},
        "blend_file": "project/build/building.blend",
        "output_folder": "project/export/.staging",
        "textures_folder": "Textures",
        "groups": [{"id": "building", "file": "Square.fbx", "kinds": [], "ids": []}],
        "materials": [],
        "expected_bounds": copy.deepcopy(BOUNDS),
        "build": {"report_found": True, "plan_changed": False, "problems": []},
        "limits": dict(LIMITS),
        "force": force,
        "write": write,
    }


# Writes the fixture job files of tests/data/export.
def write_fixtures():
    os.makedirs(DATA_FOLDER, exist_ok=True)
    for name, params in (("square", job_params()), ("check_only", job_params(write=False)),
                         ("forced", job_params(force=True))):
        data = {"format": "plynth-blender-job", "version": 1, "task": "export", "job_id": "export-1",
                "result_file": "result.json", "params": params}
        with open(os.path.join(DATA_FOLDER, name + ".json"), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(data, indent=1) + "\n")


# Reads a fixture job file's params.
def load_job(name):
    with open(os.path.join(DATA_FOLDER, name + ".json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


class FixtureBuilder(BuildTest):
    # Not a test. A BuildTest that builds the shared project once.
    def runTest(self):
        pass

    # Writes a map set for a library material and returns its job entry.
    def make_material(self, material_id, color):
        folder = self.path("library", material_id)
        os.makedirs(folder, exist_ok=True)
        maps = {}
        for channel in CHANNELS:
            if channel == "opacity":
                maps[channel] = None
                continue
            path = os.path.join(folder, channel + ".png").replace("\\", "/")
            shading.write_constant_png(path, color if channel == "base_color" else (128, 128, 128))
            maps[channel] = path
        return {"id": material_id, "tile_size": {"width": 2.0, "height": 1.0}, "maps": maps}

    # Unwraps every part of the cleaned sofa again into one shared UV map, so no two faces overlap.
    def separate_uvs(self, blend_file):
        bpy.ops.wm.open_mainfile(filepath=blend_file)
        parts = sorted((o for o in bpy.data.objects if o.type == "MESH"), key=lambda o: o.name)
        for part in parts:
            part.select_set(True)
        bpy.context.view_layer.objects.active = parts[0]
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(island_margin=0.02)
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.wm.save_mainfile()
        bpy.ops.wm.read_factory_settings(use_empty=True)

    # Cleans the sample sofa into an asset folder and returns the job's asset entry for it. With separate_uvs
    # the parts get their own space in the UV map.
    def make_sofa(self, separate_uvs=False):
        folder = self.path("library", "sofa")
        copy_folder = self.path("src", "sofa")
        shutil.copytree(os.path.join(MODELS, "sofa"), copy_folder)
        params = {
            "source": {"file": copy_folder + "/sofa.obj", "format": "obj"},
            "asset_id": "sofa",
            "output_folder": folder,
            "scale": 1.0,
            "rotate_z": 0,
            "triangle_warning": 500000,
            "bake_size": 256,
            "thumbnail": {"render": False, "size": 64, "background": BACKGROUND},
        }
        result = self.run_task("cleanup_asset", params, job_id="clean-sofa")
        assert result["ok"], result["error"]
        bpy.ops.wm.read_factory_settings(use_empty=True)
        if separate_uvs:
            self.separate_uvs(folder + "/asset.blend")
        materials = []
        for entry in result["output"]["material_maps"]:
            maps = {channel: (folder + "/" + entry["maps"][channel]["file"]) if entry["maps"][channel]["file"]
                    else None for channel in CHANNELS}
            materials.append({"name": entry["name"], "maps": maps})
        return {"id": "sofa", "blend_file": folder + "/asset.blend", "collection": "sofa", "materials": materials}

    # Makes the build job: the door room with paint_a on the living wall and ceiling, collision pieces and two sofas.
    # With clean true every slot shows paint_a, so no slot has the neutral look.
    def build_params(self, clean=False):
        from test_build_collision import collision_params
        params = copy.deepcopy(load_params("door"))
        params["settings"]["bevel_width"] = 0.0
        paint = self.make_material("paint_a", (200, 40, 40))
        params["materials"] = [paint]
        painted = None if clean else ("Living_Wall", "Living_Ceiling")
        for slot in params["slots"]:
            if painted is None or slot["name"] in painted:
                slot["material"] = "paint_a"
        floor = params["floors"][0]
        for room in floor["rooms"]:
            room["ceiling_material"] = "paint_a"
            if clean:
                room["floor_material"] = "paint_a"
        for solid in floor["wall_solids"]:
            for ring in [solid["outline"]] + solid["holes"]:
                for edge in ring["edges"]:
                    if painted is None or edge.get("slot") in painted:
                        edge["material"] = "paint_a"
        floor["collision"] = collision_params()["floors"][0]["collision"]
        params["assets"] = [self.make_sofa(separate_uvs=clean)]
        plan_items = []
        for item_id, x, y, rotation in SOFAS:
            floor["items"].append({"id": item_id, "asset": "sofa", "location": [x, -y, 0.0],
                                   "rotation_z": -math.radians(rotation),
                                   "size": {"width": 1.0, "depth": 1.0, "height": 1.0}, "wall": None})
            plan_items.append({"id": item_id, "asset": "sofa", "x": x, "y": y, "z": 0.0, "rotation": rotation})
        params["plan"] = {"version": 1, "floors": [{"id": "f1", "items": plan_items}]}
        return params


# Builds a shared fixture once and returns the project folder holding build/. Removed when the tests end. The
# clean one has a real material on every slot.
def fixture_project(clean=False):
    store = _clean_fixture if clean else _fixture
    if "folder" not in store:
        builder = FixtureBuilder()
        builder.setUp()
        atexit.register(shutil.rmtree, builder.folder, True)
        params = builder.build_params(clean)
        params["output_folder"] = builder.path("project", "build")
        params["blend_file"] = "building.blend"
        result = builder.run_task("build", params, job_id="build-1")
        assert result["ok"], result["error"]
        store["folder"] = builder.path("project")
        store["builder"] = builder
        store["problems"] = result["output"]["problems"]
        store["materials"] = json.load(open(builder.path("project", "build", "materials.json"), "r",
                                            encoding="utf-8"))["materials"]
    return store["folder"]


class ExportTest(helpers.TempFolderTest):
    # Returns the export job params for the shared project, writing into a staging folder of this test.
    def params(self, write=True, force=False, blend_file=None, clean=False):
        project = fixture_project(clean)
        params = job_params(write=write, force=force)
        params["project"]["folder"] = project
        params["blend_file"] = blend_file or project + "/build/building.blend"
        params["output_folder"] = self.path("export", ".staging")
        params["materials"] = copy.deepcopy((_clean_fixture if clean else _fixture)["materials"])
        return params

    # Runs the export task and returns the result.
    def export(self, **kwargs):
        overrides = {key: kwargs.pop(key) for key in ("params",) if key in kwargs}
        params = overrides.get("params") or self.params(**kwargs)
        return self.run_task("export", params, job_id="export-1")

    # Checks the export job against the job schema.
    def assert_job_valid(self, params):
        job = {"format": "plynth-blender-job", "version": 1, "task": "export", "job_id": "export-1",
               "result_file": "result.json", "params": params}
        self.assertEqual(validate(job, load_schema("blender-job")), [], "job does not match the schema")

    # Returns the sorted names of the files under a folder, relative to it.
    def files_under(self, folder):
        found = []
        for root, _, names in os.walk(folder):
            for name in names:
                found.append(os.path.relpath(os.path.join(root, name), folder).replace("\\", "/"))
        return sorted(found)
