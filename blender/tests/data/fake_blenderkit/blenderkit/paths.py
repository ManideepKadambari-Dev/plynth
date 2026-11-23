"""Folders of the fake add-on, under the real names. The download folders follow PREFS and are created."""
import os
import re

from . import global_vars

dirs_exist_dict = {}


# The default data folder: XDG_DATA_HOME, then the home folder.
def default_global_dict():
    home = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~")
    return os.path.join(home, "blenderkit_data")


# The add-on's slug: lower case, other characters one hyphen, cut to a length.
def slugify(text, length):
    out = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return out[:length].strip("-")


# The item folder's name.
def get_asset_directory_name(asset_name, asset_id):
    return "%s_%s" % (slugify(asset_name, 16), asset_id)


# The download folders of a kind, made when missing.
def get_download_dirs(asset_type):
    behaviour = global_vars.PREFS.get("directory_behaviour", "BOTH")
    found = []
    if behaviour in ("BOTH", "GLOBAL"):
        found.append(os.path.join(global_vars.PREFS.get("global_dir", default_global_dict()), asset_type + "s"))
    if behaviour in ("BOTH", "LOCAL"):
        import bpy
        if bpy.data.filepath:
            found.append(os.path.join(os.path.dirname(bpy.data.filepath), "assets", asset_type + "s"))
    for folder in found:
        os.makedirs(folder, exist_ok=True)
    return found


# The config folder.
def get_config_dir_path():
    return os.path.join(global_vars.PREFS.get("global_dir", default_global_dict()), "config")


# The temp folder. On any error it resets global_dir to the default, as the real one does.
def get_temp_dir(subdir=None):
    global_vars.note("get_temp_dir")
    try:
        folder = os.path.join(global_vars.PREFS["global_dir"], "temp")
        os.makedirs(folder, exist_ok=True)
        dirs_exist_dict[folder] = True
        return folder
    except Exception:
        global_vars.PREFS["global_dir"] = default_global_dict()
        return None
