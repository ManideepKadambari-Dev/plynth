import math

from opening_helpers import NO_SLAB, rectangle, ring, job, straight_opening

KERB = 0.08
HALF_WALL = 1.1
FULL = 3.0
SLANT = 0.05 / math.sqrt(2.0)


# Returns the tag of one ring edge: the wall it belongs to, its side and the room it looks into.
def tag(wall, side=None, room=None):
    return {"wall": wall, "side": side, "room": room, "material": None}


# Returns a ring whose edge tags come from the tagger, called with the start and end of each edge.
def tagged_ring(points, tagger):
    return ring(points, [tagger(p, points[(i + 1) % len(points)]) for i, p in enumerate(points)])


# Returns a wall solid of one height from its outline and holes.
def solid(height, outline, holes=()):
    return {"height": height, "top_material": None, "outline": outline, "holes": list(holes)}


# Returns one living room with the given anticlockwise outline.
def room(points, area, height=FULL):
    polygon = [{"outline": [list(p) for p in points], "holes": []}]
    return {"id": "r1", "type": "living", "area": area, "floor_z": 0.0, "ceiling_z": height,
            "floor_material": None, "ceiling_material": None, "polygons": polygon, "ceiling_polygons": polygon}


# Returns a floor from its wall solids, openings and room.
def floor(solids, openings, rooms):
    return {"id": "f1", "elevation": 0.0, "ceiling_height": FULL, "wall_solids": solids, "openings": openings,
            "rooms": rooms, "items": [], "slab": NO_SLAB, "stairs": [], "roofs": []}


# Returns an opening on the wall from a to b, its centre offset along the wall, as core works it out.
def opening(oid, wall, a, b, offset, kind="window", width=0.6, height=1.2, sill=0.9, wall_height=FULL):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    tangent = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
    centre = (a[0] + tangent[0] * offset, a[1] + tangent[1] * offset)
    right = (tangent[1], -tangent[0])
    return straight_opening(oid, wall, kind, "rectangle", rectangle(width, height), width, height, sill, centre,
                            tangent, right, wall_height=wall_height)


# Returns openings on walls given as (wall, start, end, offsets), numbered from first.
def windows(spans, first=1):
    result = []
    for wall, a, b, offsets in spans:
        for offset in offsets:
            result.append(opening("o%d" % (first + len(result)), wall, a, b, offset))
    return result


# Returns the name of the wall an edge belongs to, from a function given its middle point and whether it runs
# along X.
def wall_of(wall_at, p, q):
    middle = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
    return wall_at(middle, abs(q[0] - p[0]) > abs(q[1] - p[1]))


# Returns the tagger of the outer ring and the tagger of the inner ring of a room of tall walls.
def room_taggers(wall_at):
    # Tags an outside face.
    def outer(p, q):
        return tag(wall_of(wall_at, p, q), "right")

    # Tags a face that looks into the room.
    def inner(p, q):
        return tag(wall_of(wall_at, p, q), "left", "r1")

    return outer, inner


# The owner's case: an 8 by 6 m room of 3 m walls cut across the middle by a 0.08 m wall from p5 (4, 0) to
# p6 (4, 6), 0.1 m thick, with 14 windows on the 3 m walls. The tall inner ring has a corner wherever a corner of
# the low wall touches it, as core makes it.
def kerb_room():
    west, east = 3.95, 4.05

    # Names the wall at a point of an edge.
    def wall_at(m, along_x):
        if along_x and m[1] < 3.0:
            return "w1" if m[0] < 4.0 else "w2"
        if along_x:
            return "w4" if m[0] > 4.0 else "w5"
        return "w3" if m[0] > 4.0 else "w6"

    outer, inner = room_taggers(wall_at)
    tall = solid(FULL, tagged_ring([(-0.1, -0.1), (4.0, -0.1), (8.1, -0.1), (8.1, 6.1), (4.0, 6.1), (-0.1, 6.1)],
                                   outer),
                 [tagged_ring([(0.1, 0.1), (0.1, 5.9), (west, 5.9), (east, 5.9), (7.9, 5.9), (7.9, 0.1),
                               (east, 0.1), (west, 0.1)], inner)])
    low = solid(KERB, ring([(west, 0.1), (east, 0.1), (east, 5.9), (west, 5.9)],
                           [tag("w7"), tag("w7", "right", "r1"), tag("w7"), tag("w7", "left", "r1")]))
    p1, p5, p2, p3, p6, p4 = (0.0, 0.0), (4.0, 0.0), (8.0, 0.0), (8.0, 6.0), (4.0, 6.0), (0.0, 6.0)
    openings = windows([("w1", p1, p5, (1.0, 2.8)), ("w2", p5, p2, (1.0, 2.8)), ("w3", p2, p3, (1.2, 3.0, 4.8)),
                        ("w4", p3, p6, (1.0, 2.8)), ("w5", p6, p4, (1.0, 2.8)), ("w6", p4, p1, (1.2, 3.0, 4.8))])
    inside = [(0.1, 0.1), (7.9, 0.1), (7.9, 5.9), (0.1, 5.9)]
    return job([floor([tall, low], openings, [room(inside, 7.8 * 5.8)])])


# A 0.08 m wall in the middle of the front wall of an 8 by 6 m room, between 3 m pieces of the same thickness,
# so the low solid's ends meet the ends of the tall one. Windows and two doors on the 3 m walls.
def inline_room():
    points = [(-0.1, -0.1), (3.0, -0.1), (3.0, 0.1), (0.1, 0.1), (0.1, 5.9), (7.9, 5.9), (7.9, 0.1), (5.0, 0.1),
              (5.0, -0.1), (8.1, -0.1), (8.1, 6.1), (-0.1, 6.1)]
    edges = [tag("w1", "right"), tag("w1"), tag("w1", "left", "r1"), tag("w5", "left", "r1"),
             tag("w4", "left", "r1"), tag("w3", "left", "r1"), tag("w2", "left", "r1"), tag("w2"),
             tag("w2", "right"), tag("w3", "right"), tag("w4", "right"), tag("w5", "right")]
    tall = solid(FULL, ring(points, edges))
    low = solid(KERB, ring([(3.0, -0.1), (5.0, -0.1), (5.0, 0.1), (3.0, 0.1)],
                           [tag("w7", "right"), tag("w7"), tag("w7", "left", "r1"), tag("w7")]))
    p1, pa, pb, p2, p3, p4 = (0.0, 0.0), (3.0, 0.0), (5.0, 0.0), (8.0, 0.0), (8.0, 6.0), (0.0, 6.0)
    openings = windows([("w1", p1, pa, (1.2,)), ("w2", pb, p2, (2.3,)), ("w3", p2, p3, (1.5, 4.5)),
                        ("w4", p3, p4, (2.0, 6.0)), ("w5", p4, p1, (3.0,))])
    openings.append(opening("o%d" % (len(openings) + 1), "w1", p1, pa, 2.3, "door", 0.9, 2.1, 0.0))
    openings.append(opening("o%d" % (len(openings) + 1), "w2", pb, p2, 0.7, "door", 0.9, 2.1, 0.0))
    inside = [(0.1, 0.1), (7.9, 0.1), (7.9, 5.9), (0.1, 5.9)]
    return job([floor([tall, low], openings, [room(inside, 7.8 * 5.8)])])


# A 0.08 m wall, 0.1 m thick, running from the inside corner of a 6 by 5 m room at 45 degrees. A door next to
# the corner also cuts the end of the low wall, so one cutter meets both solids.
def corner_room():
    s = SLANT
    b, e = (0.1 + 2 * s, 0.1), (0.1, 0.1 + 2 * s)

    # Names the wall at a point of an edge.
    def wall_at(m, along_x):
        if along_x:
            return "w1" if m[1] < 2.5 else "w3"
        return "w2" if m[0] > 3.0 else "w4"

    outer, inner = room_taggers(wall_at)
    tall = solid(FULL, tagged_ring([(-0.1, -0.1), (6.1, -0.1), (6.1, 5.1), (-0.1, 5.1)], outer),
                 [tagged_ring([(0.1, 0.1), e, (0.1, 4.9), (5.9, 4.9), (5.9, 0.1), b], inner)])
    low = solid(KERB, ring([(0.1, 0.1), b, (3.0 + s, 3.0 - s), (3.0 - s, 3.0 + s), e],
                           [tag("w5"), tag("w5", "right", "r1"), tag("w5"), tag("w5", "left", "r1"), tag("w5")]))
    p1, p2, p3, p4 = (0.0, 0.0), (6.0, 0.0), (6.0, 5.0), (0.0, 5.0)
    openings = [opening("o1", "w1", p1, p2, 0.6, "door", 0.9, 2.1, 0.0)]
    openings += windows([("w2", p2, p3, (2.5,)), ("w3", p3, p4, (3.0,)), ("w4", p4, p1, (4.4,))], first=2)
    inside = [(0.1, 0.1), (5.9, 0.1), (5.9, 4.9), (0.1, 4.9)]
    return job([floor([tall, low], openings, [room(inside, 5.8 * 4.8)])])


# Two low walls meeting: a 1.1 m wall from the front wall of a 6 by 5 m room to the middle of the room, and a
# 0.08 m wall from its end to the east wall. Three heights touch, and the 1.1 m wall holds a window.
def two_low_room():
    # Names the wall at a point of an edge.
    def wall_at(m, along_x):
        if along_x and m[1] < 2.5:
            return "w1" if m[0] < 3.0 else "w2"
        if along_x:
            return "w4"
        return "w3" if m[0] > 3.0 else "w5"

    outer, inner = room_taggers(wall_at)
    tall = solid(FULL, tagged_ring([(-0.1, -0.1), (3.0, -0.1), (6.1, -0.1), (6.1, 5.1), (-0.1, 5.1)], outer),
                 [tagged_ring([(0.1, 0.1), (0.1, 4.9), (5.9, 4.9), (5.9, 2.6), (5.9, 2.4), (5.9, 0.1), (3.1, 0.1),
                               (2.9, 0.1)], inner)])
    half = solid(HALF_WALL, ring([(2.9, 0.1), (3.1, 0.1), (3.1, 2.4), (3.1, 2.6), (2.9, 2.6)],
                                 [tag("w6"), tag("w6", "right", "r1"), tag("w6", "right", "r1"), tag("w6"),
                                  tag("w6", "left", "r1")]))
    low = solid(KERB, ring([(3.1, 2.4), (5.9, 2.4), (5.9, 2.6), (3.1, 2.6)],
                           [tag("w7", "right", "r1"), tag("w7"), tag("w7", "left", "r1"), tag("w7")]))
    p1, pa, p2, p3, p4 = (0.0, 0.0), (3.0, 0.0), (6.0, 0.0), (6.0, 5.0), (0.0, 5.0)
    openings = windows([("w1", p1, pa, (1.5,)), ("w3", p2, p3, (1.2, 3.8)), ("w4", p3, p4, (3.0,)),
                        ("w5", p4, p1, (2.5,))])
    openings.append(opening("o%d" % (len(openings) + 1), "w2", pa, p2, 0.75, "door", 0.9, 2.1, 0.0))
    openings.append(opening("o%d" % (len(openings) + 1), "w6", pa, (3.0, 2.5), 1.2, "window", 0.6, 0.5, 0.3,
                            HALF_WALL))
    inside = [(0.1, 0.1), (5.9, 0.1), (5.9, 4.9), (0.1, 4.9)]
    return job([floor([tall, half, low], openings, [room(inside, 5.8 * 4.8)])])


# Returns every low wall job by name.
def low_wall_jobs():
    return {"kerb_room": kerb_room(), "inline_room": inline_room(), "corner_room": corner_room(),
            "two_low_room": two_low_room()}
