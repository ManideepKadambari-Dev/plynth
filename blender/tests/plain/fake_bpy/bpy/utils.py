"""bpy.utils of the stand-in: registering classes, and the user folders from the BLENDER_USER_ variables.
FAKE_BPY_KEEP_CLASS names operator type names, separated by commas, that unregister_class leaves registered
without an error, as a stand-in for a Blender where a class cannot be taken out.
"""
import os

from . import props, types

_operators = {}
_ever_registered = set()


# The type name Blender gives an operator: "preferences.addon_enable" becomes "PREFERENCES_OT_addon_enable".
def type_name(idname):
    group, name = idname.split(".", 1)
    return "%s_OT_%s" % (group.upper(), name)


# The first value of a property, as Blender gives it to a new object.
def _initial(field, owner):
    default = field.default
    if default is None and field.items:
        items = field.items if not callable(field.items) else field.items(owner, None)
        default = items[0][0]
    return default


# The properties of a class and its bases, by name.
def _fields(cls):
    fields = {}
    for klass in reversed(cls.__mro__):
        for name, value in getattr(klass, "__annotations__", {}).items():
            if isinstance(value, props._Property):
                fields[name] = value
    return fields


# The preferences object of an add-on: its properties as attributes, whose update runs on every set.
def _make_preferences(cls):
    fields = _fields(cls)

    class Preferences(cls):
        def __setattr__(self, name, value):
            object.__setattr__(self, name, value)
            field = fields.get(name)
            if field is not None and field.update is not None and not name.startswith("_"):
                import bpy
                field.update(self, bpy.context)

    preferences = Preferences.__new__(Preferences)
    for name, field in fields.items():
        object.__setattr__(preferences, name, _initial(field, preferences))
    return preferences


# A new operator object with every property at its default.
def new_operator(cls):
    operator = cls()
    for name, field in _fields(cls).items():
        object.__setattr__(operator, name, _initial(field, operator))
    return operator


# Registers a class: preferences go to the add-on's entry, operators to bpy.ops and bpy.types.
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
        _ever_registered.add(cls.bl_idname)
        setattr(types, type_name(cls.bl_idname), cls)
        cls.is_registered = True


# Takes a class out again.
def unregister_class(cls):
    if issubclass(cls, types.Operator):
        name = type_name(cls.bl_idname)
        if name in os.environ.get("FAKE_BPY_KEEP_CLASS", "").split(","):
            return
        if _operators.get(cls.bl_idname) is cls:
            _operators.pop(cls.bl_idname)
        if getattr(types, name, None) is cls:
            delattr(types, name)
        cls.is_registered = False


# The operator class registered under a name, or None.
def operator_class(idname):
    return _operators.get(idname)


# Whether a Python operator was ever registered under a name. Such a name is not a C operator.
def was_registered(idname):
    return idname in _ever_registered


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
