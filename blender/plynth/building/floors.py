import bmesh

from . import Step, count, slot_name
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 30


# Lays a floor and a ceiling in every room that closes, one object each, from the room polygons.
def run(ctx):
    for floor in ctx.params["floors"]:
        collection = ctx.state["collections"][floor["id"]]
        for room in floor["rooms"]:
            make_surface(ctx, room, floor, collection, "Floor", room["floor_z"], True)
            make_surface(ctx, room, floor, collection, "Ceiling", room["ceiling_z"], False)
            count(ctx, "rooms")


# Makes the floor or the ceiling object of one room: world UVs, mirrored in Y for ceilings.
def make_surface(ctx, room, floor, collection, kind, z, up):
    bm = bmesh.new()
    name = slot_name(ctx, room["%s_material" % kind.lower()], kind)
    for polygon in room["polygons"]:
        faces = meshes.add_flat(bm, polygon["outline"], polygon["holes"], z, up)
        meshes.world_uvs(bm, faces, flip_y=not up)
    obj = meshes.make_object("SM_%s_%s_%s" % (floor["id"], room["id"], kind), bm, collection, room["id"],
                             kind.lower())
    meshes.add_slots(obj, [name])
    return obj


BUILD_STEPS.register("floors", Step(ORDER, "Laying floors and ceilings", run))
