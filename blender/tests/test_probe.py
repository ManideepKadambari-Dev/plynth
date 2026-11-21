import json
import unittest

import helpers
from plynth.tasks import probe


class ProbeTests(helpers.TempFolderTest):
    # The probe result is valid and reports the running Blender.
    def test_probe_result_is_valid(self):
        import bpy
        result = self.run_task("probe", {})
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        blender = result["output"]["blender"]
        self.assertEqual(blender["version"], ".".join(str(part) for part in bpy.app.version))
        self.assertEqual(result["blender_version"], blender["version"])
        self.assertGreater(len(blender["python"]), 0)

    # The probe never writes anything that looks like a key, and says only whether one is stored.
    def test_no_key_in_result(self):
        result = self.run_task("probe", {})
        self.assertNotIn("api_key", json.dumps(result))
        self.assertIsInstance(result["output"]["blenderkit"]["signed_in"], bool)

    # Module names are matched by their last dotted part.
    def test_short_name(self):
        self.assertEqual(probe.short_name("bl_ext.user_default.blenderkit"), "blenderkit")
        self.assertEqual(probe.short_name("archimesh"), "archimesh")


if __name__ == "__main__":
    unittest.main()
