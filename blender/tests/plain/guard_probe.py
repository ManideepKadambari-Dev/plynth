"""Run by test_fetch_guard in the stand-in Blender with the fetch guard loaded. It tries each kind of event
and prints one JSON line {"name": ..., "ok": true or false, "error": "<type name>"} for each.
    stand_in_blender.py -b ... --python guard_probe.py -- <outside folder> <inside folder> <helper copy> <pid>
"""
import json
import os
import subprocess
import sys
import winreg


# Runs one attempt and prints its outcome.
def attempt(name, action):
    try:
        action()
        print(json.dumps({"name": name, "ok": True, "error": ""}), flush=True)
    except BaseException as error:
        print(json.dumps({"name": name, "ok": False, "error": type(error).__name__}), flush=True)


# Writes a small file.
def write(path, text="probe"):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


# Reads a file.
def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


# Takes out every module of the guard folder that offers it.
def unregister_guard():
    folder = os.path.normcase(os.path.abspath(os.environ.get("BLENDER_SYSTEM_SCRIPTS", "")))
    for module in list(sys.modules.values()):
        path = os.path.normcase(os.path.abspath(getattr(module, "__file__", "") or ""))
        if path.startswith(folder) and hasattr(module, "unregister"):
            module.unregister()


# Tries the file events in one folder. kept is a file there that exists before.
def file_events(where, folder, kept):
    made = os.path.join(folder, "made.txt")
    renamed = os.path.join(folder, "renamed.txt")
    attempt(where + "_create", lambda: write(made))
    attempt(where + "_change", lambda: write(kept, "changed"))
    attempt(where + "_rename", lambda: os.rename(kept, renamed))
    source = renamed if os.path.exists(renamed) else kept
    attempt(where + "_link", lambda: os.link(source, os.path.join(folder, "linked.txt")))
    attempt(where + "_folder", lambda: os.makedirs(os.path.join(folder, "made_folder")))
    attempt(where + "_delete", lambda: os.remove(source))


# Tries every event.
def main():
    outside, inside, helper, pid = sys.argv[sys.argv.index("--") + 1:][:4]
    import bpy
    write(os.path.join(inside, "kept.txt"), "kept")
    file_events("outside", outside, os.path.join(outside, "kept.txt"))
    file_events("inside", inside, os.path.join(inside, "kept.txt"))
    attempt("outside_read", lambda: read(os.path.join(outside, "kept.txt")))
    attempt("nul_open", lambda: open(os.devnull, "w").close())
    attempt("registry_read", lambda: winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Software").Close())
    key = "Software\\PlynthGuardProbe" + pid
    attempt("registry_write", lambda: winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, key).Close())
    attempt("program_outside", lambda: subprocess.run([sys.executable, "-c", "pass"], timeout=30))
    attempt("program_inside", lambda: subprocess.run([helper, "--port", "0", "--global_dir", inside], timeout=3))
    attempt("kill", lambda: os.kill(int(pid), 9))
    attempt("install", lambda: bpy.ops.extensions.package_install_files(filepath=os.path.join(outside, "x.zip")))
    attempt("unregister", unregister_guard)
    attempt("after_unregister_create", lambda: write(os.path.join(outside, "after.txt")))


main()
