import atexit
import importlib.util
import json
import os
import shutil
import unittest

import bpy

import helpers
import test_export_from_core as base
from export_helpers import DATA_FOLDER
from schema_check import load_schema, validate

KIT_FILE = "m11_Kit.fbx"
LAYOUT_FILE = "m11_Kit_layout.json"
_project = {}


# Builds the scratch project once with the build job that plynth build made for it, and removes it when the
# tests end. Returns the project and library folders.
def shared_project():
    if "project" not in _project:
        fixture = base.Fixture()
        fixture.setUp()
        atexit.register(shutil.rmtree, fixture.folder, True)
        project = fixture.path("project")
        library = fixture.path("Library")
        export_job = base.fill(base.load_job("from_core_kit_export"), project, library)
        fixture.make_materials(export_job, library)
        for asset_id in base.ASSETS:
            fixture.make_asset(asset_id, library)
        build_job = base.fill(base.load_job("from_core_kit_build"), project, library)
        os.makedirs(project + "/build", exist_ok=True)
        result = fixture.run_task("build", build_job["params"], job_id="build-1")
        assert result["ok"], result["error"]
        _project["project"], _project["library"] = project, library
    return _project["project"], _project["library"]


# Loads kit_layout_check.py as a module without running it.
def load_layout_check():
    path = os.path.join(helpers.BLENDER_FOLDER, "tests", "kit_layout_check.py")
    spec = importlib.util.spec_from_file_location("kit_layout_check", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Reads an FBX file into an empty scene and returns the sorted names of its mesh objects.
def read_names(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    return sorted(o.name for o in bpy.data.objects if o.type == "MESH")


# Returns the metres per UV unit of the faces of the objects, by (material, side or top and bottom), and the
# face area behind each number.
def density(objects):
    found = {}
    for obj in objects:
        mesh = obj.data
        layer = mesh.uv_layers[0].data
        for polygon in mesh.polygons:
            points = [layer[i].uv for i in polygon.loop_indices]
            area = abs(sum(points[k][0] * points[(k + 1) % len(points)][1]
                           - points[(k + 1) % len(points)][0] * points[k][1] for k in range(len(points)))) / 2.0
            if area < 1e-9:
                continue
            key = (mesh.materials[polygon.material_index].name.split(".")[0],
                   "side" if abs(polygon.normal.z) < 0.2 else "flat")
            total = found.setdefault(key, [0.0, 0.0])
            total[0] += polygon.area
            total[1] += area
    return {key: (values[0] ** 0.5 / values[1] ** 0.5, values[0]) for key, values in found.items()}


class FromCoreKitTests(helpers.TempFolderTest):
    # Returns the kit export job made by plynth export on the scratch project, with the folders filled in and the
    # files going into a staging folder of this test.
    def job(self, write=True):
        project, library = shared_project()
        job = base.fill(base.load_job("from_core_kit_export"), project, library)
        job["params"]["output_folder"] = self.path("export", ".staging")
        job["params"]["write"] = write
        job["result_file"] = self.path("work", "result.json")
        return job

    # Runs the export task of a job and returns the result.
    def export(self, job):
        return self.run_task("export", job["params"], job_id=job["job_id"])

    # The fixtures are plain job files with placeholders, and the export job matches the job schema.
    def test_job_validates(self):
        raw = base.load_job("from_core_kit_export")
        self.assertEqual(validate(raw, load_schema("blender-job")), [])
        self.assertEqual(validate(base.load_job("from_core_kit_build"), load_schema("blender-job")), [])
        self.assertIn("@PROJECT@", raw["params"]["project"]["folder"])
        job = self.job()
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        self.assertNotIn("@", json.dumps(job))
        kit = job["params"]["kit"]
        self.assertEqual(kit["file"], KIT_FILE)
        self.assertEqual(sorted({p["kind"] for p in kit["pieces"]}), ["corner", "opening", "wall"])
        cuts = [e for p in kit["pieces"] for so in p["wall_solids"] for ring in [so["outline"]] + so["holes"]
                for e in ring["edges"] if e.get("kit_cut")]
        self.assertTrue(cuts)
        self.assertEqual({e["wall"] for e in cuts}, {None}, "core sends no wall on a kit cut")
        self.assertTrue(all(m["tile_size"] for m in job["params"]["materials"] if m["material"]))
        with open(os.path.join(DATA_FOLDER, "from_core_kit_layout.json"), "r", encoding="utf-8") as handle:
            layout = json.load(handle)
        self.assertEqual(validate(layout, load_schema("kit-layout")), [])
        self.assertEqual([p["id"] for p in kit["pieces"]], [p["id"] for p in layout["pieces"]])

    # Every check passes, the kit file too, and the kit and the group files are written.
    def test_checks_pass_and_kit_written(self):
        job = self.job()
        result = self.export(job)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(validate(result, load_schema("blender-result")), [])
        output = result["output"]
        statuses = {c["id"]: c["status"] for c in output["checks"]}
        self.assertEqual([i for i, s in statuses.items() if s == "fail"], [])
        self.assertEqual(statuses["kit_file"], "pass")
        self.assertEqual(statuses["groups"], "pass")
        self.assertEqual(statuses["fbx_scale"], "pass")
        self.assertTrue(output["written"])
        pieces = job["params"]["kit"]["pieces"]
        self.assertEqual(output["kit"]["file"], KIT_FILE)
        self.assertEqual(output["kit"]["pieces"], len(pieces))
        self.assertEqual(output["kit"]["objects"], len(pieces) + sum(len(p["collision"]) for p in pieces))
        self.assertGreater(output["kit"]["triangles"], 0)
        self.assertTrue(os.path.isfile(self.path("export", ".staging", KIT_FILE)))
        names = read_names(self.path("export", ".staging", KIT_FILE))
        self.assertEqual(sorted(n for n in names if not n.startswith("UCX_")),
                         sorted(p["mesh"] for p in pieces))

    # No kit object is in a group file.
    def test_kit_not_in_group_files(self):
        output = self.export(self.job())["output"]
        for entry in output["files"]:
            names = read_names(self.path("export", ".staging", entry["file"]))
            self.assertEqual([n for n in names if "Kit_" in n], [])

    # With write false the pieces are checked and no kit file is written.
    def test_check_only(self):
        result = self.export(self.job(write=False))
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["kit"])
        self.assertEqual([c["id"] for c in result["output"]["checks"] if c["status"] == "fail"], [])
        self.assertFalse(os.path.exists(self.path("export")))

    # The kit pieces show each material at the size the walls do: the same metres per UV unit for every slot.
    def test_texture_density_matches_walls(self):
        self.assertTrue(self.export(self.job())["ok"])
        walls = [o for o in bpy.data.objects if o.name.startswith("SM_f1_") and o.type == "MESH"]
        pieces = [o for o in bpy.data.objects if o.name.startswith("SM_Kit_")]
        self.assertTrue(walls and pieces)
        wanted = density(walls)
        got = density(pieces)
        self.assertNotEqual(wanted[("Exterior_Wall", "side")][0], 1.0, "the fixture has no tiled material")
        compared = 0
        for key, (value, area) in got.items():
            if key in wanted and min(area, wanted[key][1]) > 1.0:
                self.assertAlmostEqual(value / wanted[key][0], 1.0, delta=0.03, msg=str(key))
                compared += 1
        self.assertGreaterEqual(compared, 4)

    # Placing the kit by its layout rebuilds the walls: same bounds, same area seen from above, and two pictures.
    def test_layout_check(self):
        self.assertTrue(self.export(self.job())["ok"])
        folder = self.path("export", ".staging")
        shutil.copyfile(os.path.join(DATA_FOLDER, "from_core_kit_layout.json"), os.path.join(folder, LAYOUT_FILE))
        answer = load_layout_check().check(folder, self.path("renders"))
        self.assertTrue(answer["ok"], answer)
        self.assertEqual(answer["placed"], answer["placements"])
        self.assertEqual(answer["missing_pieces"], [])
        self.assertEqual(answer["visible_extra"], [], "collision or kit originals would show in the pictures")
        self.assertLessEqual(answer["bounds_difference"], 0.001)
        self.assertLessEqual(answer["area_difference"], 1e-3)
        self.assertGreater(answer["walls_top_area"], 10.0)
        for name in answer["renders"]:
            self.assertGreater(os.path.getsize(self.path("renders", name)), 1000)


if __name__ == "__main__":
    unittest.main()
