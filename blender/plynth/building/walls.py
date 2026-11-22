import bmesh

from . import Step, count
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 10
SIDE_CODES = {"left": 0, "right": 1, None: 2}
SURFACE_ORDER = ["Wall_Inside", "Wall_Outside", "Wall_Top", "Reveal"]


# Raises the walls of every floor: one mesh per floor from all its wall solids. Side faces carry the wall,
# the room they look into and the side as face attributes, and a surface through their material slot.
def run(ctx):
    for floor in ctx.params["floors"]:
        if floor["wall_solids"]:
            make_walls(ctx, floor)


# Returns the index of value in items, adding it first when it is new, or -1 for none.
def index_of(items, value):
    if value is None:
        return -1
    if value not in items:
        items.append(value)
    return items.index(value)


# Builds the walls object of one floor.
def make_walls(ctx, floor):
    bm = bmesh.new()
    wall_layer = bm.faces.layers.int.new("plynth_wall")
    room_layer = bm.faces.layers.int.new("plynth_room")
    side_layer = bm.faces.layers.int.new("plynth_side")
    walls = []
    rooms = []
    elevation = floor["elevation"]
    for solid in floor["wall_solids"]:
        rings = [solid["outline"]] + list(solid["holes"])
        points = [ring["points"] for ring in rings]
        edges = [edge for ring in rings for edge in ring["edges"]]
        for face, kind, edge_index in meshes.add_prism(bm, points, elevation, elevation + solid["height"]):
            if kind == "side":
                edge = edges[edge_index]
                face[wall_layer] = index_of(walls, edge["wall"])
                face[room_layer] = index_of(rooms, edge["room"])
                face[side_layer] = SIDE_CODES[edge["side"]]
                if edge["side"] is None:
                    name = "Wall_Top"
                elif edge["room"] is not None:
                    name = "Wall_Inside"
                else:
                    name = "Wall_Outside"
            else:
                face[wall_layer] = -1
                face[room_layer] = -1
                face[side_layer] = 3
                name = "Wall_Top"
            face.material_index = SURFACE_ORDER.index(name)
        count(ctx, "wall_solids")

    collection = ctx.state["collections"][floor["id"]]
    obj = meshes.make_object("SM_%s_Walls" % floor["id"], bm, collection, floor["id"], "walls")
    meshes.add_slots(obj, SURFACE_ORDER)
    obj["plynth_walls"] = walls
    obj["plynth_rooms"] = rooms
    ctx.state.setdefault("walls_objects", {})[floor["id"]] = obj


BUILD_STEPS.register("walls", Step(ORDER, "Raising walls", run))
