import unreal

from .. import ue
from ..context import value_hash
from ..errors import LinkError
from ..parameters import CHANNELS, MAP_PARAMETERS, PARENT_CHANNELS, VALUE_PARAMETERS
from ..registry import step
from .parents import GRAPH_VERSION


# Makes or updates one material instance per material slot, MI_<slot>, on the parent of its blend.
@step("instances", 70, "material_failed", applies=lambda ctx: ctx.pack)
def run(ctx):
    for entry in ctx.job["materials"]:
        path = ctx.materials_folder + "/" + entry["asset"]
        source_hash = value_hash("instance", entry, GRAPH_VERSION, texture_stamps(ctx, entry))
        instance = ue.load(path)
        if ctx.unchanged(instance, source_hash) and isinstance(instance, unreal.MaterialInstanceConstant):
            ctx.claim(instance, path, "unchanged")
        else:
            action = "created" if instance is None else "updated"
            if instance is None:
                tools = unreal.AssetToolsHelpers.get_asset_tools()
                instance = tools.create_asset(entry["asset"], ctx.materials_folder, unreal.MaterialInstanceConstant,
                                              unreal.MaterialInstanceConstantFactoryNew())
            if not isinstance(instance, unreal.MaterialInstanceConstant):
                raise LinkError("material_failed", "Unreal Engine could not make the material %s." % path)
            fill(ctx, instance, entry)
            ctx.claim(instance, path, action, source_hash)
        ctx.instances[entry["slot"].lower()] = instance


# Returns the source fingerprints of the textures a material uses, so a new texture updates the instance.
def texture_stamps(ctx, entry):
    stamps = {}
    for channel in CHANNELS:
        name = entry["maps"][channel]
        if name:
            texture = ue.load(ctx.textures_folder + "/" + name)
            stamps[channel] = ue.get_tag(texture, ue.TAG_SOURCE) if texture is not None else ""
    return stamps


# Sets an instance's parent, maps, values and two sided setting from the job's material entry.
def fill(ctx, instance, entry):
    library = unreal.MaterialEditingLibrary
    parent = ctx.parents[entry["blend"]]
    library.set_material_instance_parent(instance, parent)
    library.clear_all_material_instance_parameters(instance)
    for channel in PARENT_CHANNELS[entry["blend"]]:
        switch_name, map_name, _ = MAP_PARAMETERS[channel]
        texture_name = entry["maps"][channel]
        library.set_material_instance_static_switch_parameter_value(instance, switch_name, texture_name is not None)
        if texture_name is None:
            continue
        texture = ue.load(ctx.textures_folder + "/" + texture_name)
        if texture is None:
            raise LinkError("material_failed", "The material %s uses the texture %s, which was not imported."
                            % (entry["asset"], texture_name))
        library.set_material_instance_texture_parameter_value(instance, map_name, texture)
    values = entry["values"]
    red, green, blue = values["base_color"]
    library.set_material_instance_vector_parameter_value(instance, VALUE_PARAMETERS["base_color"],
                                                         unreal.LinearColor(red, green, blue, 1.0))
    for channel in ("roughness", "metallic", "opacity"):
        if channel in PARENT_CHANNELS[entry["blend"]]:
            library.set_material_instance_scalar_parameter_value(instance, VALUE_PARAMETERS[channel],
                                                                 float(values[channel]))
    set_two_sided(instance, entry["two_sided"])
    library.update_material_instance(instance)


# Overrides the instance's two sided setting.
def set_two_sided(instance, two_sided):
    overrides = ue.try_get(instance, "base_property_overrides")
    if overrides is None:
        ue.warn("this engine has no material instance overrides; %s keeps its parent's sides" % instance.get_name())
        return
    ue.try_set(overrides, "override_two_sided", True)
    ue.try_set(overrides, "two_sided", bool(two_sided))
    ue.try_set(instance, "base_property_overrides", overrides)
