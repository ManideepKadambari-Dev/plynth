"""addon_utils of the stand-in: the add-ons in the addons folders of the script paths, and enabling one,
which imports it and runs its register() as Blender does. Enabling and disabling are noted as events.
"""
import importlib
import os
import sys

import bpy


# The add-on modules that can be found, without importing them.
def modules(refresh=True):
    found = []
    for folder in bpy.utils.script_paths():
        addons = os.path.join(folder, "addons")
        if not os.path.isdir(addons):
            continue
        if addons not in sys.path:
            sys.path.append(addons)
        for name in sorted(os.listdir(addons)):
            if os.path.isfile(os.path.join(addons, name, "__init__.py")):
                found.append(_Stub(name, os.path.join(addons, name, "__init__.py")))
    return found


class _Stub:
    def __init__(self, name, path):
        self.__name__ = name
        self.__file__ = path


# The bl_info of an add-on module.
def module_bl_info(module):
    loaded = sys.modules.get(module.__name__)
    return dict(getattr(loaded, "bl_info", {}) or {})


# Whether an add-on is enabled by default and whether it is loaded.
def check(name):
    return name in bpy.context.preferences.addons, name in sys.modules


# Imports an add-on and runs its register(). Returns the module, or None when that failed.
def enable(name, default_set=True, persistent=False, handle_error=None):
    bpy.note("addon_enable", module=name)
    modules()
    try:
        module = importlib.import_module(name)
        bpy.context.preferences.addons.setdefault(name, bpy._AddonEntry(name, None))
        module.register()
        return module
    except Exception as error:
        bpy.context.preferences.addons.pop(name, None)
        bpy.note("addon_failed", module=name, error=type(error).__name__)
        if handle_error is not None:
            handle_error(error)
        return None


# Runs an add-on's unregister() and forgets it.
def disable(name, default_set=True, handle_error=None):
    bpy.note("addon_disable", module=name)
    module = sys.modules.get(name)
    if module is not None and hasattr(module, "unregister"):
        module.unregister()
    bpy.context.preferences.addons.pop(name, None)
