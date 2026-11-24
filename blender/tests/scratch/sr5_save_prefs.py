"""S-R 5 setup, run with factory startup in the scratch user: enables the test add-on and the test extension and
saves the preferences there, so the one start without factory startup loads both.
    blender -b -t 6 --offline-mode --factory-startup --python sr5_save_prefs.py
"""
import addon_utils
import bpy

for module in ("sr5_addon", "bl_ext.user_default.sr5_ext"):
    enabled = addon_utils.enable(module, default_set=True) is not None
    print("SR5 setup_%s: %s" % (module.split(".")[-1], "enabled" if enabled else "failed"), flush=True)
bpy.ops.wm.save_userpref()
print("SR5 SAVED", flush=True)
