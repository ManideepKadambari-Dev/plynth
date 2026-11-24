"""S-R 6 and the autoexec control, real Blender. Run alone on the owner's PC:
    blender -b --factory-startup --python tests/run_tests.py -- test_blenderkit_autoexec.py
A .blend whose driver writes a marker is opened in a child Blender whose saved preferences allow auto run,
without the fetch guard. --disable-autoexec must win; without it the marker appears, which proves the check.
"""
import os
import unittest

import bpy

from blenderkit_edge_helpers import EdgeTest, run_blender

TURN_ON = ("import bpy; bpy.context.preferences.filepaths.use_scripts_auto_execute = True; "
           "bpy.ops.wm.save_userpref(); print('AUTO RUN SAVED')")
EVALUATE = "import bpy; bpy.context.scene.frame_set(2); bpy.context.view_layer.update(); print('EVALUATED')"


# Saves a .blend with an object whose driver writes the marker file when Python drivers may run.
def make_driver_blend(path, marker):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    holder = bpy.data.objects.new("Holder", None)
    bpy.context.scene.collection.objects.link(holder)
    driver = holder.driver_add("location", 0).driver
    driver.type = "SCRIPTED"
    driver.expression = "(open(%r, 'w').write('ran') and 0) + frame" % marker.replace("\\", "/")
    variable = driver.variables.new()
    variable.name = "frame"
    variable.targets[0].id_type = "SCENE"
    variable.targets[0].id = bpy.context.scene
    variable.targets[0].data_path = "frame_current"
    bpy.ops.wm.save_as_mainfile(filepath=path, copy=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)


class AutoexecTests(EdgeTest):
    # Turns auto run on in the scratch user's saved preferences only, and makes the driver file here.
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        done = run_blender(cls.env, ["--python-expr", TURN_ON])
        if "AUTO RUN SAVED" not in done.stdout:
            raise RuntimeError("Saving the scratch preferences failed.")
        cls.marker = os.path.join(cls.root, "ran.txt")
        cls.blend = os.path.join(cls.root, "driver.blend")
        make_driver_blend(cls.blend, cls.marker)

    # Opens the driver file in a child Blender and says whether the marker was written.
    def open_with_driver(self, extra):
        if os.path.exists(self.marker):
            os.remove(self.marker)
        done = run_blender(self.env, list(extra) + [self.blend, "--python-expr", EVALUATE])
        self.assertIn("EVALUATED", done.stdout, done.stdout[-2000:])
        return os.path.exists(self.marker)

    # The argument core passes stops scripts although the saved preference allows them.
    def test_disable_autoexec_wins(self):
        self.assertFalse(self.open_with_driver(("--disable-autoexec",)))

    # Without the argument the saved preference lets the script run, which proves the test above can fail.
    def test_saved_preference_alone_runs_scripts(self):
        self.assertTrue(self.open_with_driver(()))


if __name__ == "__main__":
    unittest.main()
