import platform

import bpy

from ..registry import TASKS

BLENDERKIT_MODULE = "blenderkit"


# Returns the last dotted part of a module name, so bl_ext.user_default.blenderkit gives blenderkit.
def short_name(module_name):
    return module_name.rsplit(".", 1)[-1]


# Reads the name and version of an add-on module, or empty text when it has none.
def addon_details(module):
    info = {}
    try:
        import addon_utils
        info = addon_utils.module_bl_info(module) or {}
    except Exception:
        pass
    name = info.get("name") or ""
    version = info.get("version") or ""
    if isinstance(version, (tuple, list)):
        version = ".".join(str(part) for part in version)
    return str(name), str(version)


# Lists the modules known to Blender, keyed by module name. Empty when they cannot be listed.
def installed_modules():
    try:
        import addon_utils
        return {module.__name__: module for module in addon_utils.modules()}
    except Exception:
        return {}


# Says whether a BlenderKit API key is stored. The key itself is never read into a variable that is kept.
def blenderkit_signed_in(addon):
    try:
        return bool(getattr(addon.preferences, "api_key", ""))
    except Exception:
        return False


# Reports the Blender version, the enabled add-ons and the state of BlenderKit.
def run(ctx):
    modules = installed_modules()
    enabled = {}
    for module_name, addon in bpy.context.preferences.addons.items():
        enabled[module_name] = addon

    addons = []
    for module_name in sorted(enabled):
        name, version = addon_details(modules.get(module_name))
        addons.append({"module": module_name, "name": name, "version": version})

    kit_installed = any(short_name(name) == BLENDERKIT_MODULE for name in modules)
    kit_enabled_names = [name for name in enabled if short_name(name) == BLENDERKIT_MODULE]
    kit_signed_in = bool(kit_enabled_names) and blenderkit_signed_in(enabled[kit_enabled_names[0]])

    build_date = bpy.app.build_date
    if isinstance(build_date, bytes):
        build_date = build_date.decode("utf-8", "replace")

    ctx.output = {
        "blender": {
            "version": ".".join(str(part) for part in bpy.app.version),
            "version_text": str(bpy.app.version_string),
            "python": platform.python_version(),
            "build_date": str(build_date),
        },
        "addons": addons,
        "blenderkit": {
            "installed": kit_installed or bool(kit_enabled_names),
            "enabled": bool(kit_enabled_names),
            "signed_in": kit_signed_in,
        },
    }


TASKS.register("probe", run)
