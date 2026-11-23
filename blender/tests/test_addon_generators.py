import math
import os
import re
import unittest

import bpy
from mathutils import Vector

import helpers
from generator_helpers import SAMPLES, GeneratorTest, generator_slot_base, job_item
from test_bridges import skip_reason

TOLERANCE = 0.01

# Generator id, file, add-on short names to check, label, params, size, location.
CASES = {
    "archimesh_column": ("archimesh_column.py", ("archimesh",), "Archimesh",
                         {"width": 0.4, "height": 3.0, "shape": "round", "material": "white_paint"},
                         (0.4, 0.4, 3.0), [0.6, -0.6, 0.0]),
    "home_builder_cabinets": ("home_builder_cabinets.py", ("home_builder_5", "home_builder"), "Home Builder 5",
                              {"length": 1.8, "depth": 0.6, "height": 0.87, "material": "white_paint"},
                              (1.8, 0.6, 0.87), [2.4, -0.5, 0.0]),
    "bool_tool_screen": ("bool_tool_screen.py", ("bool_tool",), "Bool Tool",
                         {"width": 1.2, "height": 2.0, "thickness": 0.05, "slots": 5, "material": "white_paint"},
                         (1.2, 0.05, 2.0), [1.0, -2.0, 0.0]),
    "bagapie_fence": ("bagapie_fence.py", ("bagapie",), "Bagapie",
                      {"length": 3.0, "height": 1.0, "post_spacing": 1.0, "material": "brushed_steel"},
                      (3.0, 0.15, 1.0), [2.0, -3.4, 0.0]),
}


class AddonGeneratorTests(GeneratorTest):
    # Returns the params of a room with the given generators built from the samples folder.
    def case_params(self, names):
        files = {}
        items = []
        for name in names:
            file, _, _, params, size, location = CASES[name]
            files[name] = os.path.join(SAMPLES, file)
            items.append(job_item("i_" + name, name, dict(params), location,
                                  size=dict(zip(("width", "depth", "height"), size))))
        params = self.room_params(files, items)
        params["materials"] = [self.make_material("brushed_steel"), self.make_material("white_paint")]
        return params

    # Skips the test when the add-on of a case cannot be used.
    def need(self, name):
        reason = skip_reason(CASES[name][1], CASES[name][2])
        if reason:
            self.skipTest(reason)

    # Builds one generator in a fixture job and checks its object, size and its one material slot.
    def check_case(self, name):
        self.need(name)
        result = self.build_open(self.case_params([name]))
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["generators"], 1)
        obj = bpy.data.objects["SM_%s_i_%s" % (name, name)]
        self.assertEqual(obj.type, "MESH")
        self.assertEqual(len(obj.modifiers), 0)
        size = CASES[name][4]
        points = [v.co for v in obj.data.vertices]
        self.assertAlmostEqual(min(p.z for p in points), 0.0, delta=1e-4)
        found = tuple(max(p[a] for p in points) - min(p[a] for p in points) for a in range(3))
        for axis in range(3):
            self.assertLessEqual(found[axis], size[axis] + TOLERANCE, "axis %d of %s" % (axis, found))
        self.assertGreater(found[0], size[0] * 0.9)
        self.assertGreater(found[2], size[2] * 0.9)
        allowed = {generator_slot_base(name, "material")}
        self.assertEqual({m.name for m in obj.data.materials}, allowed)
        used = {obj.data.materials[p.material_index].name for p in obj.data.polygons}
        self.assertEqual(used, allowed)
        return obj

    def test_archimesh_column(self):
        self.check_case("archimesh_column")

    def test_home_builder_cabinets(self):
        self.check_case("home_builder_cabinets")

    def test_bool_tool_screen(self):
        obj = self.check_case("bool_tool_screen")
        self.assertGreater(len(obj.data.polygons), 6 + 5 * 4)

    def test_bagapie_fence(self):
        self.check_case("bagapie_fence")

    # With the add-on name changed in a copy of the file, the build warns and makes nothing, with no error.
    def test_missing_addon(self):
        for name, case in CASES.items():
            with open(os.path.join(SAMPLES, case[0]), encoding="utf-8") as handle:
                text = handle.read()
            changed = re.sub(r'"addons": \[[^\]]*\]', '"addons": ["plynth_test_missing"]', text, count=1)
            self.assertNotEqual(text, changed)
            copy = self.path("copies", case[0])
            os.makedirs(os.path.dirname(copy), exist_ok=True)
            with open(copy, "w", encoding="utf-8") as handle:
                handle.write(changed)
            _, _, _, params, size, location = case
            params = self.room_params({name: copy}, [job_item("i1", name, dict(params), location,
                                                              size=dict(zip(("width", "depth", "height"), size)))])
            params["materials"] = [self.make_material("brushed_steel"), self.make_material("white_paint")]
            result = self.build_open(params)
            self.assertEqual(len(self.problems(result, "generator_unavailable")), 1, name)
            self.assertEqual(self.problems(result, "generator_failed"), [], name)
            self.assertEqual(result["output"]["counts"]["generators"], 0)
            self.assertIsNone(bpy.data.objects.get("SM_%s_i1" % name))

    # The four add-on generators and the two plain ones together: builds, and renders a picture when
    # PLYNTH_RENDER_OUT names a file.
    def test_all_together(self):
        available = [n for n in CASES if not skip_reason(CASES[n][1], CASES[n][2])]
        if not available:
            self.skipTest("no add-on is available")
        result = self.build_open(self.case_params(available))
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["generators"], len(available))
        target = os.environ.get("PLYNTH_RENDER_OUT")
        if target:
            render(target)
            self.assertTrue(os.path.getsize(target) > 0)


# Hides the walls and ceiling, adds a camera and lights, and renders a still with Eevee to the file.
def render(target):
    scene = bpy.context.scene
    for obj in bpy.data.objects:
        if obj.type == "MESH" and obj.get("plynth_kind") != "generator":
            if max(v.co.z for v in obj.data.vertices) > 1.0:
                obj.hide_render = True
    centre = Vector((2.0, -2.0, 0.6))
    camera = bpy.data.objects.new("test_camera", bpy.data.cameras.new("test_camera"))
    camera.data.lens = 30
    camera.location = centre + Vector((2.5, 4.2, 4.0))
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(camera)
    scene.camera = camera
    sun = bpy.data.objects.new("test_sun", bpy.data.lights.new("test_sun", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(50), 0.0, math.radians(30))
    scene.collection.objects.link(sun)
    world = bpy.data.worlds.new("test_world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.6, 0.65, 0.7, 1.0)
    scene.world = world
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = 1280, 800
    scene.render.filepath = target
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    unittest.main()
