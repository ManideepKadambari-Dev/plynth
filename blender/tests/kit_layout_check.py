"""Places the modular kit by its layout file and compares it with the exported walls.

Run: blender --background --factory-startup --python kit_layout_check.py -- <export folder> [<renders folder>]

The export folder holds the kit file, the layout file and either the walls group file or the single file.
Prints one line, KIT_LAYOUT_CHECK followed by a JSON object, and renders two Eevee pictures when a renders
folder is given. Exits with 0 when the check ran, 1 when its files were missing or unreadable.
"""
import glob
import json
import math
import os
import sys

import bpy
from mathutils import Vector

BOUNDS_TOLERANCE = 0.001
AREA_TOLERANCE = 1e-3  # FBX stores float32 coordinates, so 136 copies drift by about 1e-4
TOP_BAND = 0.05


# Returns the arguments after the "--" of the command line.
def script_args():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


# Returns the layout file, the kit file and the file with the walls of an export folder, or raises ValueError.
def find_files(folder):
    layouts = sorted(glob.glob(os.path.join(folder, "*_Kit_layout.json")))
    if not layouts:
        raise ValueError("The folder has no kit layout file.")
    with open(layouts[0], "r", encoding="utf-8") as handle:
        layout = json.load(handle)
    kit = os.path.join(folder, layout["kit_file"])
    if not os.path.isfile(kit):
        raise ValueError("The kit file %s is missing." % layout["kit_file"])
    walls = sorted(glob.glob(os.path.join(folder, "*_Walls.fbx")))
    if not walls:
        others = [p for p in sorted(glob.glob(os.path.join(folder, "*.fbx"))) if not p.endswith("_Kit.fbx")]
        walls = others[:1]
    if not walls:
        raise ValueError("The folder has no walls file or single file.")
    return layout, layout["kit_file"], kit, walls[0]


# Imports an FBX file and returns the mesh objects it added, without the UCX_ collision.
def import_meshes(path):
    before = set(bpy.data.objects.keys())
    bpy.ops.import_scene.fbx(filepath=path)
    bpy.context.view_layer.update()
    added = [o for o in bpy.data.objects if o.name not in before and o.type == "MESH"]
    return [o for o in added if not o.name.startswith("UCX_")]


# Returns the world bounds (low, high) of the objects.
def bounds_of(objects):
    low = [float("inf")] * 3
    high = [float("-inf")] * 3
    for obj in objects:
        for corner in obj.bound_box:
            point = obj.matrix_world @ Vector(corner)
            for axis in range(3):
                low[axis] = min(low[axis], point[axis])
                high[axis] = max(high[axis], point[axis])
    return low, high


# Returns the summed area seen from above of the faces that look up within TOP_BAND below the given height.
# The rounded top edges are counted too, so the number is the area of the wall tops whatever the bevel does.
def top_area(objects, height):
    total = 0.0
    for obj in objects:
        matrix = obj.matrix_world
        for polygon in obj.data.polygons:
            if polygon.normal.z > 0.0 and (matrix @ polygon.center).z >= height - TOP_BAND:
                total += polygon.area * polygon.normal.z
    return total


# Puts a copy of every piece at each placement of the layout and returns the copies. Pieces missing from the
# kit file are listed in missing.
def place_pieces(layout, pieces, collection, missing):
    placed = []
    for placement in layout["placements"]:
        source = pieces.get(placement["mesh"])
        if source is None:
            if placement["mesh"] not in missing:
                missing.append(placement["mesh"])
            continue
        obj = bpy.data.objects.new("%s_%d" % (placement["mesh"], len(placed)), source.data)
        obj.location = placement["location"]
        obj.rotation_euler = (0.0, 0.0, math.radians(placement["rotation_z_degrees"]))
        collection.objects.link(obj)
        placed.append(obj)
    bpy.context.view_layer.update()
    return placed


# Renders the scene from above and at three quarters into the folder. The same three quarter view of the walls
# file follows, for comparison, when walls and placed are given.
def render(folder, low, high, walls=(), placed=(), close=None):
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = 1280, 720
    scene.render.image_settings.file_format = "PNG"
    world = bpy.data.worlds.new("kit_check")
    world.color = (0.62, 0.66, 0.70)
    scene.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35))
    sun.data.energy = 4.0
    scene.collection.objects.link(sun)
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    centre = Vector([(a + b) / 2.0 for a, b in zip(low, high)])
    span = max(high[0] - low[0], high[1] - low[1])
    views = (("kit_layout_top.png", centre + Vector((0.0, 0.0, 1.8 * span)), "ORTHO"),
             ("kit_layout_view.png", centre + Vector((0.55 * span, -1.0 * span, 0.9 * span)), "PERSP"))
    for name, location, mode in views:
        camera.data.type = mode
        camera.data.ortho_scale = span * 1.1
        camera.data.lens = 28
        camera.data.clip_end = 1000.0
        camera.location = location
        camera.rotation_euler = (centre - location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(folder, name)
        bpy.ops.render.render(write_still=True)
    if walls:
        for obj in placed:
            obj.hide_render = True
        for obj in walls:
            obj.hide_render = False
        scene.render.filepath = os.path.join(folder, "walls_view.png")
        bpy.ops.render.render(write_still=True)
    if close is not None:
        render_close(folder, close, placed, walls)


# Returns the placement of the corner piece with the most faces (the T junction in a plan with partitions), or None.
def junction_placement(layout, pieces):
    best = None
    for placement in layout["placements"]:
        source = pieces.get(placement["mesh"])
        if source is None or "Corner" not in placement["mesh"]:
            continue
        if best is None or len(source.data.polygons) > best[0]:
            best = (len(source.data.polygons), placement)
    return best[1] if best else None


# Renders one placement close up from its +Y side and a little along X (the room side of a T junction, seen
# at an angle so the partition shows), once with the placed pieces
# and once with the walls file.
def render_close(folder, placement, placed, walls):
    scene = bpy.context.scene
    camera = scene.camera
    turn = math.radians(placement["rotation_z_degrees"])
    base = Vector(placement["location"])
    side = Vector((-math.sin(turn), math.cos(turn), 0.0))
    along = Vector((math.cos(turn), math.sin(turn), 0.0))
    camera.data.type = "PERSP"
    camera.data.lens = 35
    camera.location = base + side * 3.0 + along * 2.2 + Vector((0.0, 0.0, 3.4))
    target = base + side * 0.2 + Vector((0.0, 0.0, 1.2))
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    for name, show, hide in (("tjunction_kit.png", placed, walls), ("tjunction_walls.png", walls, placed)):
        for obj in hide:
            obj.hide_render = True
        for obj in show:
            obj.hide_render = False
        scene.render.filepath = os.path.join(folder, name)
        bpy.ops.render.render(write_still=True)


# Runs the check and returns the answer.
def check(folder, renders):
    layout, kit_name, kit_path, walls_path = find_files(folder)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    kit_objects = import_meshes(kit_path)
    pieces = {obj.name: obj for obj in kit_objects}
    wall_objects = import_meshes(walls_path)
    collection = bpy.data.collections.new("placed")
    bpy.context.scene.collection.children.link(collection)
    missing = []
    placed = place_pieces(layout, pieces, collection, missing)
    for obj in bpy.data.objects:
        if obj not in placed and obj not in wall_objects:
            obj.hide_render = True
            obj.hide_viewport = True
    wall_low, wall_high = bounds_of(wall_objects)
    kit_low, kit_high = bounds_of(placed)
    height = max(wall_high[2], kit_high[2])
    wall_area = top_area([o for o in wall_objects if o.name.endswith("_Walls")], height)
    kit_area = top_area(placed, height)
    differences = [abs(a - b) for a, b in zip(wall_low + wall_high, kit_low + kit_high)]
    answer = {
        "placements": len(layout["placements"]),
        "placed": len(placed),
        "missing_pieces": missing,
        "walls_bounds": {"min": wall_low, "max": wall_high},
        "kit_bounds": {"min": kit_low, "max": kit_high},
        "bounds_difference": max(differences),
        "walls_top_area": wall_area,
        "kit_top_area": kit_area,
        "area_difference": abs(wall_area - kit_area),
        "visible_extra": [o.name for o in bpy.data.objects if o.type == "MESH" and not o.hide_render
                          and o not in placed and o not in wall_objects],
    }
    answer["ok"] = (not missing and len(placed) == len(layout["placements"])
                    and answer["bounds_difference"] <= BOUNDS_TOLERANCE
                    and answer["area_difference"] <= AREA_TOLERANCE)
    if renders:
        for obj in wall_objects:
            obj.hide_render = True
            obj.hide_viewport = True
        close = junction_placement(layout, pieces)
        render(renders, kit_low, kit_high, wall_objects, placed, close)
        answer["renders"] = ["kit_layout_top.png", "kit_layout_view.png", "walls_view.png"]
        if close is not None:
            answer["renders"] += ["tjunction_kit.png", "tjunction_walls.png"]
            answer["junction"] = {"piece": close["piece"], "location": close["location"],
                                  "rotation_z_degrees": close["rotation_z_degrees"]}
    return answer


# Reads the arguments, runs the check and prints the answer.
def main():
    args = script_args()
    if not args:
        print("KIT_LAYOUT_CHECK " + json.dumps({"ok": False, "error": "Give the export folder."}))
        sys.exit(1)
    try:
        answer = check(args[0], args[1] if len(args) > 1 else None)
    except (ValueError, OSError, KeyError) as error:
        print("KIT_LAYOUT_CHECK " + json.dumps({"ok": False, "error": str(error)}))
        sys.exit(1)
    print("KIT_LAYOUT_CHECK " + json.dumps(answer))


if __name__ == "__main__":
    main()
