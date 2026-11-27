import os
import shutil
import tempfile

import unreal

from .. import importing, pngs, texture_settings, ue
from ..context import value_hash
from ..errors import LinkError
from ..parameters import (DEFAULT_TEXTURES, MAP_PARAMETERS, PARENT_CHANNELS, PARENTS, VALUE_PARAMETERS,
                          default_for)
from ..registry import step

# Changes when the parent graphs change, so existing parents are rebuilt once.
GRAPH_VERSION = "parent-graph-1"

BLEND_MODES = {"opaque": "BLEND_OPAQUE", "masked": "BLEND_MASKED", "translucent": "BLEND_TRANSLUCENT"}

# The material output each channel goes to, by blend.
OUTPUTS = {
    "base_color": "MP_BASE_COLOR",
    "normal": "MP_NORMAL",
    "roughness": "MP_ROUGHNESS",
    "metallic": "MP_METALLIC",
    "occlusion": "MP_AMBIENT_OCCLUSION",
}
OPACITY_OUTPUTS = {"masked": "MP_OPACITY_MASK", "translucent": "MP_OPACITY"}


# Makes the parents' default textures and the three parent materials in Materials/.
@step("parents", 60, "material_failed", applies=lambda ctx: ctx.pack)
def run(ctx):
    ue.make_folder(ctx.materials_folder)
    defaults = default_textures(ctx)
    for blend, name in PARENTS.items():
        ctx.parents[blend] = parent_material(ctx, blend, name, defaults)


# Makes the small default textures the parents' samplers start with and returns them by role.
def default_textures(ctx):
    made = {}
    scratch = None
    try:
        for role, (name, rgb, channel, srgb) in DEFAULT_TEXTURES.items():
            path = ctx.materials_folder + "/" + name
            source_hash = value_hash("default texture", rgb, channel, srgb)
            asset = ue.load(path)
            if ctx.unchanged(asset, source_hash):
                ctx.claim(asset, path, "unchanged")
            else:
                if scratch is None:
                    scratch = tempfile.mkdtemp(prefix="plynth_unreal_")
                filename = os.path.join(scratch, name + ".png")
                pngs.write_solid(filename, rgb)
                action = "created" if asset is None else "updated"
                asset = importing.import_texture(filename, ctx.materials_folder, name)
                texture_settings.apply(asset, channel, srgb)
                ctx.claim(asset, path, action, source_hash)
            made[role] = asset
    finally:
        if scratch is not None:
            shutil.rmtree(scratch, ignore_errors=True)
    return made


# Returns the parent material for a blend, made or rebuilt when it is missing or out of date.
def parent_material(ctx, blend, name, defaults):
    path = ctx.materials_folder + "/" + name
    source_hash = value_hash("parent", blend, GRAPH_VERSION)
    material = ue.load(path)
    if ctx.unchanged(material, source_hash) and isinstance(material, unreal.Material):
        ctx.claim(material, path, "unchanged")
        return material
    action = "created" if material is None else "updated"
    if material is None:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset(name, ctx.materials_folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise LinkError("material_failed", "Unreal Engine could not make the material %s." % path)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    build_graph(material, blend, defaults)
    ctx.claim(material, path, action, source_hash)
    return material


# Adds one expression to a material at a place in its graph.
def node(material, expression_class, x, y):
    made = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if made is None:
        raise LinkError("material_failed", "Unreal Engine could not add a %s to a Plynth material."
                        % expression_class.__name__)
    return made


# Connects one expression's output to another's input, or fails the job.
def connect(source, output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, output, target, target_input):
        raise LinkError("material_failed", "Unreal Engine could not connect the Plynth material graph.")


# Returns the expression a channel uses when it has no map: a parameter, or a fixed value.
def fallback(material, channel, x, y):
    if channel == "base_color":
        made = node(material, unreal.MaterialExpressionVectorParameter, x, y)
        made.set_editor_property("parameter_name", VALUE_PARAMETERS[channel])
        made.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1.0))
        return made
    if channel in VALUE_PARAMETERS:
        made = node(material, unreal.MaterialExpressionScalarParameter, x, y)
        made.set_editor_property("parameter_name", VALUE_PARAMETERS[channel])
        made.set_editor_property("default_value", {"roughness": 0.5, "metallic": 0.0, "opacity": 1.0}[channel])
        return made
    if channel == "normal":
        made = node(material, unreal.MaterialExpressionConstant3Vector, x, y)
        made.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
        return made
    made = node(material, unreal.MaterialExpressionConstant, x, y)
    made.set_editor_property("r", 1.0)
    return made


# Builds a parent graph: for each channel a static switch picks its map or its value.
def build_graph(material, blend, defaults):
    blend_mode = ue.enum_value("BlendMode", BLEND_MODES[blend])
    if blend_mode is not None:
        material.set_editor_property("blend_mode", blend_mode)
    if blend == "translucent":
        lighting = ue.enum_value("TranslucencyLightingMode", "TLM_SURFACE_PER_PIXEL_LIGHTING")
        if lighting is not None:
            ue.try_set(material, "translucency_lighting_mode", lighting)
    for row, channel in enumerate(PARENT_CHANNELS[blend]):
        switch_name, map_name, output = MAP_PARAMETERS[channel]
        y = row * 320
        sampler = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
        sampler.set_editor_property("parameter_name", map_name)
        sampler.set_editor_property("texture", defaults[default_for(channel)])
        sampler_type = texture_settings.sampler(channel)
        if sampler_type is not None:
            sampler.set_editor_property("sampler_type", sampler_type)
        value = fallback(material, channel, -900, y + 200)
        switch = node(material, unreal.MaterialExpressionStaticSwitchParameter, -400, y)
        switch.set_editor_property("parameter_name", switch_name)
        switch.set_editor_property("default_value", False)
        connect(sampler, output, switch, "True")
        connect(value, "", switch, "False")
        target = OUTPUTS.get(channel) or OPACITY_OUTPUTS[blend]
        prop = ue.enum_value("MaterialProperty", target)
        if prop is None or not unreal.MaterialEditingLibrary.connect_material_property(switch, "", prop):
            raise LinkError("material_failed", "Unreal Engine could not connect %s of a Plynth material." % target)
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
