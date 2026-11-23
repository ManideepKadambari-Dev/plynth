"""bpy.ops of the stand-in. As in Blender, bpy.ops.<group>.<name>(**options) goes through the module function
_op_call, so code that replaces bpy.ops._op_call sees every call. Built-in operators that a fetch must never
reach only note an event; preferences.addon_enable and addon_disable really change the add-ons.
"""
from . import utils as _utils


# Runs an operator by its name. Every call is noted.
def _op_call(idname, *args):
    import bpy
    options = args[-1] if args and isinstance(args[-1], dict) else {}
    bpy.note("operator", idname=idname, options=_plain(options))
    cls = _utils.operator_class(idname)
    if cls is not None:
        operator = cls()
        for name, value in options.items():
            setattr(operator, name, value)
        run = getattr(operator, "execute", None) or getattr(operator, "invoke")
        return run(bpy.context) if run.__code__.co_argcount == 2 else run(bpy.context, None)
    if idname in ("preferences.addon_enable", "preferences.addon_disable"):
        import addon_utils
        (addon_utils.enable if idname.endswith("enable") else addon_utils.disable)(options.get("module", ""))
    return {"FINISHED"}


# Options as text where they are not plain values, for the event log.
def _plain(options):
    out = {}
    for name, value in options.items():
        out[name] = value if isinstance(value, (str, int, float, bool, type(None))) else repr(value)
    return out


class _BPyOpsSubModOp:
    def __init__(self, group, name):
        self._idname = "%s.%s" % (group, name)

    def __call__(self, *args, **options):
        return _op_call(self._idname, options)

    def poll(self, *args):
        return True

    def idname_py(self):
        return self._idname


class _BPyOpsSubMod:
    def __init__(self, group):
        self._group = group

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return _BPyOpsSubModOp(self._group, name)


# bpy.ops.<group> for any group.
def __getattr__(group):
    if group.startswith("__"):
        raise AttributeError(group)
    return _BPyOpsSubMod(group)
