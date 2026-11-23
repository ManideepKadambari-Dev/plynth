"""A plain Python stand-in for a background Blender, so the fetch task runs on a machine without Blender. It
takes Blender's command line (-b, -t, --offline-mode, --factory-startup, --disable-autoexec,
--python-exit-code, --python <file>, -- ...) and starts as Blender does: the startup scripts of
BLENDER_SYSTEM_SCRIPTS first (where the fetch guard is), then the add-ons enabled in
BLENDER_USER_CONFIG/userpref.json with their saved preferences, then the --python file as __main__.
It exits with the --python-exit-code when that file raises.

userpref.json: {"addons": {"blenderkit": {"api_key": "...", ...}}}. Preferences are set without their
update functions, as Blender loads them.
"""
import importlib
import json
import os
import runpy
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))


# Reads Blender's arguments. Returns (options, script, exit code on error).
def read_arguments(argv):
    options = set()
    script = None
    exit_code = 0
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--":
            break
        if arg in ("-t", "--threads", "--python-exit-code", "--python"):
            value = argv[index + 1]
            if arg == "--python":
                script = value
            elif arg == "--python-exit-code":
                exit_code = int(value)
            else:
                options.add("threads=" + value)
            index += 2
            continue
        options.add(arg)
        index += 1
    return options, script, exit_code


# Runs the startup scripts of the system scripts folder, each module's register().
def run_startup_scripts():
    folder = os.environ.get("BLENDER_SYSTEM_SCRIPTS", "")
    startup = os.path.join(folder, "startup")
    if not folder or not os.path.isdir(startup):
        return
    sys.path.insert(0, startup)
    for name in sorted(os.listdir(startup)):
        if name.endswith(".py"):
            module = importlib.import_module(name[:-3])
            if hasattr(module, "register"):
                module.register()


# Enables the user's add-ons and sets their saved preferences.
def enable_user_addons():
    import addon_utils
    import bpy
    path = os.path.join(os.environ.get("BLENDER_USER_CONFIG", ""), "userpref.json")
    if not os.path.isfile(path):
        return
    with open(path, "r", encoding="utf-8") as handle:
        saved = json.load(handle)
    for name, values in saved.get("addons", {}).items():
        if addon_utils.enable(name) is None:
            continue
        entry = bpy.context.preferences.addons.get(name)
        if entry is not None and entry.preferences is not None:
            for field, value in values.items():
                object.__setattr__(entry.preferences, field, value)


# Starts like Blender and runs the script.
def main():
    argv = sys.argv[1:]
    options, script, exit_code = read_arguments(argv)
    sys.path.insert(0, os.path.join(HERE, "fake_bpy"))
    import bpy
    bpy.app.background = "-b" in options or "--background" in options
    bpy.app.online_access = "--offline-mode" not in options
    bpy.app.binary_path = os.path.abspath(__file__)
    if "--factory-startup" not in options:
        run_startup_scripts()
        enable_user_addons()
    if script is None:
        return 0
    sys.argv = [bpy.app.binary_path] + argv
    try:
        runpy.run_path(script, run_name="__main__")
    except SystemExit as done:
        return done.code or 0
    except BaseException:
        traceback.print_exc()
        return exit_code
    return 0


if __name__ == "__main__":
    sys.exit(main())
