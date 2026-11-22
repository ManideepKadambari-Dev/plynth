import contextlib

import bpy

from . import Scope, enable_addon, realize, register, stand_on_origin
from ..generating import AddonUnavailable

MODULES = ("home_builder_5", "home_builder")
TYPES_MODULE = "product_libraries.frameless.types_frameless"


# Says whether Home Builder 5 is installed and can be switched on.
def available():
    return enable_addon(MODULES)


# Raises AddonUnavailable when Home Builder cannot be used.
def require():
    if not available():
        raise AddonUnavailable("The add-on Home Builder 5 is not installed or cannot be enabled.")


# Imports a module of the Home Builder package, whichever name the add-on has in this Blender.
def package_module(path):
    import importlib
    from . import find_module
    root = find_module(MODULES)
    return importlib.import_module(root + "." + path)


# Home Builder 5.1 sets geometry nodes inputs with modifier["Socket_2"] = value, which Blender 5.2 refuses.
# While a cabinet is built, its input functions use modifier.properties.inputs[id]["value"] instead, and the
# originals are put back afterwards. Nothing changes in the add-on files.
@contextlib.contextmanager
def input_access_for_new_blender():
    types = package_module("hb_types")
    utils = package_module("hb_utils")
    classes = (types.GeoNodeObject, types.CabinetPartModifier)
    saved = [(cls, name, cls.__dict__.get(name)) for cls in classes for name in ("set_input", "get_input", "driver_input", "var_input")]

    def modifier_of(item):
        if isinstance(item, types.CabinetPartModifier):
            return item.mod
        return item.obj.modifiers[item.obj.home_builder.mod_name]

    def socket_of(item, name):
        modifier = modifier_of(item)
        return modifier, modifier.node_group.interface.items_tree[name].identifier

    def set_input(self, name, value):
        modifier, identifier = socket_of(self, name)
        modifier.properties.inputs[identifier]["value"] = value

    def get_input(self, name):
        modifier, identifier = socket_of(self, name)
        return modifier.properties.inputs[identifier]["value"]

    def path_of(self, name):
        modifier, identifier = socket_of(self, name)
        return 'modifiers["%s"].properties.inputs["%s"]["value"]' % (modifier.name, identifier)

    def driver_input(self, name, expression, variables=()):
        driver = self.obj.id_data.driver_add(path_of(self, name))
        utils.add_driver_variables(driver, list(variables))
        driver.driver.expression = expression

    def var_input(self, name, variable_name):
        return types.Variable(self.obj.id_data, path_of(self, name), variable_name)

    try:
        for cls in classes:
            cls.set_input, cls.get_input = set_input, get_input
            cls.driver_input, cls.var_input = driver_input, var_input
        yield
    finally:
        for cls, name, original in saved:
            if original is None:
                if name in cls.__dict__:
                    delattr(cls, name)
            else:
                setattr(cls, name, original)


# Makes a run of count base cabinets side by side, each width metres wide, with doors, standing on z 0 with the
# front toward -Y, and centres the run on the origin. Returns a list with one mesh object, with no modifiers,
# no drivers and no cage or template objects.
def base_cabinets(count=3, width=0.6, height=0.87, depth=0.6):
    require()
    cabinet_type = package_module(TYPES_MODULE).BaseCabinet
    scope = Scope()
    try:
        with input_access_for_new_blender():
            cages = []
            for index in range(count):
                cabinet = cabinet_type()
                cabinet.width, cabinet.height, cabinet.depth = width, height, depth
                cabinet.create("Base Cabinet")
                cabinet.obj.location.x = index * width
                cages.append(cabinet.obj)
            bpy.context.view_layer.update()
            parts = [obj for obj in scope.new_objects()
                     if obj.type == "MESH" and obj.parent is not None and obj not in cages
                     and obj.display_type not in ("WIRE", "BOUNDS")]
            merged = realize(parts, "BaseCabinets")
        stand_on_origin(merged)
    except Exception:
        scope.clean()
        raise
    scope.clean(keep=[merged])
    return [merged]


class Api:
    available = staticmethod(available)
    base_cabinets = staticmethod(base_cabinets)


register("home_builder", MODULES, Api)
register("home_builder_5", MODULES, Api)
