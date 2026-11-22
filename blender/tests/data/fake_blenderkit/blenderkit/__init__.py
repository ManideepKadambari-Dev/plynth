"""A fake BlenderKit add-on for the worker tests. It has the same touch points as the real one that the worker
uses (preferences api_key and global_dir, the operator scene.blenderkit_download, the timer functions) and
downloads nothing: after a few timer runs it copies files made by the test into global_dir.
"""
import bpy
from bpy.props import EnumProperty, IntProperty, StringProperty

from . import download, reports, tasks_queue, timer

bl_info = {
    "name": "Fake BlenderKit for tests",
    "author": "Plynth tests",
    "version": (9, 9, 9),
    "blender": (3, 0, 0),
    "category": "3D View",
}

CLASSES = []


class FakePreferences(bpy.types.AddonPreferences):
    bl_idname = __name__

    api_key: StringProperty(name="API key", default="", subtype="PASSWORD")  # type: ignore[valid-type]
    global_dir: StringProperty(name="Download folder", default="", subtype="DIR_PATH")  # type: ignore[valid-type]
    resolution: EnumProperty(  # type: ignore[valid-type]
        name="Resolution", items=(("1024", "1024", ""), ("ORIGINAL", "Original", "")), default="1024")


# Registers the preferences, the operator and the timers.
def register():
    bpy.utils.register_class(FakePreferences)
    download.register_download()
    for function in (timer.client_communication_timer, tasks_queue.queue_worker):
        bpy.app.timers.register(function, first_interval=0.5, persistent=True)


# Removes what register added.
def unregister():
    for function in (timer.client_communication_timer, tasks_queue.queue_worker):
        if bpy.app.timers.is_registered(function):
            bpy.app.timers.unregister(function)
    download.unregister_download()
    bpy.utils.unregister_class(FakePreferences)
