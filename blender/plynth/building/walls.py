import math

import bmesh

from . import Step, count, slot_name
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 10
SIDE_CODES = {"left": 0, "right": 1, None: 2}
TOP_SIDE = 3


# Raises the walls of every floor: one mesh per floor from all its wall solids. Side faces carry the wall,
# the room they look into and the side as face attributes, a material through their slot and UVs in metres.
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


# Returns, for every ring edge counted over the outline and then each hole, the distance along its ring from
# the ring's first point to the start of the edge, and the edge length.
def ring_distances(rings):
    result = []
    for ring in rings:
        points = ring["points"]
        along = 0.0
        for i, p in enumerate(points):
            q = points[(i + 1) % len(points)]
            length = math.hypot(q[0] - p[0], q[1] - p[1])
            result.append((along, length))
            along += length
    return result


# Builds the walls object of one floor.
def make_walls(ctx, floor):
    bm = bmesh.new()
    wall_layer = bm.faces.layers.int.new("plynth_wall")
    room_layer = bm.faces.layers.int.new("plynth_room")
    side_layer = bm.faces.layers.int.new("plynth_side")
    uv = meshes.uv_layer(bm)
    walls = []
    rooms = []
    slots = []
    elevation = floor["elevation"]
    for solid in floor["wall_solids"]:
        rings = [solid["outline"]] + list(solid["holes"])
        points = [ring["points"] for ring in rings]
        edges = [edge for ring in rings for edge in ring["edges"]]
        distances = ring_distances(rings)
        top_name = slot_name(ctx, solid["top_material"], "Wall_Top")
        for face, kind, edge_index in meshes.add_prism(bm, points, elevation, elevation + solid["height"]):
            if kind == "side":
                edge = edges[edge_index]
                face[wall_layer] = index_of(walls, edge["wall"])
                face[room_layer] = index_of(rooms, edge["room"])
                face[side_layer] = SIDE_CODES[edge["side"]]
                if edge["side"] is None:
                    surface = "Wall_Top"
                elif edge["room"] is not None:
                    surface = "Wall_Inside"
                else:
                    surface = "Wall_Outside"
                name = slot_name(ctx, edge["material"], surface)
                along, length = distances[edge_index]
                for k, loop in enumerate(face.loops):
                    u = along if k in (0, 3) else along + length
                    loop[uv].uv = (u, loop.vert.co.z - elevation)
            else:
                face[wall_layer] = -1
                face[room_layer] = -1
                face[side_layer] = TOP_SIDE
                name = top_name
                for loop in face.loops:
                    loop[uv].uv = (loop.vert.co.x, loop.vert.co.y)
            face.material_index = index_of(slots, name)
        count(ctx, "wall_solids")

    collection = ctx.state["collections"][floor["id"]]
    obj = meshes.make_object("SM_%s_Walls" % floor["id"], bm, collection, floor["id"], "walls")
    meshes.add_slots(obj, slots)
    obj["plynth_walls"] = walls
    obj["plynth_rooms"] = rooms
    ctx.state.setdefault("walls_objects", {})[floor["id"]] = obj


BUILD_STEPS.register("walls", Step(ORDER, "Raising walls", run))
