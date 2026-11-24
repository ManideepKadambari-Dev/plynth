"""The saved sign in file of the fake add-on, in <global_dir>/config, as in add-on 3.21.7.261119."""
import json
import os

from . import global_vars, utils


# The path of the file. It follows global_dir.
def get_preferences_path():
    return os.path.join(utils.get_prefs().global_dir, "config", "preferences.json")


# Reads the file and sets every value it holds. A file that does not parse is deleted. Setting
# keep_preferences to false deletes the file through its update. Ends with the lock off.
def load_preferences_from_JSON():
    path = get_preferences_path()
    global_vars.note("loader_ran", path=path)
    if not os.path.exists(path):
        return
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except Exception:
        global_vars.note("prefs_file_deleted", why="broken")
        os.remove(path)
        return
    prefs = utils.get_prefs()
    prefs.preferences_lock = True
    if isinstance(data, dict):
        for name in utils.FIELDS:
            if name in data and name != "preferences_lock":
                setattr(prefs, name, data[name])
        utils.check_api_key_length(prefs)
    prefs.preferences_lock = False


# The update of keep_preferences: false deletes the saved file.
def keep_preferences_update(prefs, context):
    if not prefs.keep_preferences:
        path = get_preferences_path()
        if os.path.exists(path):
            global_vars.note("prefs_file_deleted", why="keep_preferences")
            os.remove(path)
