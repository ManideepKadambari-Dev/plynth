import importlib
import os
import unittest

import bpy

from export_helpers import ExportTest, fixture_project, read_names
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

    # Returns the names of the light objects written in an FBX file, read from the file itself.
    def fbx_light_names(self, path):
        parser = importlib.import_module(bpy.types.EXPORT_SCENE_OT_fbx.__module__ + ".parse_fbx")
        root, _ = parser.parse(path)
        found = []

        def walk(elem):
            if elem.id == b"Model" and b"Light" in elem.props[2:]:
                found.append(elem.props[1].split(b"\x00")[0].decode("utf-8", "replace"))
            for child in elem.elems:
                walk(child)

        walk(root)
        return found

    # The plain export of a build with a light leaves the light out: the file holds only meshes, its object
    # count matches the export's, and Blender's own FBX import opens it. The unreal export keeps the light in its
    # file, apart from the joined mesh, and the file check reads it back.
    def test_light_only_in_unreal(self):
        blend = self.blend_with_light()
        target = self.path("export", ".staging", "Square.fbx")
        result = self.export(blend_file=blend, clean=True, profile="plain")
        self.assertTrue(result["ok"], result["error"])
        output = result["output"]
        checks = {c["id"]: c for c in output["checks"]}
        self.assertEqual(checks["fbx_scale"]["status"], "pass", checks["fbx_scale"]["findings"])
        self.assertTrue(output["written"])
        self.assertEqual(self.fbx_light_names(target), [])
        self.assertEqual([f["objects"] for f in output["files"]], [output["objects"]])
        self.assertNotIn("Light_f1_l1", [o.name for o in bpy.data.objects])
        self.assertEqual(len(read_names(target)), output["objects"])

        result = self.export(blend_file=blend, clean=True, profile="unreal")
        self.assertTrue(result["ok"], result["error"])
        checks = {c["id"]: c for c in result["output"]["checks"]}
        self.assertEqual(checks["fbx_scale"]["status"], "pass", checks["fbx_scale"]["findings"])
        self.assertEqual(self.fbx_light_names(target), ["Light_f1_l1"])
        self.assertIn("Light_f1_l1", [o.name for o in bpy.data.objects if o.type == "LIGHT"])
        for group in fbx_scale.light_setting_groups():
            self.assertNotIn("cast_shadow", group.bl_rna.properties, group.__name__)

if __name__ == "__main__":
    unittest.main()
