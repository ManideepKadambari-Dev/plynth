import json
import unittest

import bpy
from mathutils import Vector

from build_helpers import BuildTest, load_params

CHECKED_KINDS = ("slab", "roof", "ceiling", "floor")
ON_LINE = 1e-4


# Returns the outside faces of a floor's walls as (start, end, slot) in plan: the wall edges no room looks at.
def outer_lines(floor):
    lines = []
    for solid in floor["wall_solids"]:
        for ring in [solid["outline"]] + list(solid["holes"]):
            points = ring["points"]
            for i, edge in enumerate(ring["edges"]):
                if edge.get("room") is None and edge.get("side") is not None:
                    a, b = Vector(points[i][:2]), Vector(points[(i + 1) % len(points)][:2])
                    if (b - a).length > ON_LINE:
                        lines.append((a, b, edge["slot"]))
    return lines


# Returns the slot of the outside wall face a plan point lies on, or None.
def slot_at(lines, point):
    for a, b, slot in lines:
        along = b - a
        t = (point - a).dot(along) / along.length
        if -ON_LINE <= t <= along.length + ON_LINE and abs(along.cross(point - a)) / along.length < ON_LINE:
            return slot
    return None


# Returns (object, face index, material, wanted slot) for every vertical slab, roof, floor or ceiling face that lies
# on the outside face of its floor's walls with a material other than that wall's.
def wrong_faces(floors):
    found = []
    lines = {floor["id"]: outer_lines(floor) for floor in floors}
    rooms = {room["id"]: floor["id"] for floor in floors for room in floor["rooms"]}
    for obj in bpy.data.objects:
        if obj.type != "MESH" or obj.get("plynth_kind") not in CHECKED_KINDS:
            continue
        plan_id = obj.get("plynth_id")
        floor_lines = lines.get(plan_id) or lines.get(rooms.get(plan_id), [])
        names = [m.name for m in obj.data.materials]
        for polygon in obj.data.polygons:
            normal = obj.matrix_world.to_3x3() @ polygon.normal
            if polygon.area < 1e-8 or abs(normal.z) > ON_LINE:
                continue
            corners = [(obj.matrix_world @ obj.data.vertices[i].co).to_2d() for i in polygon.vertices]
            middle = sum(corners, Vector((0.0, 0.0))) / len(corners)
            slots = {slot_at(floor_lines, p) for p in corners + [middle]}
            if len(slots) != 1 or None in slots:
                continue
            wanted = slots.pop()
            if names[polygon.material_index] != wanted:
                found.append((obj.name, polygon.index, names[polygon.material_index], wanted))
    return found


class OuterFaceTests(BuildTest):
    # Builds a fixture and opens building.blend, returning the job's floors.
    def build_open(self, name):
        params = load_params(name)
        if name == "from_core_house":
            text = json.dumps(params).replace("@LIBRARY@", self.path("library").replace("\\", "/"))
            params = json.loads(text.replace("@PROJECT@", self.path("project").replace("\\", "/")))
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return params["floors"]

    # The outside of the building reads as one wall: no slab, roof, floor or ceiling face on the outside wall face
    # has a material other than that wall's, so a flat roof's edge between the wall and the parapet is brick too.
    def test_no_other_material_on_the_outer_wall_face(self):
        for name in ("roof_flat", "from_core", "from_core_house"):
            with self.subTest(fixture=name):
                self.setUp()
                floors = self.build_open(name)
                self.assertEqual(wrong_faces(floors), [])

    # The check sees a flush roof edge: the flat roof fixture has edge faces on the wall face line.
    def test_flat_roof_edges_are_on_the_wall_face(self):
        floors = self.build_open("roof_flat")
        lines = outer_lines(floors[0])
        edges = [f for f in floors[0]["roofs"][0]["faces"] if f["part"] == "edge"]
        self.assertTrue(edges)
        for face in edges:
            for point in face["outline"]:
                self.assertIsNotNone(slot_at(lines, Vector(point[:2])))

    # A gable roof's edges stand clear of the walls on the overhang, so they keep the fascia.
    def test_eave_edges_keep_the_fascia(self):
        self.build_open("roof_gable")
        names = {m.name for m in bpy.data.objects["f1_Roof_00"].data.materials}
        self.assertIn("Fascia", names)


class BevelEdgeTests(BuildTest):
    # With the bevel on, the corners are rounded but the edges along the top and bottom of the walls stay sharp,
    # so no groove runs along the outside where a slab or the ground meets the walls.
    def test_top_and_bottom_edges_stay_sharp(self):
        params = load_params("square_room")
        params["settings"]["bevel_width"] = 0.01
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["problems"], [])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        walls = bpy.data.objects["f1_Walls"]
        heights = sorted({round(v.co.z, 4) for v in walls.data.vertices})
        self.assertEqual(heights, [0.0, 3.0])
        self.assertGreater(len(walls.data.vertices), 16)
        self.assertNotIn("bevel_weight_edge", walls.data.attributes)


if __name__ == "__main__":
    unittest.main()
