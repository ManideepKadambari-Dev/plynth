import math

import unreal

from .. import ue
from ..registry import check

# The most triangles read in all, so a big building keeps the check quick.
TRIANGLE_BUDGET = 300000
# A triangle smaller than this, in square centimetres, counts as degenerate.
DEGENERATE_AREA = 1.0e-6


# Checks no mesh lacks triangles or normals, and counts degenerate triangles in the imported mesh data.
@check("normals", 20)
def run(ctx):
    failures = []
    warnings = []
    budget = [TRIANGLE_BUDGET]
    for path in ctx.all_mesh_paths():
        name = path.rsplit("/", 1)[-1]
        mesh = ue.load(path)
        if mesh is None:
            failures.append("%s is missing" % name)
            continue
        if mesh.get_num_triangles(0) <= 0:
            failures.append("%s has no triangles" % name)
            continue
        if recomputes_normals(mesh):
            warnings.append("%s had its normals made by Unreal instead of read from the file" % name)
        found = read_mesh_data(mesh, budget)
        if found is None:
            warnings.append("the mesh data of %s could not be read in this engine" % name)
            continue
        degenerate, without_normal = found
        if without_normal:
            failures.append("%s has %d corners without a normal" % (name, without_normal))
        if degenerate:
            warnings.append("%s has %d degenerate triangles, which Unreal drops" % (name, degenerate))
    if failures:
        return "fail", "%s." % "; ".join((failures + warnings)[:10])
    if warnings:
        return "warning", "%s." % "; ".join(warnings[:10])
    return "pass", "Every mesh has triangles and normals, with no degenerate triangles."


# Returns True when the mesh's first level of detail builds its own normals.
def recomputes_normals(mesh):
    try:
        settings = ue.mesh_tools().get_lod_build_settings(mesh, 0)
        return bool(settings.get_editor_property("recompute_normals"))
    except Exception:
        return False


# Makes a mesh description element id from its number.
def element_id(id_class, number):
    try:
        return id_class(number)
    except Exception:
        made = id_class()
        made.set_editor_property("id_value", number)
        return made


# Returns (degenerate triangles, corners without a normal) of a mesh's source data, or None when this engine
# cannot read it. Reads at most the budget's triangles and lowers the budget by what it read.
def read_mesh_data(mesh, budget):
    try:
        description = mesh.get_static_mesh_description(0)
        count = min(int(description.get_triangle_count()), budget[0])
        has_normals = hasattr(description, "get_vertex_instance_normal")
        degenerate = 0
        without_normal = 0
        for number in range(count):
            triangle = element_id(unreal.TriangleID, number)
            if hasattr(description, "is_triangle_valid") and not description.is_triangle_valid(triangle):
                continue
            corners = list(description.get_triangle_vertex_instances(triangle))
            points = [description.get_vertex_position(description.get_vertex_instance_vertex(corner))
                      for corner in corners]
            if len(points) != 3 or area(points) < DEGENERATE_AREA:
                degenerate += 1
            if has_normals:
                for corner in corners:
                    normal = description.get_vertex_instance_normal(corner)
                    if math.sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z) < 0.5:
                        without_normal += 1
        budget[0] -= count
        return degenerate, without_normal
    except Exception as error:
        ue.warn("could not read mesh data: %s" % error)
        return None


# Returns the area of a triangle given as three vectors.
def area(points):
    a, b, c = points
    ux, uy, uz = b.x - a.x, b.y - a.y, b.z - a.z
    vx, vy, vz = c.x - a.x, c.y - a.y, c.z - a.z
    cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
