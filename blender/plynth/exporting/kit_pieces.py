import copy
import math
from types import SimpleNamespace

import bmesh
import bpy
from mathutils import Matrix

from . import Step, sanitize
from .. import fills as fills_package
from .. import meshes, registry, surfaces
from ..building import bevel as build_bevel
from ..building import collision as build_collision
from ..building import materials as build_materials
from ..building import opening_fills, openings, triangulate
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS

ORDER = 35
KIT_KIND = "kit"
COLLECTION = "Kit"
SIDE_CODES = {"left": 0, "right": 1, None: 2}
TOP_SIDE = 3
BEVEL_ANGLE = math.radians(30.0)
WEIGHT = "bevel_weight_edge"
CUT_LAYER = "plynth_kit_cut"
FILL_SLOTS = {"door": ("door", "frame", "handle"), "window": ("frame", "glass")}
NEUTRAL_LOOKS = (("Exterior", "Wall_Outside"), ("Top", "Wall_Top"), ("Reveal", "Reveal"), ("Door", "Door"),
                 ("Frame", "Frame"), ("Glass", "Glass"), ("Handle", "Handle"))


# Makes the unique pieces of the modular kit from the job's kit and keeps them in ctx.state for the later steps:
# kit_objects (piece meshes and their UCX_ meshes) and kit_pieces (the job's pieces by mesh name). The piece
# meshes and collision are also added to the lists the quality checks read, so they are checked like the rest of
# the building. Does nothing when the job has no kit.
def run(ctx):
    kit = ctx.params.get("kit")
    ctx.state["kit_objects"] = []
    ctx.state["kit_pieces"] = {}
    ctx.state["kit_output"] = None
    if not kit:
        return
    collection = kit_collection()
    shim = SimpleNamespace(
        params={"settings": {"bevel_width": kit["bevel"]["width"], "bevel_segments": kit["bevel"]["segments"],
                             "fill_openings": kit["fill_openings"]}, "slots": [], "floors": []},
        state={"collections": {"kit": collection}, "problems": [], "current_step": "kit_pieces"})
    registry.discover(fills_package)
    for piece in kit["pieces"]:
        try:
            made, hulls = make_piece(ctx, shim, kit, piece, collection)
        except WorkerError:
            raise
        except Exception as error:
            raise WorkerError("export_failed", "The kit piece %s could not be made: %s" % (piece["id"], error))
        ctx.state["kit_objects"] += [made] + hulls
        ctx.state["kit_pieces"][piece["mesh"]] = piece
        ctx.state["meshes"].append(made)
        ctx.state["collision"] += hulls
    ctx.state["kit_problems"] = shim.state["problems"]


# Returns the Kit collection, linked under the scene (not under the building's collections), made on first use.
def kit_collection():
    found = bpy.data.collections.get(COLLECTION)
    if found is None:
        found = bpy.data.collections.new(COLLECTION)
        bpy.context.scene.collection.children.link(found)
    return found


# Returns the surface look a slot with no material gets, by the words in its name.
def neutral_look(slot):
    for word, look in NEUTRAL_LOOKS:
        if word in slot:
            return look
    return "Wall_Inside"


# Adds a material the building meshes did not already bring to the export's materials, so its textures are
# collected and checked and its tile size is known.
def register_known(ctx, material):
    if any(info["material"] == material for info in ctx.state["materials"].values()):
        return
    entries = {entry["name"]: entry for entry in ctx.params.get("materials", []) if "name" in entry}
    ctx.state["materials"][material.name] = {"material": material, "original": material.name,
                                             "entry": entries.get(material.name)}


# Returns the name of the Blender material a slot uses: the material the build made under that name, else the
# same with odd characters replaced (the export names), else a new one with the neutral look, which the
# materials check then reports.
def resolve_slot(ctx, slot):
    for name in (slot, sanitize(slot)):
        if name in bpy.data.materials:
            register_known(ctx, bpy.data.materials[name])
            return name
    clean = sanitize(slot)
    look = neutral_look(slot)
    material = surfaces.material(clean)
    surfaces.apply_look(material, surfaces.LOOKS[look])
    ctx.state["materials"][clean] = {"material": material, "original": slot,
                                     "entry": {"name": slot, "surface": look}}
    return clean


# Returns the tile size (width, height in metres) of every material the export knows, by Blender material name.
# The build divides the UVs of every face by it, so the kit must too or its textures show at another size.
# A material whose job entry has no tile_size counts as 1 m square, like the neutral look.
def tile_sizes(ctx):
    found = {}
    for name, info in ctx.state["materials"].items():
        size = (info["entry"] or {}).get("tile_size")
        if size:
            found[name] = (size["width"], size["height"])
    return found


# Returns the index of value in items, adding it first when it is new, or -1 for none.
def index_of(items, value):
    if value is None:
        return -1
    if value not in items:
        items.append(value)
    return items.index(value)


# Builds the walls mesh of one piece: the prisms of its solids with the build's face attributes and the kit's UVs
# (side faces: u along the face measured from the piece's origin, v up; tops and bottoms: X and Y). Returns the
# object, linked into the collection.
def make_walls(ctx, piece, collection):
    bm = bmesh.new()
    wall_layer = bm.faces.layers.int.new("plynth_wall")
    room_layer = bm.faces.layers.int.new("plynth_room")
    side_layer = bm.faces.layers.int.new("plynth_side")
    cut_layer = bm.faces.layers.int.new(CUT_LAYER)
    uv = meshes.uv_layer(bm)
    walls = []
    rooms = []
    slots = []
    for solid in piece["wall_solids"]:
        rings = [solid["outline"]] + list(solid["holes"])
        points = [ring["points"] for ring in rings]
        edges = [edge for ring in rings for edge in ring["edges"]]
        ends = []
        for ring in points:
            for i, p in enumerate(ring):
                ends.append((p, ring[(i + 1) % len(ring)]))
        top_name = resolve_slot(ctx, solid["top_slot"])
        for face, kind, edge_index in meshes.add_prism(bm, points, 0.0, solid["height"]):
            if kind == "side":
                edge = edges[edge_index]
                face[wall_layer] = index_of(walls, edge["wall"])
                face[room_layer] = index_of(rooms, edge["room"])
                face[side_layer] = SIDE_CODES[edge["side"]]
                face[cut_layer] = 1 if edge.get("kit_cut") else 0
                name = resolve_slot(ctx, edge["slot"])
                (px, py), (qx, qy) = ends[edge_index]
                length = math.hypot(qx - px, qy - py)
                dx, dy = ((qx - px) / length, (qy - py) / length) if length > 0 else (1.0, 0.0)
                for loop in face.loops:
                    loop[uv].uv = (loop.vert.co.x * dx + loop.vert.co.y * dy, loop.vert.co.z)
            else:
                face[wall_layer] = -1
                face[room_layer] = -1
                face[side_layer] = TOP_SIDE
                name = top_name
                for loop in face.loops:
                    loop[uv].uv = (loop.vert.co.x, loop.vert.co.y)
            face.material_index = index_of(slots, name)
    obj = meshes.make_object(piece["mesh"], bm, collection, piece["id"], KIT_KIND)
    meshes.add_slots(obj, slots)
    obj["plynth_walls"] = walls
    obj["plynth_rooms"] = rooms
    return obj


# Returns a copy of an opening with the slots it uses turned into the names of their materials: the reveal, and
# the parts of its fill.
def resolved_opening(ctx, opening):
    found = copy.deepcopy(opening)
    keys = ["reveal"] + list(FILL_SLOTS.get(opening["type"], ()))
    found["slots"] = {key: resolve_slot(ctx, slot) if key in keys else slot for key, slot in opening["slots"].items()}
    return found


# Puts a bevel weight on every edge that is sharp and does not touch a face on a cut line between two pieces, so
# the cut faces stay flat and pieces meet without a groove. The slanted edges the tops and bottoms were split
# with are dissolved first, because an extra edge at the end of a bevel makes a dent in it.
def weigh_edges(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    sides = bm.faces.layers.int.get("plynth_side")
    flat = [e for e in bm.edges if len(e.link_faces) == 2 and all(f[sides] == TOP_SIDE for f in e.link_faces)]
    if flat:
        bmesh.ops.dissolve_edges(bm, edges=flat, use_verts=False, use_face_split=False)
    weights = bm.edges.layers.float.get(WEIGHT) or bm.edges.layers.float.new(WEIGHT)
    cuts = bm.faces.layers.int.get(CUT_LAYER)
    for edge in bm.edges:
        faces = edge.link_faces
        if len(faces) != 2 or any(face[cuts] for face in faces):
            continue
        if edge.calc_face_angle(0.0) >= BEVEL_ANGLE:
            edge[weights] = 1.0
    bm.to_mesh(obj.data)
    bm.free()


# Removes the helper attributes from the finished piece.
def drop_helpers(obj):
    for name in (WEIGHT, CUT_LAYER):
        attribute = obj.data.attributes.get(name)
        if attribute is not None:
            obj.data.attributes.remove(attribute)


# Joins the door or window parts made for a piece into its mesh. The new faces belong to no wall or room.
def join_fills(obj, parts):
    before = len(obj.data.polygons)
    selected = [obj] + parts
    with bpy.context.temp_override(active_object=obj, object=obj, selected_objects=selected,
                                   selected_editable_objects=selected):
        bpy.ops.object.join()
    for name, value in (("plynth_wall", -1), ("plynth_room", -1), ("plynth_side", SIDE_CODES[None])):
        attribute = obj.data.attributes.get(name)
        if attribute is not None:
            for index in range(before, len(obj.data.polygons)):
                attribute.data[index].value = value


# Copies the meshes of a catalog door or window into the piece's place for it, in the piece's frame. Returns the
# copies, or None when the asset's collection is not in building.blend, so the built fill is used instead.
def asset_parts(asset, collection):
    source = bpy.data.collections.get(asset["id"])
    if source is None:
        return None
    sx, sy, sz = asset["scale"]
    placement = (Matrix.Translation(asset["location"]) @ Matrix.Rotation(asset["rotation_z"], 4, "Z")
                 @ Matrix.Diagonal((sx, sy, sz, 1.0)))
    parts = []
    for part in sorted(source.all_objects, key=lambda o: o.name):
        if part.type != "MESH":
            continue
        copy = bpy.data.objects.new(part.name + "_kit", part.data.copy())
        matrix = placement @ part.matrix_world
        copy.data.transform(matrix)
        if matrix.determinant() < 0:
            copy.data.flip_normals()
        collection.objects.link(copy)
        parts.append(copy)
    return parts


# Makes the door and window parts of the openings a piece gets and joins them into the piece. An opening with a
# catalog asset gets copies of that asset instead of the built fill.
def add_fills(ctx, shim, piece, obj):
    floor = {"id": "kit", "elevation": 0.0}
    parts = []
    for opening in piece["openings"]:
        if opening["id"] not in piece["fills"]:
            continue
        if opening.get("asset"):
            copies = asset_parts(opening["asset"], obj.users_collection[0])
            if copies is not None:
                parts += copies
                continue
        entry = opening_fills.entry_for(opening["type"])
        if entry is None:
            continue
        found = resolved_opening(ctx, opening)
        if not found["inner_outline"]:
            continue
        parts += entry.build(shim, floor, found) or []
    if parts:
        join_fills(obj, parts)


# Makes the UCX_ meshes of a piece, in the order the job lists them.
def make_collision(piece, collection):
    hulls = []
    for number, shape in enumerate(piece["collision"]):
        name = "UCX_%s_%02d" % (piece["mesh"], number)
        mesh = build_collision.piece_mesh(name, shape)
        if mesh is None:
            raise WorkerError("export_failed", "The collision piece %s of the kit piece %s could not be made." % (
                name, piece["id"]))
        hull = build_collision.add_object(name, mesh, collection, piece["mesh"], piece["id"])
        hull["plynth_kind"] = KIT_KIND
        hulls.append(hull)
    return hulls


# Makes one piece: walls, the openings cut, the sharp edges rounded, big faces triangulated, doors and windows
# fitted, and its collision. Returns the mesh object and the collision objects.
def make_piece(ctx, shim, kit, piece, collection):
    obj = make_walls(ctx, piece, collection)
    bpy.context.view_layer.update()
    for opening in piece["openings"]:
        if not openings.cut(shim, {"id": "kit"}, obj, resolved_opening(ctx, opening), partial=True):
            raise WorkerError("export_failed", "The opening %s could not be cut into the kit piece %s." % (
                opening["id"], piece["id"]))
    width = kit["bevel"]["width"]
    if width > 0:
        weigh_edges(obj)
        build_bevel.bevel(shim, "kit", obj, width, kit["bevel"]["segments"], weighted=True)
    shim.state["walls_objects"] = {"kit": obj}
    triangulate.run(shim)
    if kit["fill_openings"]:
        add_fills(ctx, shim, piece, obj)
    build_materials.scale_uvs([obj], tile_sizes(ctx))
    drop_helpers(obj)
    return obj, make_collision(piece, collection)


EXPORT_STEPS.register("kit_pieces", Step(ORDER, "Making the kit pieces", run))
