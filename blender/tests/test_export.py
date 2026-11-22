import hashlib
import importlib
import json
import os
import shutil
import unittest

import bpy
from mathutils import Matrix

import export_helpers
from export_helpers import ExportTest, fixture_project
from plynth import quality, registry, scene
from schema_check import load_schema, validate

STEPS = ["open_blend", "prepare", "name_objects", "textures", "checks", "write_fbx", "file_checks"]


# Returns the SHA 256 of a file.
def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


class FakeCheck:
    # A quality check that answers with a fixed status, for the tests of the gate.
    def __init__(self, phase, status):
        self.phase = phase
        self.status = status

    # Returns the check entry to register.
    def entry(self, order=1):
        return quality.QualityCheck(order, self.phase, "Fake check", self.run)

    # Returns the fixed answer.
    def run(self, ctx):
        return quality.result(self.status, "Fixed answer.", [quality.finding("About the walls.", "SM_f1_Walls", "w1")])


# Returns the id and status of the fake checks in an export output.
def fakes(output):
    return [(c["id"], c["status"]) for c in output["checks"] if c["id"].startswith("fake_")]


class ExportTests(ExportTest):
    # Registers a check for the length of one test.
    def register_check(self, check_id, entry):
        registry.discover(quality)
        registry.QUALITY_CHECKS.register(check_id, entry)
        self.addCleanup(registry.QUALITY_CHECKS._entries.pop, check_id, None)

    # The fixture job files are the ones the helper writes, and they match the job schema.
    def test_fixture_files(self):
        for name, write, force in (("square", True, False), ("check_only", False, False), ("forced", True, True)):
            job = export_helpers.load_job(name)
            self.assertEqual(validate(job, load_schema("blender-job")), [], name)
            self.assertEqual(job["params"], export_helpers.job_params(write=write, force=force))
        self.assert_job_valid(self.params())

    # The shared build has no problems and holds the pieces the tests need.
    def test_fixture_build(self):
        fixture_project()
        self.assertEqual(export_helpers._fixture["problems"], [])

    # The square room export writes the FBX and Textures, with the names the plan asks for.
    def test_export(self):
        result = self.export(clean=True)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertTrue(output["written"])
        staging = self.path("export", ".staging")
        self.assertIn("Square.fbx", self.files_under(staging))
        self.assertEqual([s["step"] for s in output["steps"]], STEPS)
        self.assertEqual(output["files"][0]["group"], "building")
        self.assertEqual(output["files"][0]["file"], "Square.fbx")
        self.assertEqual([c["id"] for c in output["checks"] if c["status"] == "fail"], [])
        objects = scene.scene_objects()
        names = sorted(o.name for o in objects if o.type == "MESH")
        for wanted in ("SM_f1_Walls", "SM_f1_r1_Floor", "SM_sofa_i1", "SM_sofa_i2", "UCX_SM_f1_Walls_00",
                       "UCX_SM_f1_Walls_03", "UCX_SM_f1_r1_Floor_00", "UCX_SM_sofa_i1_00", "UCX_SM_sofa_i2_00"):
            self.assertIn(wanted, names)
        for name in names:
            self.assertTrue(name.startswith("SM_") or name.startswith("UCX_"), name)
        self.assertEqual(output["collision_objects"], len([n for n in names if n.startswith("UCX_")]))
        self.assertEqual(output["objects"], len(names))
        self.assertEqual(len(objects), len(names))
        for obj in objects:
            self.assertIsNone(obj.parent)
            for row in range(4):
                for column in range(4):
                    self.assertAlmostEqual(obj.matrix_world[row][column], Matrix.Identity(4)[row][column], delta=1e-6)
        self.assertEqual(len([o for o in names if o.startswith("SM_sofa_")]), 2)
        self.assertIsNot(bpy.data.objects["SM_sofa_i1"].data, bpy.data.objects["SM_sofa_i2"].data)
        self.assertEqual(bpy.data.objects["UCX_SM_sofa_i1_00"]["plynth_collision_for"], "SM_sofa_i1")

    # Slot names are kept and furniture materials are named after their asset.
    def test_material_names(self):
        self.export(clean=True)
        used = {m.name for o in bpy.data.objects if o.type == "MESH" for m in o.data.materials if m}
        for slot in ("Living_Wall", "Living_Ceiling", "Living_Floor", "Exterior_Wall", "Wall_Top"):
            self.assertIn(slot, used)
        furniture = {m.name for m in bpy.data.objects["SM_sofa_i1"].data.materials if m}
        self.assertTrue(furniture)
        for name in furniture:
            self.assertTrue(name.startswith("Sofa_"), name)
        self.assertEqual(sorted(furniture), sorted(m.name for m in bpy.data.objects["SM_sofa_i2"].data.materials if m))

    # Textures are named after their material, copied byte for byte and once for each source file.
    def test_textures(self):
        result = self.export(clean=True)
        output = result["output"]
        folder = self.path("export", ".staging", "Textures")
        self.assertEqual(sorted(os.listdir(folder)), output["textures"])
        self.assertIn("T_PaintA_BaseColor.png", output["textures"])
        self.assertIn("T_PaintA_Normal.png", output["textures"])
        self.assertEqual([t for t in output["textures"] if t.startswith("T_PaintA_")].count("T_PaintA_BaseColor.png"), 1)
        project = fixture_project(clean=True)
        source = os.path.join(project, "..", "library", "paint_a", "base_color.png")
        self.assertEqual(digest(os.path.join(folder, "T_PaintA_BaseColor.png")), digest(source))
        self.assertTrue(any(t.startswith("T_Sofa_") for t in output["textures"]))
        floor = bpy.data.materials["Living_Floor"]
        floor_files = {os.path.basename(n.image.filepath) for n in floor.node_tree.nodes
                       if n.type == "TEX_IMAGE" and n.image}
        self.assertIn("T_PaintA_BaseColor.png", floor_files)
        sources = {}
        for image in bpy.data.images:
            path = os.path.normcase(os.path.abspath(bpy.path.abspath(image.filepath)))
            if path.startswith(os.path.normcase(os.path.abspath(folder))):
                sources.setdefault(path, 0)
        self.assertEqual(len(sources), len(output["textures"]))
        self.assertEqual(len(set(t.lower() for t in output["textures"])), len(output["textures"]))

    # The FBX moved with its Textures folder still finds every texture, and the walls have the bounds of the build.
    def test_reimport_elsewhere(self):
        self.export(clean=True)
        moved = self.path("elsewhere", "export")
        shutil.copytree(self.path("export", ".staging"), moved)
        shutil.rmtree(self.path("export"))
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=moved + "/Square.fbx")
        textured = [i for i in bpy.data.images if i.filepath]
        self.assertTrue(textured)
        for image in textured:
            path = os.path.abspath(bpy.path.abspath(image.filepath))
            self.assertTrue(os.path.isfile(path), path)
            self.assertTrue(os.path.normcase(path).startswith(os.path.normcase(os.path.abspath(moved))), path)
        walls = bpy.data.objects["SM_f1_Walls"]
        low, high = scene.world_bounds([walls])
        for got, want in zip(list(low) + list(high), export_helpers.BOUNDS["min"] + export_helpers.BOUNDS["max"]):
            self.assertAlmostEqual(got, want, delta=1e-3)
        self.assertIn("UCX_SM_sofa_i1_00", [o.name for o in bpy.data.objects])

    # Returns every texture path string written in an FBX file, absolute and relative ones alike.
    def fbx_texture_paths(self, path):
        parser = importlib.import_module(bpy.types.EXPORT_SCENE_OT_fbx.__module__ + ".parse_fbx")
        root, _ = parser.parse(path)
        found = []

        def walk(elem):
            for prop in elem.props:
                if isinstance(prop, bytes):
                    text = prop.decode("utf-8", "replace")
                    if text.lower().endswith((".png", ".jpg", ".tga")):
                        found.append(text)
            for child in elem.elems:
                walk(child)

        walk(root)
        return found

    # After the staging folder is renamed and again after it is moved elsewhere, every texture path in the
    # FBX, absolute or relative, resolves to a file of the moved Textures folder.
    def test_fbx_paths_survive_rename(self):
        self.export(clean=True)
        final = self.path("export", "final")
        os.rename(self.path("export", ".staging"), final)
        moved = self.path("elsewhere2", "export")
        shutil.copytree(final, moved)
        shutil.rmtree(final)
        paths = self.fbx_texture_paths(moved + "/Square.fbx")
        self.assertTrue(paths)
        textures = os.path.normcase(os.path.abspath(os.path.join(moved, "Textures")))
        for text in paths:
            self.assertNotIn(".staging", text)
            full = text if os.path.isabs(text) else os.path.join(moved, text)
            full = os.path.normcase(os.path.abspath(full))
            self.assertTrue(os.path.isfile(full), text)
            self.assertEqual(os.path.dirname(full), textures, text)

    # Without the exporter's path function, or with a path mode other than RELATIVE, the wrapper does nothing.
    def test_wrapper_skipped_when_unavailable(self):
        from plynth.exporters import fbx
        package = bpy.types.EXPORT_SCENE_OT_fbx.__module__
        module = importlib.import_module(package + ".export_fbx_bin")
        original = module._gen_vid_path
        try:
            del module._gen_vid_path
            with fbx.relative_texture_names("RELATIVE"):
                pass
            self.assertFalse(hasattr(module, "_gen_vid_path"))
        finally:
            module._gen_vid_path = original
        with fbx.relative_texture_names("COPY"):
            self.assertIs(module._gen_vid_path, original)
        with fbx.relative_texture_names("RELATIVE"):
            self.assertIsNot(module._gen_vid_path, original)
        self.assertIs(module._gen_vid_path, original)

    # Every file in Textures is named by the FBX, so no unused map (such as occlusion) is copied.
    def test_only_used_textures_copied(self):
        result = self.export(clean=True)
        names = {os.path.basename(t) for t in self.fbx_texture_paths(self.path("export", ".staging", "Square.fbx"))}
        self.assertEqual(sorted(os.listdir(self.path("export", ".staging", "Textures"))), sorted(names))
        self.assertEqual(sorted(result["output"]["textures"]), sorted(names))
        self.assertFalse([n for n in names if "Occlusion" in n])

    # building.blend is never changed.
    def test_blend_untouched(self):
        project = fixture_project()
        path = project + "/build/building.blend"
        before = digest(path)
        self.export()
        self.assertEqual(digest(path), before)
        self.assertEqual(sorted(os.listdir(project + "/build")).count("building.blend1"), 0)

    # With write false nothing is written and every file list is empty.
    def test_check_only(self):
        result = self.export(write=False)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertFalse(output["written"])
        self.assertEqual(output["files"], [])
        self.assertEqual(output["textures"], [])
        self.assertGreater(output["objects"], 0)
        self.assertFalse(os.path.exists(self.path("export")))

    # A cube the user added by hand comes out named SM_Cube.
    def test_hand_added_cube(self):
        project = fixture_project()
        bpy.ops.wm.open_mainfile(filepath=project + "/build/building.blend")
        bpy.ops.mesh.primitive_cube_add(size=0.5, location=(2, -2, 3.5))
        bpy.context.active_object.name = "Cube"
        copy = self.path("hand", "building.blend")
        os.makedirs(os.path.dirname(copy))
        bpy.ops.wm.save_as_mainfile(filepath=copy, copy=True, relative_remap=False)
        result = self.export(blend_file=copy)
        self.assertTrue(result["ok"], result["error"])
        self.assertIn("SM_Cube", [o.name for o in bpy.data.objects])
        self.assertNotIn("Cube", [o.name for o in bpy.data.objects])
        self.assertIn("SM_f1_Walls", [o.name for o in bpy.data.objects])

    # A blend without anything the build made is no_geometry, and so is a missing file import_failed.
    def test_no_geometry(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.mesh.primitive_cube_add()
        copy = self.path("plain", "building.blend")
        os.makedirs(os.path.dirname(copy))
        bpy.ops.wm.save_as_mainfile(filepath=copy, copy=True)
        result = self.export(blend_file=copy)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "no_geometry")
        self.assert_result_valid(result)
        result = self.export(blend_file=self.path("nowhere", "building.blend"))
        self.assertEqual(result["error"]["code"], "import_failed")
        self.assert_result_valid(result)

    # A job without a field the task needs is bad_job.
    def test_bad_job(self):
        params = self.params()
        del params["groups"]
        result = self.export(params=params)
        self.assertEqual(result["error"]["code"], "bad_job")

    # A failing scene check blocks the files, with the check reported and the file check skipped. A forced job
    # writes anyway and the check still says fail.
    def test_gate(self):
        self.register_check("fake_scene", FakeCheck("scene", "fail").entry(1))
        self.register_check("fake_file", FakeCheck("file", "pass").entry(1))
        result = self.export()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertFalse(output["written"])
        self.assertEqual(fakes(output), [("fake_scene", "fail"), ("fake_file", "skipped")])
        self.assertEqual(output["checks"][0]["findings"][0]["plan_id"], "w1")
        self.assertEqual(output["files"], [])
        self.assertEqual(output["textures"], [])
        self.assertFalse(os.path.exists(self.path("export", ".staging", "Square.fbx")))
        self.assertFalse(os.path.exists(self.path("export", ".staging", "Textures")))
        forced = self.export(force=True)
        self.assert_result_valid(forced)
        self.assertTrue(forced["output"]["written"])
        self.assertEqual(fakes(forced["output"]), [("fake_scene", "fail"), ("fake_file", "pass")])
        self.assertTrue(os.path.isfile(self.path("export", ".staging", "Square.fbx")))

    # A warning does not block, and a check that raises fails with a plain sentence.
    def test_warning_and_crash(self):
        self.register_check("fake_warn", FakeCheck("scene", "warning").entry(1))

        def broken(ctx):
            raise RuntimeError("boom")

        self.register_check("fake_crash", quality.QualityCheck(2, "scene", "Crash", broken))
        output = self.export(force=False)["output"]
        fake = [c for c in output["checks"] if c["id"].startswith("fake_")]
        self.assertEqual([c["status"] for c in fake], ["warning", "fail"])
        self.assertIn("boom", fake[1]["message"])
        self.assertFalse(output["written"])

    # Findings are capped at 50 and the message says how many there were.
    def test_findings_cap(self):
        answer = quality.result("fail", "Too many.", [quality.finding("x") for _ in range(120)])
        self.assertEqual(len(answer["findings"]), 50)
        self.assertIn("120", answer["message"])

    # Progress lines come once per step, in order.
    def test_progress(self):
        self.export(write=False)
        lines = [json.loads(line[len("PLYNTH:"):])["progress"] for line in self.printed.splitlines()
                 if line.startswith("PLYNTH:")]
        self.assertEqual([p["step"] for p in lines], STEPS)
        self.assertEqual([p["index"] for p in lines], list(range(1, 8)))

    # Groups that take only some kinds split the objects, and a collision mesh follows its object.
    def test_groups(self):
        params = self.params(clean=True)
        params["groups"] = [{"id": "furniture", "file": "Furniture.fbx", "kinds": ["item"], "ids": []},
                            {"id": "shell", "file": "Shell.fbx", "kinds": ["walls", "floor", "ceiling", "threshold",
                                                                          "fill"], "ids": []}]
        result = self.export(params=params)
        self.assertTrue(result["ok"], result["error"])
        files = {f["group"]: f for f in result["output"]["files"]}
        self.assertEqual(files["furniture"]["objects"], 4)
        self.assertGreater(files["shell"]["objects"], 5)


if __name__ == "__main__":
    unittest.main()
