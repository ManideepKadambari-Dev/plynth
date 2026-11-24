"""Run by test_fetch_guard in the stand-in Blender, also at low integrity. It writes one file inside the fetch
folder, then turns on a preference and calls wm.save_userpref, a C operator in Blender. Prints one JSON line
{"name": ..., "ok": true or false, "error": "<type name>"} for each.
    stand_in_blender.py -b ... --python save_probe.py -- <fetch folder>
"""
import json
import os
import sys


# Runs one attempt and prints its outcome.
def attempt(name, action):
    try:
        action()
        print(json.dumps({"name": name, "ok": True, "error": ""}), flush=True)
    except BaseException as error:
        print(json.dumps({"name": name, "ok": False, "error": type(error).__name__}), flush=True)


# Writes a small file in the fetch folder.
def write_inside(folder):
    with open(os.path.join(folder, "inside.txt"), "w", encoding="utf-8") as handle:
        handle.write("probe")


# Tries both.
def main():
    folder = sys.argv[sys.argv.index("--") + 1]
    import bpy
    attempt("inside", lambda: write_inside(folder))
    bpy.context.preferences.filepaths.use_scripts_auto_execute = True
    attempt("save_userpref", lambda: bpy.ops.wm.save_userpref())


main()
