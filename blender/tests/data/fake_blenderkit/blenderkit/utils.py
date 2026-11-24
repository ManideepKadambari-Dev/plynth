"""Preference helpers of the fake add-on."""
import bpy

from . import global_vars

FIELDS = ("api_key", "api_key_refresh", "api_key_timeout", "keep_preferences", "preferences_lock", "global_dir",
          "directory_behaviour", "unpack_files", "create_asset_library", "resolution", "use_clipboard_scan",
          "thread_communication", "send_usage_data", "client_port", "proxy_which", "proxy_address")


# The add-on's preferences object.
def get_prefs():
    return bpy.context.preferences.addons[__package__].preferences


# A dict of every preference, as the add-on hands to its helper. It holds the key.
def get_preferences_as_dict():
    prefs = get_prefs()
    return {name: getattr(prefs, name) for name in FIELDS}


# The other form, for searches.
def get_preferences():
    return get_preferences_as_dict()


# Does nothing in background, as the real one.
def save_prefs(user_preferences=None, context=None, **kwargs):
    if not bpy.app.background:
        global_vars.note("save_prefs")


# Writes the preferences file in global_dir/config. Reached by a queued task and a logout.
def save_prefs_to_file():
    import json
    import os
    from . import persistent_preferences
    path = persistent_preferences.get_preferences_path()
    global_vars.note("prefs_file_written", path=path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(get_preferences_as_dict(), handle)


# The key is cleared when it is shorter than 25 characters, as the real add-on does.
def check_api_key_length(prefs):
    if prefs.api_key and len(prefs.api_key) < 25:
        prefs.api_key = ""
