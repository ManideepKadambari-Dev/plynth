import bpy

from . import Profile
from ..name_objects import (ENGINE_COLLISION_PREFIX, ENGINE_MESH_PREFIX, name_materials, plan_plain_names,
                            rename_meshes)
from ...registry import EXPORT_PROFILES


# Takes the collision meshes out of the scene and the export, since a plain FBX has no collision.
def drop_collision(ctx):
    for obj in ctx.state["collision"]:
        bpy.data.objects.remove(obj, do_unlink=True)
    ctx.state["collision"] = []


# Keeps the build's plain names, with odd characters replaced and repeats made unique, names the materials and
# leaves the collision out.
def name_objects(ctx):
    drop_collision(ctx)
    rename_meshes(plan_plain_names(ctx.state["meshes"]), {})
    ctx.state["materials"] = name_materials(ctx, ctx.state["meshes"])


# Leaves every object as it is: each one goes into its group's file on its own.
def finish_objects(ctx):
    return None


EXPORT_PROFILES.register("plain", Profile(
    name="the plain FBX", collision=False, kit=False, texture_prefix="", mesh_prefix="", collision_prefix=None,
    refused_prefixes=(ENGINE_MESH_PREFIX, ENGINE_COLLISION_PREFIX), name_objects=name_objects,
    finish_objects=finish_objects))
