"""Turns a sweep (rings of world points of the same count, worked out by core) into mesh data: vertices, faces and
UVs in metres. Plain Python only, so it runs and is tested without Blender. The trim build step hands the result
to bmesh.
"""
import math


# The difference of two points.
def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


# The cross product of two vectors.
def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


# The dot product of two vectors.
def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


# The length of a vector.
def _length(a):
    return math.sqrt(_dot(a, a))


# The middle of a ring of points.
def _centroid(ring):
    n = float(len(ring))
    return (sum(p[0] for p in ring) / n, sum(p[1] for p in ring) / n, sum(p[2] for p in ring) / n)


# Checks the sections and returns them as tuples. Raises ValueError with a plain sentence when they are unusable.
def check_sections(sections):
    if len(sections) < 2:
        raise ValueError("A sweep needs at least two sections.")
    count = len(sections[0])
    if count < 3:
        raise ValueError("Every section of a sweep needs at least three points.")
    out = []
    for section in sections:
        if len(section) != count:
            raise ValueError("Every section of a sweep needs the same number of points.")
        ring = []
        for p in section:
            if len(p) != 3 or not all(isinstance(c, (int, float)) and math.isfinite(c) for c in p):
                raise ValueError("A sweep point must be three finite numbers.")
            ring.append((float(p[0]), float(p[1]), float(p[2])))
        out.append(ring)
    return out


# The signed volume a closed mesh holds, positive when its faces point outward.
def signed_volume(vertices, faces):
    total = 0.0
    for face in faces:
        a = vertices[face[0]]
        for i in range(1, len(face) - 1):
            b = vertices[face[i]]
            c = vertices[face[i + 1]]
            total += _dot(a, _cross(b, c))
    return total / 6.0


# The UVs of a cap: the ring's points measured in metres in the ring's own plane.
def _cap_uvs(ring):
    c = _centroid(ring)
    normal = (0.0, 0.0, 0.0)
    for i, p in enumerate(ring):
        q = ring[(i + 1) % len(ring)]
        normal = (normal[0] + (p[1] - q[1]) * (p[2] + q[2]), normal[1] + (p[2] - q[2]) * (p[0] + q[0]),
                  normal[2] + (p[0] - q[0]) * (p[1] + q[1]))
    size = _length(normal) or 1.0
    normal = (normal[0] / size, normal[1] / size, normal[2] / size)
    first = _sub(ring[1], ring[0])
    if _length(first) < 1e-12:
        first = (1.0, 0.0, 0.0)
    a = _cross(normal, _cross(first, normal))
    a_size = _length(a) or 1.0
    a = (a[0] / a_size, a[1] / a_size, a[2] / a_size)
    b = _cross(normal, a)
    return [(_dot(_sub(p, c), a), _dot(_sub(p, c), b)) for p in ring]


# Makes the mesh of a sweep. Returns a dict with "vertices" (point tuples), "faces" (index lists, every face
# pointing outward) and "uvs" (one list of (u, v) per face, one pair per corner). u runs along the path and v
# round the profile, both in metres. An open sweep gets a cap at each end.
def sweep_mesh(sections, closed):
    rings = check_sections(sections)
    count = len(rings[0])
    total = len(rings)
    vertices = [p for ring in rings for p in ring]

    # u of each section: the distance along the path of the section middles.
    middles = [_centroid(ring) for ring in rings]
    along = [0.0]
    for k in range(1, total):
        along.append(along[-1] + _length(_sub(middles[k], middles[k - 1])))
    path_end = along[-1] + (_length(_sub(middles[0], middles[-1])) if closed else 0.0)

    # v of each profile point: the distance round the first section.
    first = rings[0]
    round_profile = [0.0]
    for j in range(1, count):
        round_profile.append(round_profile[-1] + _length(_sub(first[j], first[j - 1])))
    profile_end = round_profile[-1] + _length(_sub(first[0], first[-1]))

    faces = []
    uvs = []
    segments = total if closed else total - 1
    for k in range(segments):
        k1 = (k + 1) % total
        u0 = along[k]
        u1 = along[k1] if k1 != 0 else path_end
        for j in range(count):
            j1 = (j + 1) % count
            v0 = round_profile[j]
            v1 = round_profile[j1] if j1 != 0 else profile_end
            faces.append([k * count + j, k * count + j1, k1 * count + j1, k1 * count + j])
            uvs.append([(u0, v0), (u0, v1), (u1, v1), (u1, v0)])
    if not closed:
        start = list(range(count - 1, -1, -1))
        end = [(total - 1) * count + j for j in range(count)]
        faces.append(start)
        uvs.append(_cap_uvs([vertices[i] for i in start]))
        faces.append(end)
        uvs.append(_cap_uvs([vertices[i] for i in end]))

    if signed_volume(vertices, faces) < 0.0:
        faces = [list(reversed(f)) for f in faces]
        uvs = [list(reversed(uv)) for uv in uvs]
    return {"vertices": vertices, "faces": faces, "uvs": uvs}
