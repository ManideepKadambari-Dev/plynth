import copy
import json
import math
import os
import unittest

import bmesh
import bpy
import numpy as np
from mathutils import Matrix

from build_helpers import DATA_FOLDER, BuildTest, load_params
from plynth import scene
from schema_check import load_schema, validate
from slot_fixtures import upgrade

CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")

# The four wall pieces of the square room (outer 4.2 m, walls 0.2 m, 3 m high) and the floor slab under it.
WALL_PIECES = [
    [(-0.1, -4.1), (0.1, -4.1), (0.1, 0.1), (-0.1, 0.1)],
    [(0.1, -4.1), (4.1, -4.1), (4.1, -3.9), (0.1, -3.9)],
    [(3.9, -3.9), (4.1, -3.9), (4.1, 0.1), (3.9, 0.1)],
    [(0.1, -0.1), (3.9, -0.1), (3.9, 0.1), (0.1, 0.1)],
]
ROOM_RING = [(0.1, -3.9), (3.9, -3.9), (3.9, -0.1), (0.1, -0.1)]


# Returns the square room job with the collision pieces of the plan filled in.
def collision_params():
    params = copy.deepcopy(load_params("square_room"))
    params["settings"]["bevel_width"] = 0.0
    walls = {"target": {"kind": "walls", "id": "f1"},
             "pieces": [{"outline": [list(p) for p in ring], "bottom": 0.0, "top": 3.0} for ring in WALL_PIECES],
             "index": 0, "hulls": []}
    floor = {"target": {"kind": "floor", "id": "r1"},
             "pieces": [{"outline": [list(p) for p in ROOM_RING], "bottom": -0.1, "top": 0.0}],
             "index": 0, "hulls": []}
    params["floors"][0]["collision"] = [walls, floor]
    return upgrade(params)


# Writes the collision fixture to tests/data/buildjob.
def write_fixture():
    data = {"format": "plynth-blender-job", "version": 1, "task": "build", "job_id": "fixture-1",
            "result_file": "result.json", "params": collision_params()}
    with open(os.path.join(DATA_FOLDER, "collision.json"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(data, indent=1) + "\n")


# Returns the volume of the convex hull of a mesh object's vertices.
def hull_volume(obj):
    bm = bmesh.new()
    for vertex in obj.data.vertices:
        bm.verts.new(vertex.co)
    result = bmesh.ops.convex_hull(bm, input=bm.verts[:])
    drop = list({g for g in result["geom_interior"] + result["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
    bmesh.ops.delete(bm, geom=drop, context="VERTS")
    volume = bm.calc_volume(signed=True)
    bm.free()
    return volume


# Returns the volume of a mesh object's own faces.
def mesh_volume(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    volume = bm.calc_volume(signed=True)
    bm.free()
    return volume


# Returns true when every edge of the mesh object has exactly two faces.
def is_closed(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    closed = all(len(edge.link_faces) == 2 for edge in bm.edges)
    bm.free()
    return closed


# Writes a PNG of one flat colour with the given size and returns its path.
def write_image(path, size, rgb):
    image = bpy.data.images.new("tmp_%d" % size, size, size, alpha=False)
    image.colorspace_settings.name = "Non-Color"
    pixels = np.empty((size, size, 4), dtype=np.float32)
    pixels[:, :] = (rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 1.0)
    image.pixels.foreach_set(pixels.ravel())
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)
    return path


class CollisionTests(BuildTest):
    # Builds the collision fixture and opens building.blend.
    def build_open(self, params):
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return result

    # The fixture file is the one the helper makes, and it matches the job schema.
    def test_fixture(self):
        path = os.path.join(DATA_FOLDER, "collision.json")
        with open(path, "r", encoding="utf-8") as handle:
            job = json.load(handle)
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        self.assertEqual(job["params"], json.loads(json.dumps(collision_params())))

    # Every piece becomes a closed convex mesh named after its target, inside the job's prism, tagged and kept
    # in the Collision collection, and the count says how many.
    def test_pieces(self):
        params = collision_params()
        result = self.build_open(params)
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["collision"], 5)
        names = sorted(o.name for o in bpy.data.objects if o.get("plynth_kind") == "collision")
        self.assertEqual(names, ["f1_Walls_Collision_00", "f1_Walls_Collision_01", "f1_Walls_Collision_02",
                                 "f1_Walls_Collision_03", "f1_r1_Floor_Collision_00"])
        pieces = params["floors"][0]["collision"][0]["pieces"] + params["floors"][0]["collision"][1]["pieces"]
        collection = bpy.data.collections["Collision"]
        for number, name in enumerate(names):
            obj = bpy.data.objects[name]
            self.assertIn(obj, list(collection.objects))
            self.assertEqual(obj.type, "MESH")
            self.assertEqual(obj.display_type, "WIRE")
            self.assertTrue(obj.hide_render, "a render of building.blend must not show %s" % name)
            self.assertTrue(is_closed(obj))
            volume = mesh_volume(obj)
            self.assertGreater(volume, 0.0)
            self.assertAlmostEqual(volume, hull_volume(obj), delta=1e-6)
            piece = pieces[number]
            xs = [p[0] for p in piece["outline"]]
            ys = [p[1] for p in piece["outline"]]
            low, high = scene.world_bounds([obj])
            self.assertGreaterEqual(low.x, min(xs) - 1e-5)
            self.assertLessEqual(high.x, max(xs) + 1e-5)
            self.assertGreaterEqual(low.y, min(ys) - 1e-5)
            self.assertLessEqual(high.y, max(ys) + 1e-5)
            self.assertGreaterEqual(low.z, piece["bottom"] - 1e-5)
            self.assertLessEqual(high.z, piece["top"] + 1e-5)
        walls = bpy.data.objects["f1_Walls_Collision_01"]
        self.assertEqual(walls["plynth_collision_for"], "f1_Walls")
        self.assertEqual(bpy.data.objects["f1_r1_Floor_Collision_00"]["plynth_collision_for"], "f1_r1_Floor")

    # The collision meshes leave the FBX, the materials file and the object counts alone.
    def test_left_out_of_the_fbx(self):
        result = self.build_open(collision_params())
        data = json.load(open(self.path("build", "materials.json"), "r", encoding="utf-8"))
        self.assertEqual(validate(data, load_schema("build-materials")), [])
        self.assertEqual([o["name"] for o in data["objects"] if "_Collision_" in o["name"]], [])
        plain = copy.deepcopy(load_params("square_room"))
        plain["settings"]["bevel_width"] = 0.0
        without = self.build(plain, job_id="again")
        self.assertEqual(result["output"]["objects"], without["output"]["objects"])
        self.assertEqual(result["output"]["triangles"], without["output"]["triangles"])
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("build", "building.fbx"))
        self.assertEqual([o.name for o in bpy.data.objects if "_Collision_" in o.name], [])
        self.assertIn("f1_Walls", [o.name for o in bpy.data.objects])

    # A piece with no height is reported and left out, and the others are still made.
    def test_failed_piece(self):
        params = collision_params()
        params["floors"][0]["collision"][0]["pieces"][1]["top"] = 0.0
        result = self.build_open(params)
        self.assertEqual([(p["code"], p["severity"], p["object"], p["floor"], p["step"])
                          for p in result["output"]["problems"]], [("collision_failed", "warning", "f1", "f1",
                                                                    "collision")])
        self.assertEqual(result["output"]["counts"]["collision"], 4)
        self.assertIsNone(bpy.data.objects.get("f1_Walls_Collision_01"))
        self.assertIsNotNone(bpy.data.objects.get("f1_Walls_Collision_02"))

    # A target that was not built is reported once and its pieces are skipped.
    def test_missing_target(self):
        params = collision_params()
        params["floors"][0]["collision"].append({"target": {"kind": "thresholds", "id": "f1"},
                                                 "pieces": [{"outline": [[1, -0.1], [2, -0.1], [2, 0.1], [1, 0.1]],
                                                             "bottom": -0.1, "top": 0.0}],
                                                 "index": 0, "hulls": []})
        result = self.build_open(params)
        self.assertEqual([p["code"] for p in result["output"]["problems"]], ["collision_failed"])
        self.assertEqual(result["output"]["counts"]["collision"], 5)

    # A job without collision gives no Collision collection and a count of 0.
    def test_none(self):
        result = self.build_open(copy.deepcopy(load_params("square_room")))
        self.assertEqual(result["output"]["counts"]["collision"], 0)
        self.assertIsNone(bpy.data.collections.get("Collision"))


class ItemHullTests(BuildTest):
    # Writes a blend file with one collection holding a box (or a flat plane) with two materials, and returns
    # its path and the material names.
    def make_asset(self, asset_id, flat=False):
        scene.clear_scene()
        collection = bpy.data.collections.new(asset_id)
        bpy.context.scene.collection.children.link(collection)
        bm = bmesh.new()
        if flat:
            bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=0.5)
        else:
            bmesh.ops.create_cube(bm, size=1.0)
            bmesh.ops.translate(bm, vec=(0.0, 0.0, 0.5), verts=bm.verts)
            bmesh.ops.create_cube(bm, size=0.2, matrix=Matrix.Translation((0.0, 0.0, 1.1)))
        mesh = bpy.data.meshes.new(asset_id)
        bm.to_mesh(mesh)
        bm.free()
        names = ["%s_wood" % asset_id, "%s_metal" % asset_id]
        for name in names:
            mesh.materials.append(bpy.data.materials.new(name))
        for index, polygon in enumerate(mesh.polygons):
            polygon.material_index = index % 2
        obj = bpy.data.objects.new(asset_id, mesh)
        collection.objects.link(obj)
        path = self.path("library", asset_id, "asset.blend")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=path)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return path, names

    # Returns the maps of a material of an asset, written with the given base color size.
    def asset_maps(self, folder, label, size):
        os.makedirs(folder, exist_ok=True)
        maps = {}
        for channel in CHANNELS:
            if channel in ("opacity", "occlusion"):
                maps[channel] = None
                continue
            rgb = (128, 128, 255) if channel == "normal" else (200, 100, 50)
            maps[channel] = write_image(os.path.join(folder, "%s_%s.png" % (label, channel)).replace("\\", "/"),
                                        size if channel in ("base_color", "normal") else 8, rgb)
        return maps

    # The square room with two box items and one flat one.
    def params(self, sizes=(256, 256), flat=False):
        asset_id = "flat" if flat else "box"
        path, names = self.make_asset(asset_id, flat)
        params = copy.deepcopy(load_params("square_room"))
        params["settings"]["bevel_width"] = 0.0
        materials = [{"name": name, "maps": self.asset_maps(self.path("maps", asset_id), name, size)}
                     for name, size in zip(names, sizes)]
        params["assets"] = [{"id": asset_id, "blend_file": path, "collection": asset_id, "materials": materials}]
        size = {"width": 1.0, "depth": 1.0, "height": 1.0}
        params["floors"][0]["items"] = [
            {"id": "i1", "asset": asset_id, "location": [1.5, -2.0, 0.0], "rotation_z": 0.0, "size": size,
             "wall": None},
            {"id": "i2", "asset": asset_id, "location": [3.0, -3.0, 0.0], "rotation_z": math.radians(30.0),
             "size": size, "wall": None}]
        return upgrade(params)

    # Two items of one asset give two hulls that share one mesh, placed like their items, with the right volume.
    def test_two_hulls_share_a_mesh(self):
        result = self.build(self.params())
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["collision"], 2)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        bpy.context.view_layer.update()
        first = bpy.data.objects["box_i1_Collision_00"]
        second = bpy.data.objects["box_i2_Collision_00"]
        self.assertIs(first.data, second.data)
        for hull, item in ((first, "i1"), (second, "i2")):
            empty = bpy.data.objects["box_%s" % item]
            self.assertEqual(hull["plynth_kind"], "collision")
            self.assertTrue(hull.hide_render)
            self.assertEqual(hull["plynth_collision_for"], empty.name)
            for row in range(4):
                for column in range(4):
                    self.assertAlmostEqual(hull.matrix_world[row][column], empty.matrix_world[row][column],
                                           delta=1e-6)
        self.assertTrue(is_closed(first))
        self.assertAlmostEqual(mesh_volume(first), hull_volume(first), delta=1e-6)
        low, high = scene.world_bounds([first])
        self.assertAlmostEqual(low.z, 0.0, delta=1e-5)
        self.assertAlmostEqual(high.z, 1.2, delta=1e-5)

    # A flat asset has no hull: the item is reported at its marker and the other items are unaffected.
    def test_hull_that_fails(self):
        result = self.build(self.params(flat=True))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        problems = result["output"]["problems"]
        self.assertEqual([(p["code"], p["severity"], p["object"], p["floor"], p["step"]) for p in problems],
                         [("collision_failed", "warning", "i1", "f1", "collision"),
                          ("collision_failed", "warning", "i2", "f1", "collision")])
        self.assertEqual(result["output"]["counts"]["collision"], 0)
        self.assertEqual(result["output"]["counts"]["items"], 2)

    # The furniture materials are listed with their asset, with the maps of the job.
    def test_materials_file(self):
        params = self.params()
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(validate(data, load_schema("build-materials")), [])
        listed = [m for m in data["materials"] if m.get("asset") == "box"]
        self.assertEqual(sorted(m["name"] for m in listed), ["box_metal", "box_wood"])
        self.assertEqual(result["output"]["counts"]["materials"], len(data["materials"]) - 2)
        self.assertEqual([m for m in data["materials"] if m.get("asset") is None and m["name"].startswith("box")], [])
        self.assertTrue(all(m["material"] is None and m["surface"] is None for m in listed))
        self.assertEqual([o for o in data["objects"] if o["name"].startswith("box_i")], [])
        by_name = {m["name"]: m for m in listed}
        self.assertEqual(by_name["box_wood"]["maps"]["base_color"],
                         params["assets"][0]["materials"][0]["maps"]["base_color"])


class PreviewTextureTests(ItemHullTests):
    # Returns the size and file path of every image the materials of a scene use.
    def image_files(self):
        found = {}
        for mat in bpy.data.materials:
            if mat.node_tree is None:
                continue
            for node in mat.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image is not None:
                    found[(mat.name, node.label)] = (tuple(node.image.size),
                                                     os.path.abspath(bpy.path.abspath(node.image.filepath)))
        return found

    # A 2048 px base color and normal map get 1024 px copies in preview_textures that building.fbx and
    # materials.json use, while building.blend keeps the full maps. A 512 px map gets no copy.
    def test_big_maps_get_copies(self):
        params = self.params(sizes=(2048, 512))
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        full = params["assets"][0]["materials"]
        copies = sorted(os.listdir(self.path("build", "preview_textures")))
        self.assertEqual(copies, ["box_box_wood_base_color.png", "box_box_wood_normal.png"])
        for name in copies:
            image = bpy.data.images.load(self.path("build", "preview_textures", name), check_existing=False)
            self.assertEqual(tuple(image.size), (1024, 1024))
            bpy.data.images.remove(image)

        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(validate(data, load_schema("build-materials")), [])
        wood = [m for m in data["materials"] if m["name"] == "box_wood"][0]
        metal = [m for m in data["materials"] if m["name"] == "box_metal"][0]
        self.assertEqual(wood["maps"]["base_color"], self.path("build", "preview_textures",
                                                                "box_box_wood_base_color.png"))
        self.assertEqual(wood["maps"]["roughness"], full[0]["maps"]["roughness"])
        self.assertEqual(metal["maps"]["base_color"], full[1]["maps"]["base_color"])

        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        files = self.image_files()
        size, path = files[("box_wood", "base_color")]
        self.assertEqual(size, (2048, 2048))
        self.assertEqual(path.replace("\\", "/").lower(), full[0]["maps"]["base_color"].lower())

        scene.clear_scene()
        for mat in list(bpy.data.materials):
            bpy.data.materials.remove(mat)
        bpy.ops.import_scene.fbx(filepath=self.path("build", "building.fbx"))
        loaded = {os.path.abspath(bpy.path.abspath(i.filepath)).replace("\\", "/").lower(): tuple(i.size)
                  for i in bpy.data.images if i.filepath}
        copy_path = self.path("build", "preview_textures", "box_box_wood_base_color.png").lower()
        self.assertEqual(loaded[os.path.abspath(copy_path).replace("\\", "/")], (1024, 1024))
        self.assertNotIn(full[0]["maps"]["base_color"].lower(), loaded)
        self.assertIn(full[1]["maps"]["base_color"].lower(), loaded)

    # Scaling keeps the values of a normal map and gives it back as a Non-Color image.
    def test_copy_keeps_values(self):
        self.build(self.params(sizes=(2048, 512)))
        image = bpy.data.images.load(self.path("build", "preview_textures", "box_box_wood_normal.png"),
                                     check_existing=False)
        image.colorspace_settings.name = "Non-Color"
        pixel = list(image.pixels[:4])
        bpy.data.images.remove(image)
        for got, wanted in zip(pixel, (128 / 255.0, 128 / 255.0, 1.0, 1.0)):
            self.assertAlmostEqual(got, wanted, delta=0.01)

    # Maps no larger than 1024 px make no copies and no preview_textures folder.
    def test_small_maps_make_no_copies(self):
        result = self.build(self.params(sizes=(1024, 512)))
        self.assertTrue(result["ok"], result["error"])
        self.assertFalse(os.path.exists(self.path("build", "preview_textures")))

    # A build removes the copies of an earlier build that are no longer used, and only image files.
    def test_old_copies_are_cleared(self):
        params = self.params(sizes=(2048, 512))
        self.build(params)
        folder = self.path("build", "preview_textures")
        stale = os.path.join(folder, "gone_base_color.png")
        keep = os.path.join(folder, "notes.txt")
        for path in (stale, keep):
            with open(path, "w") as handle:
                handle.write("x")
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(sorted(os.listdir(folder)),
                         ["box_box_wood_base_color.png", "box_box_wood_normal.png", "notes.txt"])
        os.remove(keep)
        params["assets"][0]["materials"] = [
            dict(m, maps=self.asset_maps(self.path("maps", "box"), m["name"], 512))
            for m in params["assets"][0]["materials"]]
        self.build(params)
        self.assertFalse(os.path.exists(folder))

    # Library materials too: a catalog material with a 2048 px base color shared by two slots gets one copy.
    def test_catalog_material_copy_is_shared(self):
        folder = self.path("library", "paint_a")
        os.makedirs(folder, exist_ok=True)
        maps = {}
        for channel in CHANNELS:
            if channel in ("opacity", "occlusion"):
                maps[channel] = None
            else:
                maps[channel] = write_image(os.path.join(folder, channel + ".png").replace("\\", "/"),
                                            2048 if channel == "base_color" else 4, (90, 90, 90))
        params = copy.deepcopy(load_params("square_room"))
        params["settings"]["bevel_width"] = 0.0
        params["materials"] = [{"id": "paint_a", "tile_size": {"width": 1.0, "height": 1.0}, "maps": maps}]
        for solid in params["floors"][0]["wall_solids"]:
            for ring in [solid["outline"]] + solid["holes"]:
                for edge in ring["edges"]:
                    if edge["room"] is not None:
                        edge["material"] = "paint_a"
        for room in params["floors"][0]["rooms"]:
            room["ceiling_material"] = "paint_a"
        upgrade(params)
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(os.listdir(self.path("build", "preview_textures")), ["paint_a_base_color.png"])
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        shown = [m for m in data["materials"] if m["material"] == "paint_a"]
        self.assertEqual(len(shown), 2)
        for entry in shown:
            self.assertEqual(entry["maps"]["base_color"], self.path("build", "preview_textures",
                                                                    "paint_a_base_color.png"))


if __name__ == "__main__":
    unittest.main()
