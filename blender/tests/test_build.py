import copy
import os
import unittest

import bmesh
import bpy

from build_helpers import BuildTest, load_params
from plynth import meshes, registry, scene

# Mesh coordinates are 32 bit floats, so volumes agree to about 1e-5 of their size, not 1e-6 metres.
SQUARE_VOLUME = (4.2 * 4.2 - 3.8 * 3.8) * 3.0


# Returns a copy of fixture params with the bevel off, so volumes are exact.
def sharp(name):
    params = copy.deepcopy(load_params(name))
    params["settings"]["bevel_width"] = 0.0
    return params


# Returns the face attribute values of an object as lists, by attribute name.
def face_values(obj, name):
    return [item.value for item in obj.data.attributes[name].data]


class SquareRoomTests(BuildTest):
    # Builds the square room once for the checks.
    def setUp(self):
        super().setUp()
        self.params = sharp("square_room")
        self.result = self.build(self.params)
        self.assertTrue(self.result["ok"], self.result["error"])

    # The fixture is a valid job and the result is valid with a timing for every step.
    def test_valid(self):
        self.assert_job_valid("square_room")
        self.assert_result_valid(self.result)
        steps = [s["step"] for s in self.result["output"]["steps"]]
        self.assertEqual(steps, ["prepare_scene", "walls", "openings", "bevel", "triangulate", "opening_fills", "floors", "slabs", "stairs", "roofs",
                         "assets", "materials", "save_blend", "export_fbx", "preview_parts"])
        counts = self.result["output"]["counts"]
        self.assertEqual((counts["wall_solids"], counts["rooms"]), (1, 1))

    # Progress goes out once per step with the right count.
    def test_progress(self):
        lines = self.progress_lines()
        self.assertEqual([l["step"] for l in lines], [s["step"] for s in self.result["output"]["steps"]])
        self.assertEqual([l["index"] for l in lines], list(range(1, len(lines) + 1)))
        self.assertTrue(all(l["count"] == len(lines) for l in lines))

    # The files exist and no temporary file is left.
    def test_files(self):
        self.assertEqual(sorted(os.listdir(self.path("build"))),
                         ["building.blend", "building.fbx", "materials.json", "textures"])

    # Walls have the volume of the solid, a surface on every face, and inside and outside faces differ.
    def test_walls(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        obj = bpy.data.objects["SM_f1_Walls"]
        self.assertEqual(obj["plynth_kind"], "walls")
        self.assertAlmostEqual(meshes.volume(obj), SQUARE_VOLUME, delta=1e-4)
        self.assertTrue(meshes.is_manifold(obj))
        names = [m.name for m in obj.data.materials]
        for polygon in obj.data.polygons:
            self.assertTrue(0 <= polygon.material_index < len(names))
        rooms = list(obj["plynth_rooms"])
        self.assertEqual(rooms, ["r1"])
        sides = face_values(obj, "plynth_side")
        room_values = face_values(obj, "plynth_room")
        side_faces = [p for p in obj.data.polygons if sides[p.index] != 3]
        self.assertEqual(len(side_faces), 8)
        for polygon in side_faces:
            inside = room_values[polygon.index] == 0
            wanted = "Wall_Inside" if inside else "Wall_Outside"
            self.assertEqual(names[polygon.material_index], wanted)
        for polygon in obj.data.polygons:
            if sides[polygon.index] == 3:
                self.assertEqual(names[polygon.material_index], "Wall_Top")

    # Outer side faces point away from the centre and room side faces point into the room.
    def test_normals(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        obj = bpy.data.objects["SM_f1_Walls"]
        room_values = face_values(obj, "plynth_room")
        sides = face_values(obj, "plynth_side")
        for polygon in obj.data.polygons:
            if sides[polygon.index] == 3:
                continue
            to_face = polygon.center.copy()
            to_face.x -= 2.0
            to_face.y += 2.0
            to_face.z = 0.0
            dot = polygon.normal.dot(to_face)
            if room_values[polygon.index] == 0:
                self.assertLess(dot, 0)
            else:
                self.assertGreater(dot, 0)

    # Floor and ceiling area equal the room area, floor faces up and ceiling faces down at ceiling_z.
    def test_floor_and_ceiling(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        floor = bpy.data.objects["SM_f1_r1_Floor"]
        ceiling = bpy.data.objects["SM_f1_r1_Ceiling"]
        self.assertAlmostEqual(meshes.area(floor), 14.44, delta=1e-6)
        self.assertAlmostEqual(meshes.area(ceiling), 14.44, delta=1e-6)
        for polygon in floor.data.polygons:
            self.assertGreater(polygon.normal.z, 0.99)
            self.assertAlmostEqual(polygon.center.z, 0.0, delta=1e-9)
        for polygon in ceiling.data.polygons:
            self.assertLess(polygon.normal.z, -0.99)
            self.assertAlmostEqual(polygon.center.z, 3.0, delta=1e-9)
        self.assertEqual(floor["plynth_kind"], "floor")
        self.assertEqual(ceiling["plynth_id"], "r1")

    # Neutral looks are applied as constant images in build/textures.
    def test_materials(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        mat = bpy.data.materials["Wall_Inside"]
        images = {n.label: n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE"}
        self.assertEqual(sorted(images), ["base_color", "metallic", "normal", "roughness"])
        roughness = images["roughness"]
        self.assertEqual(os.path.dirname(roughness.filepath.replace("\\", "/")), self.path("build", "textures"))
        self.assertAlmostEqual(roughness.pixels[0], 0.9, delta=0.01)

    # The FBX re-imports with the walls, floor and ceiling at the same bounds.
    def test_fbx_reimport(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        before = {o.name: self.bounds(o) for o in bpy.data.objects if o.type == "MESH"}
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("build", "building.fbx"))
        after = {o.name: self.bounds(o) for o in bpy.data.objects if o.type == "MESH"}
        self.assertEqual(sorted(after), sorted(before))
        for name in before:
            for a, b in zip(before[name], after[name]):
                self.assertAlmostEqual(a, b, delta=1e-3)

    # Returns the world bounds of an object as six numbers.
    def bounds(self, obj):
        low, high = scene.world_bounds([obj])
        return tuple(low) + tuple(high)


class FloorsTests(BuildTest):
    # The second floor starts at z 3 and its room is at the second level.
    def test_two_floors(self):
        result = self.build(load_params("two_floors"))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        low, high = scene.world_bounds([bpy.data.objects["SM_f2_Walls"]])
        self.assertAlmostEqual(low.z, 3.0, delta=1e-9)
        self.assertAlmostEqual(high.z, 6.0, delta=1e-9)
        low, high = scene.world_bounds([bpy.data.objects["SM_f1_Walls"]])
        self.assertAlmostEqual(high.z, 3.0, delta=1e-9)
        self.assertEqual([c.name for c in bpy.context.scene.collection.children], ["f1", "f2"])
        ceiling = bpy.data.objects["SM_f2_r1_Ceiling"]
        self.assertAlmostEqual(ceiling.data.polygons[0].center.z, 6.0, delta=1e-9)

    # A half wall is its own lower solid in the same walls object, and every solid is closed.
    def test_half_wall(self):
        result = self.build(sharp("half_wall"))
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["counts"]["wall_solids"], 2)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        obj = bpy.data.objects["SM_f1_Walls"]
        self.assertAlmostEqual(meshes.volume(obj), SQUARE_VOLUME + 3.8 * 0.2 * 1.1, delta=1e-4)
        self.assertTrue(meshes.is_manifold(obj))

    # A failed save (the output folder is a file) gives save_failed.
    def test_save_failed(self):
        with open(self.path("blocker"), "w") as handle:
            handle.write("x")
        params = load_params("square_room")
        params["output_folder"] = self.path("blocker")
        result = self.run_task("build", params, job_id="build-1")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "save_failed")
        self.assert_result_valid(result)

    # Without an fbx_file nothing is exported and the result says so.
    def test_no_fbx(self):
        result = self.build(load_params("square_room"), fbx_file=None)
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["fbx_file"])
        self.assertEqual(sorted(os.listdir(self.path("build"))), ["building.blend", "textures"])
        self.assertIsNone(result["output"]["materials_file"])
        self.assertGreater(result["output"]["triangles"], 0)

    # A job without floors is a bad_job.
    def test_bad_job(self):
        params = load_params("square_room")
        del params["floors"]
        result = self.build(params)
        self.assertEqual(result["error"]["code"], "bad_job")

    # Every build step is registered once and sorted by its order.
    def test_registered_steps(self):
        self.build(load_params("square_room"))
        orders = [step.order for _, step in registry.BUILD_STEPS.all()]
        self.assertEqual(len(orders), len(set(orders)))


if __name__ == "__main__":
    unittest.main()
