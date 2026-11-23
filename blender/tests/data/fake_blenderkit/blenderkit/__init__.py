"""A fake BlenderKit add-on for the fetch tests, with the names and behaviors of add-on 3.21.7.261119 that
the fetch meets (notes of the source check): the preferences and their updates, the loader of the saved
file, the helper started from <global_dir>/client, its ports, the operator that sends the download at once,
the append step that deletes on failure, refresh and logout. It downloads nothing itself: the fake helper
program (tests/tools/fake_store_helper) does, by its scenario. The test copies that program to
client/bk_client.exe beside this file and makes the blenderkit_updater folder, as a first start would.
"""
import logging
import sys

import bpy
from bpy.props import BoolProperty, EnumProperty, IntProperty, StringProperty

from . import (addon_updater_ops, bg_blender, client_lib, download, global_vars, paths, persistent_preferences,
               tasks_queue, timer, utils)

bl_info = {
    "name": "BlenderKit Online Asset Library",
    "author": "Plynth tests",
    "version": (3, 21, 7, 261119),
    "blender": (4, 2, 0),
    "category": "3D View",
}

RESOLUTIONS = (("512", "512", ""), ("1024", "1024", ""), ("2048", "2048", ""), ("4096", "4096", ""),
               ("8192", "8192", ""), ("ORIGINAL", "Original", ""))

bk_logger = logging.getLogger("blenderkit")
bk_logger.propagate = False
if not bk_logger.handlers:
    bk_logger.addHandler(logging.StreamHandler(sys.stdout))
bk_logger.setLevel(logging.INFO)


# The update of the key: the real one asks the helper about it.
def api_key_update(prefs, context):
    global_vars.note("api_key_update")


# The update of send_usage_data: posts it to the helper, which saves it in its settings file.
def send_usage_data_update(prefs, context):
    client_lib.send_settings({"send_usage_data": prefs.send_usage_data})


# The update of the port and proxy fields: restarts the helper unless the lock is on.
def connection_update(prefs, context):
    if not prefs.preferences_lock:
        global_vars.note("connection_changed")
        client_lib.shutdown_client()


class BlenderKitAddonPreferences(bpy.types.AddonPreferences):
    bl_idname = __name__

    api_key: StringProperty(default="", subtype="PASSWORD", update=api_key_update)  # type: ignore[valid-type]
    api_key_refresh: StringProperty(default="", subtype="PASSWORD")  # type: ignore[valid-type]
    api_key_timeout: IntProperty(default=0)  # type: ignore[valid-type]
    keep_preferences: BoolProperty(default=True, update=persistent_preferences.keep_preferences_update)  # type: ignore[valid-type]
    preferences_lock: BoolProperty(default=False)  # type: ignore[valid-type]
    global_dir: StringProperty(default=paths.default_global_dict(), subtype="DIR_PATH")  # type: ignore[valid-type]
    directory_behaviour: EnumProperty(items=(("BOTH", "Both", ""), ("GLOBAL", "Global", ""), ("LOCAL", "Local", "")),  # type: ignore[valid-type]
                                      default="BOTH")
    unpack_files: BoolProperty(default=False)  # type: ignore[valid-type]
    create_asset_library: BoolProperty(default=True)  # type: ignore[valid-type]
    resolution: EnumProperty(items=RESOLUTIONS, default="2048")  # type: ignore[valid-type]
    use_clipboard_scan: BoolProperty(default=True)  # type: ignore[valid-type]
    thread_communication: BoolProperty(default=True)  # type: ignore[valid-type]
    send_usage_data: BoolProperty(default=True, update=send_usage_data_update)  # type: ignore[valid-type]
    client_port: StringProperty(default="", update=connection_update)  # type: ignore[valid-type]
    proxy_which: StringProperty(default="SYSTEM", update=connection_update)  # type: ignore[valid-type]
    proxy_address: StringProperty(default="", update=connection_update)  # type: ignore[valid-type]


# Registers the add-on. In background no timer is registered, as in the real one. A preferences save is
# queued, the updater folder is made when it is missing, and callables to operators are taken for the tests.
def register():
    addon_updater_ops.register(bl_info)
    bpy.utils.register_class(BlenderKitAddonPreferences)
    for cls in download.CLASSES:
        bpy.utils.register_class(cls)
    download.take_callables()
    global_vars.PREFS = utils.get_preferences_as_dict()
    tasks_queue.add_task((utils.save_prefs_to_file, ()))
    if not bpy.app.background:
        bpy.app.timers.register(timer.client_communication_timer, persistent=True)
        bpy.app.timers.register(tasks_queue.queue_worker, persistent=True)
        bpy.app.timers.register(bg_blender.bg_update, persistent=True)
    global_vars.note("registered")


# Removes what register added.
def unregister():
    for cls in reversed(download.CLASSES):
        bpy.utils.unregister_class(cls)
    bpy.utils.unregister_class(BlenderKitAddonPreferences)
