import hashlib
import os
import unittest

import bpy

from cleanup_helpers import BACKGROUND, CleanupTest
from plynth import registry, scene
from plynth import exporters
from plynth.exporters import fbx  # noqa: F401


class ExporterTests(CleanupTest):
    # Cleans the chair and returns the result.
    def clean_chair(self, **overrides):
        chair = self.copy_chair()
        return self.cleanup(os.path.join(chair, "chair.obj"), "obj", **overrides)

    # The fbx exporter is registered with its extension.
    def test_registered(self):
        entry = registry.EXPORTERS.get("fbx")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.extension, ".fbx")

    # Cleanup writes asset.fbx, reports it and leaves no temporary file.
    def test_cleanup_writes_fbx(self):
        result = self.clean_chair()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["fbx_file"], "asset.fbx")
        self.assertEqual(result["warnings"], [])
        self.assertTrue(os.path.isfile(self.path("item", "asset.fbx")))
        self.assertFalse([n for n in os.listdir(self.path("item")) if ".part" in n])

    # asset.fbx imports into an empty scene with the same size and finds its texture.
    def test_fbx_reimports(self):
        self.clean_chair()
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("item", "asset.fbx"))
        objects = scene.mesh_objects(scene.scene_objects())
        self.assertEqual(len(objects), 6)
        low, high = scene.world_bounds(objects)
        size = high - low
        self.assertAlmostEqual(size.x, 0.45, delta=1e-3)
        self.assertAlmostEqual(size.y, 0.50, delta=1e-3)
        self.assertAlmostEqual(size.z, 0.90, delta=1e-3)
        found = [i for i in bpy.data.images if i.source == "FILE" and "chair_wood" in i.filepath]
        self.assertTrue(found)
        path = os.path.abspath(bpy.path.abspath(found[0].filepath, library=None))
        self.assertTrue(os.path.isfile(path), path)

    # render_thumbnail with fbx_file writes it and leaves asset.blend unchanged.
    def test_render_thumbnail_writes_fbx(self):
        self.clean_chair()
        blend = self.path("item", "asset.blend")
        before = hashlib.sha256(open(blend, "rb").read()).hexdigest()
        target = self.path("item", "again.fbx")
        spec = {"render": False, "size": 64, "background": BACKGROUND}
        result = self.run_task("render_thumbnail", {"blend_file": blend, "output_file": self.path("t.png"),
                                                   "measure": False, "fbx_file": target, "thumbnail": spec})
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["fbx_file"], "again.fbx")
        self.assertTrue(os.path.isfile(target))
        self.assertEqual(hashlib.sha256(open(blend, "rb").read()).hexdigest(), before)

    # With fbx_file null nothing is written.
    def test_render_thumbnail_without_fbx(self):
        self.clean_chair()
        os.remove(self.path("item", "asset.fbx"))
        spec = {"render": False, "size": 64, "background": BACKGROUND}
        result = self.run_task("render_thumbnail", {"blend_file": self.path("item", "asset.blend"),
                                                   "output_file": self.path("t.png"), "measure": False,
                                                   "fbx_file": None, "thumbnail": spec})
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["fbx_file"])
        self.assertFalse(os.path.exists(self.path("item", "asset.fbx")))
        self.assert_result_valid(result)

    # A path that cannot be written gives fbx_failed in the retry task and the job still succeeds.
    def test_bad_export_path(self):
        self.clean_chair()
        spec = {"render": False, "size": 64, "background": BACKGROUND}
        bad = self.path("item", "asset.blend", "x.fbx")
        result = self.run_task("render_thumbnail", {"blend_file": self.path("item", "asset.blend"),
                                                   "output_file": self.path("t.png"), "measure": False,
                                                   "fbx_file": bad, "thumbnail": spec})
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["fbx_file"])
        self.assertEqual(self.codes(result), ["fbx_failed"])
        self.assert_result_valid(result)

    # A failing exporter gives fbx_failed in cleanup and the cleanup still succeeds.
    def test_cleanup_survives_failed_export(self):
        original = registry.EXPORTERS.get("fbx")

        def broken(objects, filepath, path_mode):
            raise RuntimeError("no disk")

        registry.EXPORTERS._entries["fbx"] = exporters.ExporterEntry(".fbx", broken)
        try:
            result = self.clean_chair()
        finally:
            registry.EXPORTERS._entries["fbx"] = original
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["fbx_file"])
        self.assertEqual(self.codes(result), ["fbx_failed"])
        self.assertTrue(os.path.isfile(self.path("item", "asset.blend")))
        self.assertFalse(os.path.exists(self.path("item", "asset.fbx")))
        self.assert_result_valid(result)


if __name__ == "__main__":
    unittest.main()
