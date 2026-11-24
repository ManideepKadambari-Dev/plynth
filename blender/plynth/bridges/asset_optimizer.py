import bpy

from . import enable_addon, register, select_only
from ..generating import AddonUnavailable

MODULES = ("asset_optimizer",)


# Says whether Game Asset Optimizer is installed and can be switched on.
def available():
    return enable_addon(MODULES)


# Raises AddonUnavailable when the add-on cannot be used.
def require():
    if not available():
        raise AddonUnavailable("The add-on Asset Optimizer is not installed or cannot be enabled.")


# Returns the mesh objects of a list.
def meshes_of(objects):
    return [obj for obj in objects if obj.type == "MESH"]


# Merges vertices closer than distance (metres) on each mesh object, in place. Returns the objects.
def merge_vertices(objects, distance=0.0001):
    require()
    targets = meshes_of(objects)
    if targets:
        select_only(targets)
        result = bpy.ops.mesh.smart_vertex_merge(merge_distance=distance)
        if "FINISHED" not in result:
            raise RuntimeError("Asset Optimizer did not merge the vertices.")
    return list(objects)


# Adds a second UV map named UVMap_Lightmap with non overlapping islands to each mesh object, in place. The
# first UV map is left as it is. Returns the objects.
def lightmap_uvs(objects):
    require()
    targets = meshes_of(objects)
    if targets:
        select_only(targets)
        result = bpy.ops.mesh.dual_uv_unwrap(uv0_enabled=False, uv1_enabled=True)
        if "FINISHED" not in result:
            raise RuntimeError("Asset Optimizer did not unwrap the lightmap UVs.")
    return list(objects)


class Api:
    available = staticmethod(available)
    merge_vertices = staticmethod(merge_vertices)
    lightmap_uvs = staticmethod(lightmap_uvs)


register("asset_optimizer", MODULES, Api)
