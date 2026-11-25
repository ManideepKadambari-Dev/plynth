import os
import unittest

import bpy

from export_helpers import ExportTest, fixture_project
from plynth.quality import fbx_scale


class ExportLightTests(ExportTest):
    # Saves a copy of the shared build with clean materials and a ceiling light marker added, the way the build
    # makes one.
    def blend_with_light(self):
        project = fixture_project(clean=True)
        bpy.ops.wm.open_mainfile(filepath=project + "/build/building.blend")
        light = bpy.data.objects.new("Light_f1_l1", bpy.data.lights.new("Light_f1_l1", "POINT"))
        light.location = (2.0, -2.0, 2.9)
        light["plynth_id"] = "l1"
        light["plynth_kind"] = "light"
        bpy.context.scene.collection.objects.link(light)
        copy = self.path("lit", "building.blend")
        os.makedirs(os.path.dirname(copy))
        bpy.ops.wm.save_as_mainfile(filepath=copy, copy=True, relative_remap=False)
        return copy

    # A build with a light exports, and the written file reads back with the light counted, so the file check
    # passes.
    def test_light_reads_back(self):
        result = self.export(blend_file=self.blend_with_light(), clean=True)
        self.assertTrue(result["ok"], result["error"])
        checks = {c["id"]: c for c in result["output"]["checks"]}
        self.assertEqual(checks["fbx_scale"]["status"], "pass", checks["fbx_scale"]["findings"])
        for group in fbx_scale.light_setting_groups():
            self.assertNotIn("cast_shadow", group.bl_rna.properties, group.__name__)


if __name__ == "__main__":
    unittest.main()
