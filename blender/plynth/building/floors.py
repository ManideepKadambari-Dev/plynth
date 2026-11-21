import bmesh

from . import Step, count
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 30


# Lays a floor and a ceiling in every room that closes, one object each, from the room polygons.
def run(ctx):
    for floor in ctx.params["floors"]:
        collection = ctx.state["collections"][floor["id"]]
        for room in floor["rooms"]:
            make_surface(room, floor, collection, "Floor", room["floor_z"], True)
            make_surface(room, floor, collection, "Ceiling", room["ceiling_z"], False)
            count(ctx, "rooms")


# Makes the floor or the ceiling object of one room.
def make_surface(room, floor, collection, kind, z, up):
    bm = bmesh.new()
    for polygon in room["polygons"]:
        meshes.add_flat(bm, polygon["outline"], polygon["holes"], z, up)
    name = "SM_%s_%s_%s" % (floor["id"], room["id"], kind)
    obj = meshes.make_object(name, bm, collection, room["id"], kind.lower())
    meshes.add_slots(obj, [kind])
    return obj


BUILD_STEPS.register("floors", Step(ORDER, "Laying floors and ceilings", run))
