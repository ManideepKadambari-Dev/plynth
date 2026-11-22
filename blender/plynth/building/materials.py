import os

import bmesh
import bpy

from . import Step, count, report
from .. import shading, surfaces
from ..jobs import WorkerError
from ..registry import BUILD_STEPS

ORDER = 80
NEUTRAL_TILE = (1.0, 1.0)


# Returns the objects the build made, which are the mesh objects with a plynth kind other than items.
def built_objects():
    return [o for o in bpy.data.objects
            if o.type == "MESH" and "plynth_kind" in o and o["plynth_kind"] != "item"]


# Makes a Blender material for every slot name the built objects use: a library material from its image maps,
# or the neutral look of its surface as constant images written to the textures folder. A map that will not
# load gives material_failed and the neutral look. Then every UV is divided by the tile size of its material,
# so a tile is one repeat of the image. The result is kept in ctx.state for the materials file.
def run(ctx):
    library = {m["id"]: m for m in ctx.params.get("materials", [])}
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
    built = []
    tiles = {}
    for name in sorted(used):
        mat = bpy.data.materials[name]
        entry = library.get(name)
        maps = None
        surface = None
        tile = NEUTRAL_TILE
        if entry is not None:
            try:
                maps = {k: (v.replace("\\", "/") if v else None) for k, v in entry["maps"].items()}
                shading.apply_maps(mat, maps)
                tile = (entry["tile_size"]["width"], entry["tile_size"]["height"])
            except shading.MapError as error:
                report(ctx, "material_failed", "warning", "The material %s could not be loaded: %s" % (name, error),
                       None, None)
                maps = None
        if maps is None:
            surface = ctx.state.get("slot_surfaces", {}).get(name, name)
            if surface not in surfaces.LOOKS:
                surface = "Wall_Inside"
            maps = neutral(mat, surface, folder, name, name not in surfaces.LOOKS)
        tiles[name] = tile
        built.append({
            "name": name,
            "material": name if surface is None else None,
            "surface": surface,
            "maps": maps,
            "alpha_mode": "blend" if maps.get("opacity") else "opaque",
        })
        count(ctx, "materials")
    scale_uvs(objects, tiles)
    ctx.state["built_materials"] = built
    ctx.state["built_objects"] = [{"name": o.name, "slots": [m.name if m else "" for m in o.data.materials]}
                                  for o in sorted(objects, key=lambda o: o.name)]


# Gives the material the neutral look of the surface. A library material that failed keeps its own name, so
# its constant images are named after it and no two materials share an image; a surface material uses the
# surface name.
def neutral(mat, surface, folder, name, failed):
    if failed:
        maps = shading.write_neutral_maps(folder, name, surfaces.LOOKS[surface])
        shading.apply_maps(mat, maps)
        return maps
    return shading.apply_neutral(mat, surface, folder)


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
