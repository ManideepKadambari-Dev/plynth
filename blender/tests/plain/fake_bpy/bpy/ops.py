"""bpy.ops of the stand-in, as in Blender 5.2: bpy.ops.<group>.<name> is a callable made for that name, and
each call looks the operator up by its name at that moment. No Python function sits between a call and the
operator, so nothing in bpy.ops can be replaced to refuse calls, and a callable taken before its class was
unregistered fails once the class is gone. The Python operators are the registered classes, among them the
bundled ones of bl_operators.py. Every other name is a C operator: wm.save_userpref writes the preferences
file, the rest only note an event. In Blender a C operator writes without Python, so the guard never sees it;
here it writes with Python, so a test of a C write runs without the guard or at low integrity.
An error inside an operator reaches the caller as RuntimeError with the traceback in its text, as in Blender.
"""
import json
import os
import traceback

from . import utils as _utils


# Options as text where they are not plain values, for the event log.
def _plain(options):
    out = {}
    for name, value in options.items():
        out[name] = value if isinstance(value, (str, int, float, bool, type(None))) else repr(value)
    return out


# Writes the preferences file from the preferences in memory, as the C code of Blender does.
def _save_userpref():
    import bpy
    addons = {}
    for name, entry in bpy.context.preferences.addons.items():
        values = {}
        if entry.preferences is not None:
            values = {field: value for field, value in vars(entry.preferences).items()
                      if not field.startswith("_") and isinstance(value, (str, int, float, bool))}
        addons[name] = values
    data = {"addons": addons, "filepaths": dict(vars(bpy.context.preferences.filepaths))}
    path = os.path.join(os.environ.get("BLENDER_USER_CONFIG", ""), "userpref.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle)


_C_OPERATORS = {"wm.save_userpref": _save_userpref}


# Makes the callable for one operator name. It finds the operator again at each call.
def _create_function(group, name):
    idname = "%s.%s" % (group, name)

    # Runs the operator, or raises when a Python operator of this name is no longer registered.
    def call(*args, **options):
        import bpy
        cls = _utils.operator_class(idname)
        if cls is None and _utils.was_registered(idname):
            bpy.note("operator_not_found", idname=idname)
            raise AttributeError("Calling operator bpy.ops.%s error, could not be found" % idname)
        bpy.note("operator", idname=idname, options=_plain(options))
        try:
            if cls is None:
                run_c = _C_OPERATORS.get(idname)
                if run_c is not None:
                    run_c()
                return {"FINISHED"}
            operator = _utils.new_operator(cls)
            for key, value in options.items():
                setattr(operator, key, value)
            run = getattr(operator, "execute", None) or getattr(operator, "invoke")
            return run(bpy.context) if run.__code__.co_argcount == 2 else run(bpy.context, None)
        except Exception as error:
            trace = "".join(traceback.format_exception(type(error), error, error.__traceback__))
            raise RuntimeError("Error: Python: %s\nlocation: <unknown location>:-1\n" % trace) from error

    call.poll = lambda *args: _utils.operator_class(idname) is not None or not _utils.was_registered(idname)
    call.idname_py = lambda: idname
    return call


class _BPyOpsSubMod:
    def __init__(self, group):
        self._group = group

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return _create_function(self._group, name)

    def __dir__(self):
        prefix = self._group + "."
        names = [idname for idname in list(_utils._operators) + list(_C_OPERATORS) if idname.startswith(prefix)]
        return sorted(idname[len(prefix):] for idname in names)


# bpy.ops.<group> for any group.
def __getattr__(group):
    if group.startswith("__"):
        raise AttributeError(group)
    return _BPyOpsSubMod(group)
