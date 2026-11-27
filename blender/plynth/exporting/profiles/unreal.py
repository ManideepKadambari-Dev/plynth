import os

import bpy

from . import Profile
from .. import COLLISION_KIND, sanitize, unique_name, will_write
from ..groups import KIT_KIND, assign
from ..name_objects import (ENGINE_COLLISION_PREFIX, ENGINE_MESH_PREFIX, name_materials, plan_mesh_names,
                            rename_meshes)
from ... import scene
from ...meshes import UV_NAME
from ...registry import EXPORT_PROFILES

JOINED_KIND = "group"


# Names every mesh SM_<name> and every collision mesh UCX_<its mesh>_<NN>, and names the materials. The scene
# checks then see each built object on its own; finish_objects joins them afterwards.
def name_objects(ctx):
    wanted, new_of = plan_mesh_names(ctx.state["meshes"], ctx.state["collision"])
    rename_meshes(wanted, new_of)
    ctx.state["materials"] = name_materials(ctx, ctx.state["meshes"])


# Gives the UV map each part shows (or its first) the one shared name, so joining keeps every part's UVs in the
# same map. A different map that already has the name is renamed out of the way.
def share_uv_names(parts):
    for obj in parts:
        layers = obj.data.uv_layers
        if not len(layers):
            continue
        main = next((layer for layer in layers if layer.active_render), layers[0])
        if main.name == UV_NAME:
            continue
        other = layers.get(UV_NAME)
        if other is not None:
            other.name = UV_NAME + "_Other"
        main.name = UV_NAME


# Joins the parts into one mesh object with the name and returns it. A part whose first UV map is the shared
# one leads the join, so that map stays the first of the result.
def join_parts(parts, name, group_id):
    share_uv_names(parts)
    lead = next((o for o in parts if len(o.data.uv_layers) and o.data.uv_layers[0].name == UV_NAME), parts[0])
    ordered = [lead] + [o for o in parts if o != lead]
    if len(ordered) > 1:
        with bpy.context.temp_override(active_object=lead, object=lead, selected_objects=ordered,
                                       selected_editable_objects=ordered):
            bpy.ops.object.join()
    for key in [k for k in lead.keys() if k.startswith("plynth_")]:
        del lead[key]
    lead.name = name
    lead.data.name = name
    lead["plynth_kind"] = JOINED_KIND
    lead["plynth_id"] = group_id
    layer = lead.data.uv_layers.get(UV_NAME)
    if layer is not None:
        lead.data.uv_layers.active = layer
        layer.active_render = True
    return lead


# Joins the objects of every group into one mesh, SM_<file stem>: SM_<Stem> for a single export and
# SM_<Stem>_<Group> for a grouped one, as the group's file is named. The group's collision meshes become
# UCX_<that mesh>_<NN>, and its lights stay as they are. A group's collision without any mesh is left out. Kit
# pieces stay apart. Runs only when the files will be written. Keeps the bounds the walls had in each joined
# mesh for the file check.
def finish_objects(ctx):
    if not will_write(ctx):
        return
    params = ctx.params
    kit = [o for o in ctx.state["meshes"] if o.get("plynth_kind") == KIT_KIND]
    kit_collision = [o for o in ctx.state["collision"] if o.get("plynth_kind") == KIT_KIND]
    found, _ = assign(params["groups"], ctx.state["meshes"] + ctx.state.get("lights", []), ctx.state["collision"])
    moving = [o for objects in found.values() for o in objects if o.type == "MESH"]
    for number, obj in enumerate(moving):
        obj.name = "__plynth_join_%d" % number
    taken = {o.name.lower() for o in bpy.data.objects if o not in moving}
    meshes, collision, by_group, wall_bounds = [], [], {}, {}
    for group in params["groups"]:
        objects = found[group["id"]]
        parts = [o for o in objects if o.type == "MESH" and o.get("plynth_kind") != COLLISION_KIND]
        hulls = [o for o in objects if o.type == "MESH" and o.get("plynth_kind") == COLLISION_KIND]
        written = [o for o in objects if o.type == "LIGHT"]
        if not parts:
            for hull in hulls:
                bpy.data.objects.remove(hull, do_unlink=True)
            by_group[group["id"]] = written
            continue
        walls = [o for o in parts if o.get("plynth_kind") == "walls"]
        bounds = scene.world_bounds(walls) if walls else None
        stem = sanitize(os.path.splitext(group["file"])[0])
        name = unique_name(ENGINE_MESH_PREFIX + stem, taken)
        joined = join_parts(parts, name, group["id"])
        if bounds is not None:
            wall_bounds[name] = (list(bounds[0]), list(bounds[1]))
        meshes.append(joined)
        for number, hull in enumerate(hulls):
            hull_name = unique_name("%s%s_%02d" % (ENGINE_COLLISION_PREFIX, name, number), taken)
            hull.name = hull_name
            hull.data.name = hull_name
            hull["plynth_collision_for"] = name
            collision.append(hull)
        by_group[group["id"]] = [joined] + hulls + written
    ctx.state["meshes"] = meshes + kit
    ctx.state["collision"] = collision + kit_collision
    ctx.state["group_objects"] = by_group
    ctx.state["joined_wall_bounds"] = wall_bounds


EXPORT_PROFILES.register("unreal", Profile(
    name="Unreal Engine", collision=True, lights=True, kit=True, texture_prefix="T_", mesh_prefix=ENGINE_MESH_PREFIX,
    collision_prefix=ENGINE_COLLISION_PREFIX, refused_prefixes=(), name_objects=name_objects,
    finish_objects=finish_objects))
