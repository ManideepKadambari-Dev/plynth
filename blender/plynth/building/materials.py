import os

import bmesh
import bpy

from . import Step, count, report, slot_table
from .. import shading, surfaces
from ..jobs import WorkerError
from ..registry import BUILD_STEPS

ORDER = 80
NEUTRAL_TILE = (1.0, 1.0)


# Returns the objects the build made, which are the mesh objects with a plynth kind other than items and
# collision meshes.
def built_objects():
    return [o for o in bpy.data.objects
            if o.type == "MESH" and "plynth_kind" in o and o["plynth_kind"] not in ("item", "collision")]


# Deletes the neutral look images an earlier build wrote into the folder, so only the ones this build
# needs are left. Files with other names are not touched.
def clear_neutral_images(folder):
    for surface in surfaces.LOOKS:
        for channel in shading.CHANNELS:
            path = os.path.join(folder, "%s_%s.png" % (surface, channel))
            try:
                if os.path.isfile(path):
                    os.remove(path)
            except OSError:
                pass


# Makes a Blender material for every slot the built objects use: the library material the slot shows, from its
# image maps, or the neutral look of the slot's surface as constant images written to the textures folder.
# A map that will not load gives material_failed and the neutral look. Then every UV is divided by the tile
# size of its material, so a tile is one repeat of the image. The result is kept in ctx.state for the
# materials file, with the tile size of every library material, so the kit uses the size the walls were built
# with.
def run(ctx):
    library = {m["id"]: m for m in ctx.params.get("materials", [])}
    slots = slot_table(ctx)
    objects = built_objects()
    used = []
    for obj in objects:
        for mat in obj.data.materials:
            if mat is not None and mat.name not in used:
                used.append(mat.name)
    folder = os.path.join(ctx.params["output_folder"], ctx.params.get("textures_folder", "textures"))
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError as error:
        raise WorkerError("save_failed", "The textures folder could not be made: %s" % error)
    clear_neutral_images(folder)
    built = []
    tiles = {}
    for name in sorted(used):
        mat = bpy.data.materials[name]
        slot = slots.get(name, {"name": name, "material": None, "surface": "Wall_Inside"})
        entry = library.get(slot["material"]) if slot["material"] else None
        maps = None
        surface = None
        tile = NEUTRAL_TILE
        if entry is not None:
            try:
                maps = {k: (v.replace("\\", "/") if v else None) for k, v in entry["maps"].items()}
                shading.apply_maps(mat, maps)
                tile = (entry["tile_size"]["width"], entry["tile_size"]["height"])
            except shading.MapError as error:
                report(ctx, "material_failed", "warning", "The material %s could not be loaded: %s" % (
                    slot["material"], error), None, None)
                maps = None
        if maps is None:
            surface = slot["surface"] if slot["surface"] in surfaces.LOOKS else "Wall_Inside"
            maps = shading.apply_neutral(mat, surface, folder)
        tiles[name] = tile
        entry_out = {
            "name": name,
            "material": slot["material"] if surface is None else None,
            "surface": surface,
            "maps": maps,
            "alpha_mode": "blend" if maps.get("opacity") else "opaque",
        }
        if surface is None:
            entry_out["tile_size"] = {"width": tile[0], "height": tile[1]}
        built.append(entry_out)
        count(ctx, "materials")
    scale_uvs(objects, tiles)
    ctx.state["built_materials"] = built
    ctx.state["built_objects"] = [{"name": o.name, "slots": [m.name if m else "" for m in o.data.materials]}
                                  for o in sorted(objects, key=lambda o: o.name)]


# Divides the UVs of every face by the tile size of its material.
def scale_uvs(objects, tiles):
    for obj in objects:
        names = [m.name if m else None for m in obj.data.materials]
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        uv = bm.loops.layers.uv.get("UVMap")
        if uv is not None:
            for face in bm.faces:
                width, height = tiles.get(names[face.material_index], NEUTRAL_TILE)
                for loop in face.loops:
                    loop[uv].uv = (loop[uv].uv.x / width, loop[uv].uv.y / height)
            bm.to_mesh(obj.data)
        bm.free()


BUILD_STEPS.register("materials", Step(ORDER, "Applying materials", run))
