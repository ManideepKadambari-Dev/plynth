"""Run by test_fetch_guard in the stand-in Blender with the fetch guard and the fake add-on loaded. It takes
callables to the operators the seal closes, asks the guard to require the fetch folder and to seal, then tries
each way the seal closes. With "held" after "--" it also calls addon_utils.enable and disable through the names
it took when it was loaded, before the seal. Prints one JSON line {"name": ..., "ok": true or false,
"error": "<type name>", "reason": "<reason of a GuardError>"} for each.
    stand_in_blender.py -b ... --python seal_probe.py [-- held]
"""
import json
import os
import sys

from addon_utils import disable as HELD_DISABLE
from addon_utils import enable as HELD_ENABLE

NAMED = ("preferences.addon_enable", "preferences.addon_disable", "preferences.addon_install",
         "preferences.addon_remove", "extensions.package_install_files", "extensions.package_install",
         "extensions.package_uninstall", "extensions.package_upgrade_all", "scene.blenderkit_addon_choice")


# Runs one attempt and prints its outcome. A call that returns a result other than FINISHED counts as refused.
def attempt(name, action):
    try:
        result = action()
        ok = not isinstance(result, set) or "FINISHED" in result
        print(json.dumps({"name": name, "ok": ok, "error": "", "reason": ""}), flush=True)
    except BaseException as error:
        print(json.dumps({"name": name, "ok": False, "error": type(error).__name__,
                          "reason": str(getattr(error, "reason", ""))}), flush=True)


# The callable of an operator by its name.
def operator(idname):
    import bpy
    group, name = idname.split(".", 1)
    return getattr(getattr(bpy.ops, group), name)


# Tries every way.
def main():
    import addon_utils
    import bpy
    guard = sys.modules.get("plynth_fetch_guard")
    attempt("guard_loaded", lambda: guard.register)
    early = {idname: operator(idname) for idname in NAMED}
    attempt("require", lambda: guard.require(os.environ.get("PLYNTH_FETCH_PRIVATE", "")))
    attempt("seal", lambda: guard.seal(operators=("scene.blenderkit_addon_choice",)))
    for idname in NAMED:
        attempt("type_left_" + idname, lambda idname=idname: getattr(bpy.types, guard.operator_type_name(idname)))
        attempt("by_name_" + idname, lambda idname=idname: operator(idname)())
        attempt("early_" + idname, lambda idname=idname: early[idname]())
    attempt("addon_utils_enable", lambda: addon_utils.enable("other_addon", default_set=True))
    attempt("addon_utils_disable", lambda: addon_utils.disable("blenderkit", default_set=True))
    attempt("seal_again", lambda: guard.seal())
    if "held" in sys.argv:
        attempt("held_enable", lambda: HELD_ENABLE("other_addon", default_set=True))
        attempt("held_disable", lambda: HELD_DISABLE("blenderkit", default_set=True))


main()
