"""bpy.utils of the stand-in: registering classes, and the user folders from the BLENDER_USER_ variables."""
import os

from . import props, types

_operators = {}


# The preferences object of an add-on: its properties as attributes, whose update runs on every set.
def _make_preferences(cls):
    fields = {}
    for klass in reversed(cls.__mro__):
        for name, value in getattr(klass, "__annotations__", {}).items():
            if isinstance(value, props._Property):
                fields[name] = value

    class Preferences(cls):
        def __setattr__(self, name, value):
            object.__setattr__(self, name, value)
            field = fields.get(name)
            if field is not None and field.update is not None and not name.startswith("_"):
                import bpy
                field.update(self, bpy.context)

    preferences = Preferences.__new__(Preferences)
    for name, field in fields.items():
        default = field.default
        if default is None and field.items:
            items = field.items if not callable(field.items) else field.items(preferences, None)
            default = items[0][0]
        object.__setattr__(preferences, name, default)
    return preferences


# Registers a class: preferences go to the add-on's entry, operators to bpy.ops.
def register_class(cls):
    import bpy
    if issubclass(cls, types.AddonPreferences):
        module = cls.bl_idname
        entry = bpy.context.preferences.addons.get(module)
        if entry is None:
            entry = bpy._AddonEntry(module, None)
            bpy.context.preferences.addons[module] = entry
        entry.preferences = _make_preferences(cls)
    elif issubclass(cls, types.Operator):
        _operators[cls.bl_idname] = cls


# Takes a class out again.
def unregister_class(cls):
    if issubclass(cls, types.Operator):
        _operators.pop(cls.bl_idname, None)


# The operator class registered under a name, or None.
def operator_class(idname):
    return _operators.get(idname)


# The user folders, from the BLENDER_USER_ variables as Blender reads them.
def user_resource(kind, path="", create=False):
    names = {"CONFIG": "BLENDER_USER_CONFIG", "SCRIPTS": "BLENDER_USER_SCRIPTS", "DATAFILES": "BLENDER_USER_DATAFILES",
             "EXTENSIONS": "BLENDER_USER_EXTENSIONS"}
    folder = os.environ.get(names.get(kind, ""), "")
    full = os.path.join(folder, path) if path else folder
    if create:
        os.makedirs(full, exist_ok=True)
    return full


# The script folders: the system one from BLENDER_SYSTEM_SCRIPTS, then the user's.
def script_paths():
    found = [os.environ.get("BLENDER_SYSTEM_SCRIPTS", ""), os.environ.get("BLENDER_USER_SCRIPTS", "")]
    return [folder for folder in found if folder]


# Absolute path of a Blender path.
def abspath(path):
    return os.path.abspath(path)
