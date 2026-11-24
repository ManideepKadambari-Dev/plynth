"""A stand-in for Blender's bpy module, for the worker tests that run in plain Python on a machine without
Blender. It holds only what the fetch task and the fake BlenderKit add-on touch. Operators behave as in Blender
5.2 (see ops.py): nothing in Python sits between a call and the operator. A fake .blend file is JSON:
{"fake_blend": 1, "materials": ["Oak"], "objects": ["Chair"]}. Every operator call and every saved
preference is appended to the file FAKE_BK_EVENTS names. FAKE_BLENDER_VERSION, for example "5.3.0", sets
the version this Blender reports; it is 5.2.0 otherwise.
"""
import json
import os
import sys

from . import ops, props, types, utils  # noqa: F401


# Appends one event line for the tests.
def note(event, **fields):
    path = os.environ.get("FAKE_BK_EVENTS")
    if path:
        fields["event"] = event
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(fields) + "\n")


class _Timers:
    # Timers are kept but never run by themselves, as in a background Blender running a script.
    def __init__(self):
        self.registered = []

    def register(self, function, first_interval=0.0, persistent=False):
        self.registered.append(function)

    def unregister(self, function):
        if function in self.registered:
            self.registered.remove(function)

    def is_registered(self, function):
        return function in self.registered


# The version this stand-in reports, as numbers and as text.
def _version():
    text = os.environ.get("FAKE_BLENDER_VERSION", "") or "5.2.0"
    return tuple(int(part) for part in text.split(".")), text


class _App:
    background = True
    version, version_string = _version()
    binary_path = sys.executable
    online_access = False
    timers = _Timers()
    driver_namespace = {}


app = _App()


class _AddonEntry:
    def __init__(self, module, preferences):
        self.module = module
        self.preferences = preferences


class _Addons(dict):
    # Like Blender's collection of enabled add-ons: get, keys, in.
    pass


class _Preferences:
    def __init__(self):
        self.addons = _Addons()
        self.filepaths = types.Bag(use_scripts_auto_execute=False)


class _Collection(list):
    # A list of data blocks by name.
    def get(self, name, default=None):
        for item in self:
            if item.name == name:
                return item
        return default

    def new(self, name, *args):
        item = types.ID(name)
        if args:
            item.data = args[0]
        self.append(item)
        return item

    def remove(self, item, do_unlink=True):
        if item in self:
            list.remove(self, item)

    def keys(self):
        return [item.name for item in self]


class _LoadedNames:
    def __init__(self, data):
        self.materials = list(data.get("materials", []))
        self.objects = list(data.get("objects", []))
        self.meshes = []
        self.collections = []


class _Libraries:
    # bpy.data.libraries.load reads a fake .blend file's names.
    def load(self, path, link=False):
        return _Loader(path)


class _Loader:
    def __init__(self, path):
        self.path = path

    def __enter__(self):
        with open(self.path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        if data.get("fake_blend") != 1:
            raise OSError("not a fake .blend file")
        return _LoadedNames(data), _LoadedNames({})

    def __exit__(self, *exc):
        return False


class _Data:
    def __init__(self):
        self.objects = _Collection()
        self.meshes = _Collection()
        self.materials = _Collection()
        self.libraries = _Libraries()
        self.filepath = ""

    def __getattr__(self, name):
        value = _Collection()
        setattr(self, name, value)
        return value


data = _Data()


class _Scene(dict):
    name = "Scene"


class _ViewLayer:
    def __init__(self):
        self.objects = types.Bag(active=None)


class _Context:
    def __init__(self):
        self.preferences = _Preferences()
        self.scene = _Scene()
        self.view_layer = _ViewLayer()
        self.window = types.Bag(screen=types.Bag(areas=[]))
        self.window_manager = types.Bag(windows=[self.window])
        self.mode = "OBJECT"

    @property
    def active_object(self):
        return self.view_layer.objects.active


context = _Context()
