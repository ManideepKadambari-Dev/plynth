import re

import numpy as np

NAME_PATTERN = re.compile(r"[A-Za-z0-9_]+")
CONSTANT_SIZE = 4


# Returns the plan id stored on an object, or None.
def plan_id(obj):
    return obj.get("plynth_id") or None


# Returns every exported object: the building meshes then the collision meshes.
def exported_objects(ctx):
    return list(ctx.state["meshes"]) + list(ctx.state["collision"])


# Returns the Principled shader of a material, or None.
def principled(material):
    tree = material.node_tree
    if tree is None:
        return None
    for node in tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            return node
    return None


# Returns every node that feeds an input socket, following links backwards through any node.
def upstream_nodes(socket):
    found = []
    pending = [link.from_node for link in socket.links]
    while pending:
        node = pending.pop()
        if node in found:
            continue
        found.append(node)
        for entry in node.inputs:
            pending.extend(link.from_node for link in entry.links)
    return found


# Says what feeds the base color of a material. Returns (kind, detail): "image" with the image, "empty" for an
# image node without an image, "procedural" with the node's label, "flat" for a plain color value and "none"
# when there is no Principled shader.
def base_color_source(material):
    shader = principled(material)
    if shader is None:
        return "none", None
    nodes = upstream_nodes(shader.inputs["Base Color"])
    images = [n for n in nodes if n.type == "TEX_IMAGE"]
    for node in images:
        if node.image is not None:
            return "image", node.image
    if images:
        return "empty", None
    for node in nodes:
        if node.type.startswith("TEX_") or node.type == "GROUP":
            return "procedural", node.bl_label
    return "flat", None


# Returns the image nodes of a material that hold an image.
def image_nodes(material):
    tree = material.node_tree
    if tree is None:
        return []
    return [n for n in tree.nodes if n.type == "TEX_IMAGE" and n.image is not None]


# Returns the polygon areas of a mesh as (uv areas, areas in square metres) arrays, or None without a UV map.
def polygon_areas(mesh):
    layer = mesh.uv_layers.active or (mesh.uv_layers[0] if len(mesh.uv_layers) else None)
    if layer is None or len(mesh.polygons) == 0:
        return None
    mesh.calc_loop_triangles()
    triangles = len(mesh.loop_triangles)
    if triangles == 0:
        return None
    tri_loops = np.empty(triangles * 3, dtype=np.int64)
    tri_polygon = np.empty(triangles, dtype=np.int64)
    mesh.loop_triangles.foreach_get("loops", tri_loops)
    mesh.loop_triangles.foreach_get("polygon_index", tri_polygon)
    uv = np.empty(len(mesh.loops) * 2, dtype=np.float64)
    layer.uv.foreach_get("vector", uv)
    uv = uv.reshape(-1, 2)
    corners = uv[tri_loops.reshape(-1, 3)]
    first = corners[:, 1] - corners[:, 0]
    second = corners[:, 2] - corners[:, 0]
    tri_area = np.abs(first[:, 0] * second[:, 1] - first[:, 1] * second[:, 0]) * 0.5
    uv_area = np.bincount(tri_polygon, weights=tri_area, minlength=len(mesh.polygons))
    area = np.empty(len(mesh.polygons), dtype=np.float64)
    mesh.polygons.foreach_get("area", area)
    return uv_area, area


# Returns the material index of every polygon of a mesh as an array.
def polygon_materials(mesh):
    indices = np.zeros(len(mesh.polygons), dtype=np.int64)
    if len(mesh.polygons):
        mesh.polygons.foreach_get("material_index", indices)
    return indices
