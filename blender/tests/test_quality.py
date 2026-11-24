import os
import time
import unittest

import bmesh
import bpy

from export_helpers import ExportTest
from plynth import quality, registry

CHECK_IDS = ["build_current", "build_complete", "scale", "transforms", "naming", "uvs", "texel_density",
             "triangles", "materials", "collision", "groups", "fbx_scale", "kit_file"]


# Returns the L shaped prism used as a concave collision mesh.
def l_prism_mesh():
    outline = [(0, 0), (2, 0), (2, 1), (1, 1), (1, 2), (0, 2)]
    bm = bmesh.new()
    bottom = [bm.verts.new((x, y, 0.0)) for x, y in outline]
    top = [bm.verts.new((x, y, 1.0)) for x, y in outline]
    bm.faces.new(list(reversed(bottom)))
    bm.faces.new(top)
    for index in range(len(outline)):
        following = (index + 1) % len(outline)
        bm.faces.new((bottom[index], bottom[following], top[following], top[index]))
    mesh = bpy.data.meshes.new("l_prism")
    bm.to_mesh(mesh)
    bm.free()
    return mesh


# Returns the Principled base color image node of a material.
def base_color_node(material):
    shader = [n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED"][0]
    return shader.inputs["Base Color"].links[0].from_node


class QualityTests(ExportTest):
    # Registers a scene check that runs before every other check and changes the scene.
    def inject(self, fault):
        registry.discover(quality)

        def run(ctx):
            fault(ctx)
            return quality.result("pass", "Fault made.")

        registry.QUALITY_CHECKS._entries.pop("fault", None)
        registry.QUALITY_CHECKS.register("fault", quality.QualityCheck(0, "scene", "Fault", run))
        self.addCleanup(registry.QUALITY_CHECKS._entries.pop, "fault", None)

    # Exports with an optional fault and returns the checks by id, after checking the result against the schema.
    def checks(self, fault=None, params=None, clean=True, **kwargs):
        registry.QUALITY_CHECKS._entries.pop("fault", None)
        if fault is not None:
            self.inject(fault)
        if params is None:
            params = self.params(clean=clean, **kwargs)
        result = self.run_task("export", params, job_id="export-1")
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.output = result["output"]
        for check in self.output["checks"]:
            if check["id"] == "fault":
                self.assertEqual(check["status"], "pass", check["message"])
        return {c["id"]: c for c in self.output["checks"] if c["id"] != "fault"}

    # Asserts that the one check has the status and that no other check of the list changed.
    def assert_only(self, checks, check_id, status):
        self.assertEqual(checks[check_id]["status"], status, checks[check_id])
        for other in CHECK_IDS:
            if other != check_id and other in checks and other not in ("fbx_scale", "kit_file"):
                self.assertEqual(checks[other]["status"], "pass", checks[other])
        if check_id != "kit_file":
            self.assertEqual(checks["kit_file"]["status"], "skipped", checks["kit_file"])

    # Returns the exported object with the name.
    def obj(self, ctx, name):
        for o in ctx.state["meshes"] + ctx.state["collision"]:
            if o.name == name:
                return o
        raise KeyError(name)

    # A clean export passes the eleven checks in order, and the files are written.
    def test_clean(self):
        started = time.monotonic()
        checks = self.checks()
        elapsed = time.monotonic() - started
        self.assertEqual([c["id"] for c in self.output["checks"]], CHECK_IDS)
        for check_id in CHECK_IDS:
            expected = "skipped" if check_id == "kit_file" else "pass"
            self.assertEqual(checks[check_id]["status"], expected, checks[check_id])
            self.assertEqual(checks[check_id]["findings"], [])
        self.assertEqual(checks["fbx_scale"]["phase"], "file")
        self.assertEqual(checks["naming"]["phase"], "scene")
        self.assertTrue(self.output["written"])
        steps = {s["step"]: s["ms"] for s in self.output["steps"]}
        print("export of the fixture: %.2f s in all, steps %s" % (elapsed, steps))

    # The default fixture has slots without a material, so the materials check names them and nothing is written.
    def test_neutral_slots(self):
        checks = self.checks(clean=False)
        self.assertEqual(checks["materials"]["status"], "fail")
        named = {f["object"] for f in checks["materials"]["findings"]}
        self.assertTrue({"Living_Floor", "Exterior_Wall", "Wall_Top"} <= named, named)
        self.assertFalse(self.output["written"])
        self.assertEqual(checks["fbx_scale"]["status"], "skipped")
        for check_id in ("build_current", "build_complete", "scale", "transforms", "naming", "triangles",
                         "collision"):
            self.assertEqual(checks[check_id]["status"], "pass", checks[check_id])

    # A plan changed since the build fails build_current, and a missing report does too.
    def test_build_current(self):
        params = self.params(clean=True)
        params["build"]["plan_changed"] = True
        checks = self.checks(params=params)
        self.assert_only(checks, "build_current", "fail")
        self.assertIn("changed", checks["build_current"]["message"])
        params = self.params(clean=True)
        params["build"] = {"report_found": False, "plan_changed": True, "problems": []}
        self.assertEqual(self.checks(params=params)["build_current"]["status"], "fail")

    # A build error fails build_complete with its plan id. Warnings do not.
    def test_build_complete(self):
        params = self.params(clean=True)
        params["build"]["problems"] = [
            {"code": "missing_asset", "severity": "error", "message": "The asset sofa is missing.", "floor": "f1",
             "object": "i3", "step": "assets"},
            {"code": "material_failed", "severity": "warning", "message": "A map did not load.", "floor": None,
             "object": None, "step": "materials"}]
        self.assert_job_valid(params)
        checks = self.checks(params=params)
        self.assert_only(checks, "build_complete", "fail")
        self.assertEqual([f["plan_id"] for f in checks["build_complete"]["findings"]], ["i3"])

    # A scene with unit scale 2 fails scale, and so does a scene moved away from the plan.
    def test_scale(self):
        def scaled(ctx):
            bpy.context.scene.unit_settings.scale_length = 2.0

        checks = self.checks(scaled)
        self.assert_only(checks, "scale", "fail")
        self.assertIn("unit scale", checks["scale"]["findings"][0]["message"])

        def moved(ctx):
            walls = self.obj(ctx, "SM_f1_Walls")
            for vertex in walls.data.vertices:
                vertex.co.x += 0.5

        checks = self.checks(moved)
        self.assertEqual(checks["scale"]["status"], "fail")
        self.assertEqual(checks["scale"]["findings"][0]["plan_id"], "f1")

    # An object with a parent or a transform fails transforms.
    def test_transforms(self):
        def parented(ctx):
            self.obj(ctx, "SM_f1_Walls").parent = self.obj(ctx, "SM_f1_r1_Floor")

        checks = self.checks(parented)
        self.assertEqual(checks["transforms"]["status"], "fail")
        self.assertEqual(checks["transforms"]["findings"][0]["object"], "SM_f1_Walls")

        def moved(ctx):
            self.obj(ctx, "SM_sofa_i1").location.x = 1.0

        checks = self.checks(moved)
        self.assertEqual(checks["transforms"]["status"], "fail")
        self.assertEqual(checks["transforms"]["findings"][0]["plan_id"], "i1")

    # A mesh without the prefix, a repeated name and a bad character fail naming.
    def test_naming(self):
        def renamed(ctx):
            self.obj(ctx, "SM_f1_Walls").name = "Wall"

        checks = self.checks(renamed)
        self.assertEqual(checks["naming"]["status"], "fail")
        messages = " ".join(f["message"] for f in checks["naming"]["findings"])
        self.assertIn("does not start with SM_", messages)
        self.assertIn("which is not exported", messages)

        def repeated(ctx):
            self.obj(ctx, "SM_sofa_i1").name = "SM_Dup"
            self.obj(ctx, "SM_sofa_i2").name = "sm_dup"

        checks = self.checks(repeated)
        self.assertEqual(checks["naming"]["status"], "fail")
        self.assertTrue(any("used twice" in f["message"] for f in checks["naming"]["findings"]))

        def odd(ctx):
            self.obj(ctx, "SM_sofa_i2").name = "SM_sofa i2"
            ctx.state["materials"]["Bad Slot"] = ctx.state["materials"]["Living_Wall"]

        findings = self.checks(odd)["naming"]["findings"]
        self.assertTrue(any("SM_sofa i2" in f["message"] for f in findings))
        self.assertTrue(any(f["object"] == "Bad Slot" for f in findings))

    # A textured mesh without UVs fails uvs, a face with no UV area and overlapping furniture UVs warn.
    def test_uvs(self):
        def no_uvs(ctx):
            mesh = self.obj(ctx, "SM_f1_Walls").data
            while mesh.uv_layers:
                mesh.uv_layers.remove(mesh.uv_layers[0])

        checks = self.checks(no_uvs)
        self.assert_only(checks, "uvs", "fail")
        self.assertEqual(checks["uvs"]["findings"][0]["object"], "SM_f1_Walls")

        def flat_face(ctx):
            mesh = self.obj(ctx, "SM_f1_Thresholds").data
            for loop in mesh.polygons[0].loop_indices:
                mesh.uv_layers[0].uv[loop].vector = (0.5, 0.5)

        checks = self.checks(flat_face)
        self.assert_only(checks, "uvs", "warning")
        self.assertIn("no area", checks["uvs"]["findings"][0]["message"])

        def overlapping(ctx):
            mesh = self.obj(ctx, "SM_sofa_i1").data
            layer = mesh.uv_layers[0]
            first, second = mesh.polygons[0], mesh.polygons[1]
            self.assertEqual(first.loop_total, second.loop_total)
            for a, b in zip(first.loop_indices, second.loop_indices):
                layer.uv[b].vector = layer.uv[a].vector

        checks = self.checks(overlapping)
        self.assertEqual(checks["uvs"]["status"], "warning")
        self.assertTrue(any("overlap" in f["message"] for f in checks["uvs"]["findings"]))

    # A 64 pixel base color among 1024 pixel ones fails texel_density.
    def test_texel_density(self):
        def images(ctx):
            sizes = {"Living_Wall": 1024, "Living_Ceiling": 1024, "Living_Floor": 1024, "Exterior_Wall": 1024,
                     "Wall_Top": 64}
            for name, size in sizes.items():
                node = base_color_node(ctx.state["materials"][name]["material"])
                node.image = bpy.data.images.new("generated_%s" % name, size, size)

        checks = self.checks(images)
        density = checks["texel_density"]
        self.assertEqual(density["status"], "fail", density)
        self.assertEqual([f["object"] for f in density["findings"]], ["Wall_Top"])

        def soft(ctx):
            for name in ("Living_Wall", "Living_Ceiling"):
                node = base_color_node(ctx.state["materials"][name]["material"])
                node.image = bpy.data.images.new("generated_%s" % name, 128, 128)

        checks = self.checks(soft)
        self.assertEqual(checks["texel_density"]["status"], "warning", checks["texel_density"])

    # An empty mesh fails triangles, and a low limit warns.
    def test_triangles(self):
        def empty(ctx):
            self.obj(ctx, "SM_f1_Thresholds").data.clear_geometry()

        checks = self.checks(empty)
        self.assertEqual(checks["triangles"]["status"], "fail")
        self.assertEqual(checks["triangles"]["findings"][0]["object"], "SM_f1_Thresholds")
        params = self.params(clean=True)
        params["limits"]["object_triangles"] = 10
        checks = self.checks(params=params)
        self.assert_only(checks, "triangles", "warning")
        params = self.params(clean=True)
        params["limits"]["total_triangles"] = 10
        checks = self.checks(params=params)
        self.assert_only(checks, "triangles", "warning")
        self.assertIn("building", checks["triangles"]["findings"][0]["message"])

    # A face on an empty slot, a deleted texture file and a noise base color fail materials.
    def test_materials(self):
        def empty_slot(ctx):
            mesh = self.obj(ctx, "SM_f1_Walls").data
            mesh.materials[1] = None

        checks = self.checks(empty_slot)
        self.assertEqual(checks["materials"]["status"], "fail")
        self.assertTrue(any("empty material slot" in f["message"] for f in checks["materials"]["findings"]))

        def deleted(ctx):
            node = base_color_node(ctx.state["materials"]["Living_Wall"]["material"])
            os.remove(bpy.path.abspath(node.image.filepath))

        checks = self.checks(deleted)
        self.assertEqual(checks["materials"]["status"], "fail")
        self.assertTrue(any("is missing" in f["message"] for f in checks["materials"]["findings"]))

        def noise(ctx):
            material = ctx.state["materials"]["Living_Wall"]["material"]
            tree = material.node_tree
            shader = [n for n in tree.nodes if n.type == "BSDF_PRINCIPLED"][0]
            texture = tree.nodes.new("ShaderNodeTexNoise")
            tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])

        checks = self.checks(noise)
        self.assertEqual(checks["materials"]["status"], "fail")
        finding = checks["materials"]["findings"][0]
        self.assertEqual(finding["object"], "Living_Wall")
        self.assertIn("cannot turn into image maps", finding["message"])

    # A missing UCX, an open one and a concave one fail collision.
    def test_collision(self):
        def removed(ctx):
            target = self.obj(ctx, "UCX_SM_sofa_i1_00")
            ctx.state["collision"].remove(target)
            bpy.data.objects.remove(target, do_unlink=True)

        checks = self.checks(removed)
        self.assertEqual(checks["collision"]["status"], "fail")
        self.assertEqual(checks["collision"]["findings"][0]["object"], "SM_sofa_i1")
        self.assertEqual(checks["collision"]["findings"][0]["plan_id"], "i1")

        def concave(ctx):
            self.obj(ctx, "UCX_SM_f1_Walls_00").data = l_prism_mesh()

        checks = self.checks(concave)
        self.assertEqual(checks["collision"]["status"], "fail")
        self.assertIn("not convex", checks["collision"]["findings"][0]["message"])

        def opened(ctx):
            mesh = self.obj(ctx, "UCX_SM_f1_Walls_01").data
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bm.faces.ensure_lookup_table()
            bmesh.ops.delete(bm, geom=[bm.faces[0]], context="FACES_ONLY")
            bm.to_mesh(mesh)
            bm.free()

        checks = self.checks(opened)
        self.assertEqual(checks["collision"]["status"], "fail")
        self.assertIn("not closed", checks["collision"]["findings"][0]["message"])

    # A generator object needs its UCX like an item, and the pass message names generators.
    def test_generator_collision(self):
        def generator(ctx):
            self.obj(ctx, "SM_sofa_i1")["plynth_kind"] = "generator"

        checks = self.checks(generator)
        self.assert_only(checks, "collision", "pass")
        self.assertIn("generator", checks["collision"]["message"])

        def bare(ctx):
            generator(ctx)
            target = self.obj(ctx, "UCX_SM_sofa_i1_00")
            ctx.state["collision"].remove(target)
            bpy.data.objects.remove(target, do_unlink=True)

        checks = self.checks(bare)
        self.assertEqual(checks["collision"]["status"], "fail")
        self.assertEqual([(f["object"], f["plan_id"]) for f in checks["collision"]["findings"]],
                         [("SM_sofa_i1", "i1")])
        self.assertIn("no collision mesh", checks["collision"]["findings"][0]["message"])
        self.assertFalse(self.output["written"])

    # With write false the file check is skipped.
    def test_check_only(self):
        checks = self.checks(write=False)
        self.assertEqual(checks["fbx_scale"]["status"], "skipped")
        self.assertEqual(checks["kit_file"]["status"], "skipped")
        for check_id in CHECK_IDS[:-2]:
            self.assertEqual(checks[check_id]["status"], "pass", checks[check_id])
        self.assertFalse(self.output["written"])

    # With force the files are written and the failing checks still say fail.
    def test_force(self):
        def renamed(ctx):
            self.obj(ctx, "SM_f1_Walls").name = "Wall"

        checks = self.checks(renamed, force=True)
        self.assertTrue(self.output["written"])
        self.assertEqual(checks["naming"]["status"], "fail")
        self.assertEqual(checks["fbx_scale"]["status"], "pass", checks["fbx_scale"])
        self.assertTrue(os.path.isfile(self.path("export", ".staging", "Square.fbx")))

    # The file check fails when the written walls do not have the planned size.
    def test_fbx_scale_mismatch(self):
        params = self.params(clean=True)
        params["expected_bounds"] = {"min": [-0.1, -4.1, 0.0], "max": [4.1, 0.1, 3.5]}
        params["force"] = True
        checks = self.checks(params=params)
        self.assertEqual(checks["scale"]["status"], "fail")
        self.assertEqual(checks["fbx_scale"]["status"], "fail")
        self.assertIn("Square.fbx", checks["fbx_scale"]["findings"][0]["message"])

    # Findings never pass 50 and the message says how many there were.
    def test_findings_cap(self):
        params = self.params(clean=True)
        params["build"]["problems"] = [
            {"code": "missing_asset", "severity": "error", "message": "Item %d is missing." % number, "floor": "f1",
             "object": "i%d" % number, "step": "assets"} for number in range(120)]
        checks = self.checks(params=params)
        self.assertEqual(len(checks["build_complete"]["findings"]), 50)
        self.assertIn("120", checks["build_complete"]["message"])

    # Every check is its own registry entry with a name, a phase and an order.
    def test_registry(self):
        registry.discover(quality)
        entries = dict(registry.QUALITY_CHECKS.all())
        ordered = sorted(entries, key=lambda key: entries[key].order)
        self.assertEqual(ordered, CHECK_IDS)
        for entry in entries.values():
            self.assertTrue(entry.name)
            self.assertIn(entry.phase, ("scene", "file"))


if __name__ == "__main__":
    unittest.main()
