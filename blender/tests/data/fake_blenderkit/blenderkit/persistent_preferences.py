"""The saved sign in file of the fake add-on. Like the real one it deletes a file it cannot parse, so a test can
tell whether the worker reached it.
"""
import json
import os

import bpy


# Returns the path of the saved settings file, in the Blender config folder.
def get_preferences_path():
    return os.path.abspath(os.path.join(bpy.utils.user_resource("CONFIG"), "preferences.json"))


# Reads the saved settings and sets the key from them. A file that does not parse is deleted.
def load_preferences_from_JSON():
    path = get_preferences_path()
    if not os.path.exists(path):
        return
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except Exception:
        os.remove(path)
        return
    entry = bpy.context.preferences.addons.get(__package__)
    if entry is not None and isinstance(data, dict):
        entry.preferences.api_key = str(data.get("api_key", ""))
