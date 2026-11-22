import copy
import glob
import json
import os
import unittest

import bpy

from build_helpers import DATA_FOLDER, BuildTest, load_params
from plynth import scene, shading
from slot_fixtures import upgrade
from schema_check import load_schema, validate

CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
COLORS = {"paint_a": (200, 40, 40), "paint_b": (40, 40, 200)}


# Returns the face attribute values of an object as a list.
def face_values(obj, name):
    return [item.value for item in obj.data.attributes[name].data]


# Returns a path as the preview compares it: forward slashes and lower case.
def normal_path(path):
    return os.path.abspath(bpy.path.abspath(path)).replace("\\", "/").lower()


# Returns the (u, v) of every corner of a polygon.
def corner_uvs(obj, polygon):
    layer = obj.data.uv_layers["UVMap"]
    return [tuple(layer.uv[i].vector) for i in polygon.loop_indices]


class MaterialBuildTest(BuildTest):
    # Writes the map images of a library material and returns its job entry.
    def make_material(self, material_id, tile=(2.0, 1.0), opacity=False, occlusion=True):
        folder = self.path("library", material_id)
        os.makedirs(folder, exist_ok=True)
        maps = {}
        for channel in CHANNELS:
            wanted = (channel != "opacity" or opacity) and (channel != "occlusion" or occlusion)
            if wanted:
                path = os.path.join(folder, channel + ".png").replace("\\", "/")
                if channel == "base_color":
                    color = COLORS.get(material_id, (90, 90, 90))
                elif channel == "normal":
                    color = (128, 128, 255)
                else:
                    color = (100, 100, 100)
                shading.write_constant_png(path, color)
                maps[channel] = path
            else:
                maps[channel] = None
        return {"id": material_id, "tile_size": {"width": tile[0], "height": tile[1]}, "maps": maps}

    # Sets materials on a fixture and adds the library entries.
    def with_materials(self, name="square_room", inside=None, outside=None, top=None, floor=None, ceiling=None,
                       bevel=0.0, entries=()):
        params = copy.deepcopy(load_params(name))
        params["settings"]["bevel_width"] = bevel
        params["materials"] = list(entries)
        for floor_data in params["floors"]:
            for solid in floor_data["wall_solids"]:
                solid["top_material"] = top
                for ring in [solid["outline"]] + solid["holes"]:
                    for edge in ring["edges"]:
                        if edge["side"] is not None:
                            edge["material"] = inside if edge["room"] is not None else outside
            for room in floor_data["rooms"]:
                room["floor_material"] = floor
                room["ceiling_material"] = ceiling
        upgrade(params)
        return params

    # Builds and opens building.blend.
    def build_open(self, params, **overrides):
        result = self.build(params, **overrides)
        self.assertTrue(result["ok"], result["error"])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return result

    # Returns the polygons of an object with the name of their material.
    def named_polygons(self, obj):
        names = [m.name for m in obj.data.materials]
        return [(p, names[p.material_index]) for p in obj.data.polygons]

    # Returns the image file of each channel node of a material as a path with forward slashes.
    def images_of(self, name):
        mat = bpy.data.materials[name]
        return {n.label: bpy.path.abspath(n.image.filepath).replace("\\", "/")
                for n in mat.node_tree.nodes if n.type == "TEX_IMAGE"}

    # Reads the materials.json the build wrote.
    def read_materials(self):
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            return json.load(handle)


class FixtureTests(unittest.TestCase):
    # Every build job fixture matches the job schema.
    def test_fixtures_valid(self):
        schema = load_schema("blender-job")
        files = sorted(glob.glob(os.path.join(DATA_FOLDER, "*.json")))
        self.assertGreaterEqual(len(files), 11)
        for path in files:
            with open(path, "r", encoding="utf-8") as handle:
                job = json.load(handle)
            self.assertEqual(validate(job, schema), [], os.path.basename(path))


class WallMaterialTests(MaterialBuildTest):
    # Inside faces use the inside material, outside faces the neutral Wall_Outside, tops the top material.
    def test_faces(self):
        params = self.with_materials(inside="paint_a", top="paint_b",
                                     entries=[self.make_material("paint_a"), self.make_material("paint_b")])
        self.assertEqual([(s["name"], s["material"]) for s in params["slots"]],
                         [("Exterior_Wall", None), ("Living_Ceiling", None), ("Living_Floor", None),
                          ("Living_Wall", "paint_a"), ("Wall_Top", "paint_b")])
        job = {"format": "plynth-blender-job", "version": 1, "task": "build", "job_id": "x",
               "result_file": "r.json", "params": params}
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        self.build_open(params)
        walls = bpy.data.objects["SM_f1_Walls"]
        sides = face_values(walls, "plynth_side")
        rooms = face_values(walls, "plynth_room")
        seen = {"inside": 0, "outside": 0, "top": 0}
        for polygon, name in self.named_polygons(walls):
            if sides[polygon.index] == 3:
                self.assertEqual(name, "Wall_Top")
                seen["top"] += 1
            elif rooms[polygon.index] == 0:
                self.assertEqual(name, "Living_Wall")
                seen["inside"] += 1
            else:
                self.assertEqual(name, "Exterior_Wall")
                seen["outside"] += 1
        self.assertEqual(seen["inside"], 4)
        self.assertEqual(seen["outside"], 4)
        self.assertGreater(seen["top"], 0)
        self.assertTrue(self.images_of("Living_Wall")["base_color"].endswith("paint_a/base_color.png"))
        outside = self.images_of("Exterior_Wall")
        self.assertEqual(os.path.basename(outside["base_color"]), "Wall_Outside_base_color.png")
        listed = {m["name"]: m for m in self.read_materials()["materials"]}
        self.assertEqual(listed["Exterior_Wall"]["surface"], "Wall_Outside")

    # A straight 4 m face with a 2 x 1 tile has a u range of 2 and a v range of 3.
    def test_wall_uvs(self):
        points = [[0.0, 0.0], [4.0, 0.0], [4.0, 4.0], [0.0, 4.0]]
        params = self.with_materials(outside="paint_a", entries=[self.make_material("paint_a")])
        solid = params["floors"][0]["wall_solids"][0]
        solid["outline"] = {"points": points, "edges": [
            {"wall": "w%d" % i, "side": "right", "room": None, "material": "paint_a"} for i in range(4)]}
        solid["holes"] = []
        params["floors"][0]["rooms"] = []
        upgrade(params)
        self.build_open(params)
        walls = bpy.data.objects["SM_f1_Walls"]
        sides = face_values(walls, "plynth_side")
        checked = 0
        for polygon, _ in self.named_polygons(walls):
            if sides[polygon.index] == 3:
                continue
            uvs = corner_uvs(walls, polygon)
            us = [u for u, _ in uvs]
            vs = [v for _, v in uvs]
            self.assertAlmostEqual(max(us) - min(us), 2.0, delta=1e-5)
            self.assertAlmostEqual(max(vs) - min(vs), 3.0, delta=1e-5)
            checked += 1
        self.assertEqual(checked, 4)

    # Round the corners of the outer ring u carries on from one face to the next.
    def test_u_continues_at_corners(self):
        params = self.with_materials(outside="paint_a", entries=[self.make_material("paint_a", tile=(1.0, 1.0))])
        self.build_open(params)
        walls = bpy.data.objects["SM_f1_Walls"]
        sides = face_values(walls, "plynth_side")
        rooms = face_values(walls, "plynth_room")
        layer = walls.data.uv_layers["UVMap"]
        found = {}
        for polygon in walls.data.polygons:
            if sides[polygon.index] == 3 or rooms[polygon.index] != -1:
                continue
            for index in polygon.loop_indices:
                vert = walls.data.vertices[walls.data.loops[index].vertex_index].co
                if vert.z < 0.01:
                    key = (round(vert.x, 3), round(vert.y, 3))
                    found.setdefault(key, set()).add(round(layer.uv[index].vector[0], 3))
        for corner, expected in (((-0.1, -4.1), 4.2), ((4.1, -4.1), 8.4), ((4.1, 0.1), 12.6)):
            self.assertEqual(found[corner], {expected}, corner)

    # Floor UVs are world X and Y over the tile size, ceiling UVs are X and minus Y.
    def test_floor_and_ceiling_uvs(self):
        params = self.with_materials(floor="paint_a", ceiling="paint_b",
                                     entries=[self.make_material("paint_a", tile=(2.0, 0.5)),
                                              self.make_material("paint_b", tile=(1.0, 4.0))])
        self.build_open(params)
        for name, tile, sign in (("SM_f1_r1_Floor", (2.0, 0.5), 1.0), ("SM_f1_r1_Ceiling", (1.0, 4.0), -1.0)):
            obj = bpy.data.objects[name]
            layer = obj.data.uv_layers["UVMap"]
            for polygon in obj.data.polygons:
                for index in polygon.loop_indices:
                    co = obj.data.vertices[obj.data.loops[index].vertex_index].co
                    u, v = layer.uv[index].vector
                    self.assertAlmostEqual(u, co.x / tile[0], delta=1e-6)
                    self.assertAlmostEqual(v, sign * co.y / tile[1], delta=1e-6)


class CutTests(MaterialBuildTest):
    # The reveal faces have side 4 and the reveal material, and a face far from the opening keeps its tags,
    # material and UVs through the cut, the bevel and the triangulate step.
    def test_reveal_and_far_faces(self):
        for bevel in (0.0, 0.01):
            with self.subTest(bevel=bevel):
                entries = [self.make_material("paint_a", tile=(1.0, 1.0)), self.make_material("paint_b")]
                params = self.with_materials("door", inside="paint_a", bevel=bevel, entries=entries)
                params["floors"][0]["openings"][0]["materials"]["reveal"] = "paint_b"
                upgrade(params)
                self.build_open(params)
                walls = bpy.data.objects["SM_f1_Walls"]
                sides = face_values(walls, "plynth_side")
                reveal = [(p, n) for p, n in self.named_polygons(walls) if sides[p.index] == 4]
                self.assertGreater(len(reveal), 0)
                for _, name in reveal:
                    self.assertEqual(name, "Living_Wall_PaintB")
                checked = 0
                for polygon, name in self.named_polygons(walls):
                    if abs(polygon.normal.y - 1.0) < 1e-3 and abs(polygon.center.y + 3.9) < 1e-4:
                        self.assertEqual(name, "Living_Wall_PaintA")
                        self.assertEqual(sides[polygon.index], 0)
                        uvs = corner_uvs(walls, polygon)
                        tolerance = 1e-5 if bevel == 0.0 else 0.05
                        self.assertAlmostEqual(max(u for u, _ in uvs) - min(u for u, _ in uvs), 3.8, delta=tolerance)
                        self.assertAlmostEqual(max(v for _, v in uvs) - min(v for _, v in uvs), 3.0, delta=tolerance)
                        checked += 1
                self.assertGreaterEqual(checked, 1)

    # The parts of a door use the opening's materials, and parts with one material share a slot.
    def test_door_slots(self):
        entries = [self.make_material("paint_a"), self.make_material("paint_b")]
        params = self.with_materials("door", entries=entries)
        materials = params["floors"][0]["openings"][0]["materials"]
        materials.update({"door": "paint_a", "frame": "paint_a", "handle": "paint_b", "threshold": "paint_b"})
        upgrade(params)
        self.build_open(params)
        door = bpy.data.objects["SM_f1_o1_Door"]
        self.assertEqual([m.name for m in door.data.materials], ["Frame", "Door", "Handle"])
        self.assertEqual({p.material_index for p in door.data.polygons}, {0, 1, 2})
        thresholds = bpy.data.objects["SM_f1_Thresholds"]
        self.assertEqual([m.name for m in thresholds.data.materials], ["Living_Floor_PaintB"])
        shown = {m["name"]: m["material"] for m in self.read_materials()["materials"]}
        self.assertEqual((shown["Door"], shown["Frame"], shown["Handle"], shown["Living_Floor_PaintB"]),
                         ("paint_a", "paint_a", "paint_b", "paint_b"))
        self.assertEqual(self.images_of("Door"), self.images_of("Frame"))


class MaterialNodeTests(MaterialBuildTest):
    # A library material has image nodes for its maps with the right colour spaces and is blended only with
    # an opacity map.
    def test_nodes(self):
        entries = [self.make_material("paint_a", opacity=False, occlusion=True),
                   self.make_material("paint_b", opacity=True, occlusion=False)]
        self.build_open(self.with_materials(inside="paint_a", outside="paint_b", entries=entries))
        a = bpy.data.materials["Living_Wall"]
        b = bpy.data.materials["Exterior_Wall"]
        nodes_a = {n.label: n for n in a.node_tree.nodes if n.type == "TEX_IMAGE"}
        nodes_b = {n.label: n for n in b.node_tree.nodes if n.type == "TEX_IMAGE"}
        self.assertEqual(sorted(nodes_a), ["base_color", "metallic", "normal", "occlusion", "roughness"])
        self.assertEqual(sorted(nodes_b), ["base_color", "metallic", "normal", "opacity", "roughness"])
        for nodes in (nodes_a, nodes_b):
            for label, node in nodes.items():
                expected = "sRGB" if label == "base_color" else "Non-Color"
                self.assertEqual(node.image.colorspace_settings.name, expected, label)
        self.assertTrue(any(n.type == "NORMAL_MAP" for n in a.node_tree.nodes))
        self.assertEqual(b.surface_render_method, "BLENDED")
        self.assertNotEqual(a.surface_render_method, "BLENDED")
        shader = [n for n in b.node_tree.nodes if n.type == "BSDF_PRINCIPLED"][0]
        self.assertTrue(shader.inputs["Alpha"].is_linked)

    # A face with no material gets the neutral surface made of images in build/textures.
    def test_neutral_images(self):
        self.build_open(self.with_materials(inside="paint_a", entries=[self.make_material("paint_a")]))
        mat = bpy.data.materials["Exterior_Wall"]
        images = [n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE"]
        self.assertEqual(len(images), 4)
        for image in images:
            self.assertEqual(os.path.dirname(normal_path(image.filepath)), normal_path(self.path("build", "textures")))
            self.assertTrue(os.path.basename(image.filepath).startswith("Wall_Outside_"))
        self.assertEqual(len(glob.glob(self.path("build", "textures", "Wall_Outside_*.png"))), 4)

    # Neutral images left by an earlier build are removed, and other files in the folder are kept.
    def test_stale_neutral_images_cleared(self):
        folder = self.path("build", "textures")
        os.makedirs(folder)
        for name in ("Glass_base_color.png", "Glass_opacity.png", "keep_me.png"):
            with open(os.path.join(folder, name), "wb") as handle:
                handle.write(b"x")
        self.build_open(self.with_materials(inside="paint_a", entries=[self.make_material("paint_a")]))
        self.assertFalse(glob.glob(os.path.join(folder, "Glass_*.png")))
        self.assertTrue(os.path.isfile(os.path.join(folder, "keep_me.png")))
        self.assertEqual(len(glob.glob(os.path.join(folder, "Wall_Outside_*.png"))), 4)

    # A map file that is missing gives material_failed and the neutral look, built from images of its own.
    def test_missing_map(self):
        entry = self.make_material("paint_a")
        entry["maps"]["roughness"] = self.path("library", "paint_a", "gone.png")
        result = self.build_open(self.with_materials(inside="paint_a", entries=[entry]))
        self.assert_result_valid(result)
        problems = result["output"]["problems"]
        self.assertEqual([p["code"] for p in problems], ["material_failed"])
        self.assertEqual(problems[0]["severity"], "warning")
        mat = bpy.data.materials["Living_Wall"]
        images = [n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE"]
        self.assertEqual(len(images), 4)
        self.assertTrue(all("textures" in normal_path(i.filepath) for i in images))
        failed = [m for m in self.read_materials()["materials"] if m["name"] == "Living_Wall"][0]
        self.assertIsNone(failed["material"])
        self.assertEqual(failed["surface"], "Wall_Inside")


class MaterialsFileTests(MaterialBuildTest):
    # materials.json validates, lists every built object but furniture with its slots in Blender order, no
    # temporary file is left, the count is right and the result validates.
    def test_file(self):
        entries = [self.make_material("paint_a"), self.make_material("paint_b", opacity=True)]
        params = self.with_materials("door", inside="paint_a", floor="paint_b", entries=entries)
        result = self.build_open(params)
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["materials_file"], "materials.json")
        self.assertEqual(sorted(os.listdir(self.path("build"))),
                         ["building.blend", "building.fbx", "materials.json", "textures"])
        data = self.read_materials()
        self.assertEqual(validate(data, load_schema("build-materials")), [])
        built = sorted(o.name for o in bpy.data.objects
                       if o.type == "MESH" and o.get("plynth_kind") not in (None, "item", "collision"))
        self.assertEqual([o["name"] for o in data["objects"]], built)
        for entry in data["objects"]:
            obj = bpy.data.objects[entry["name"]]
            self.assertEqual(entry["slots"], [m.name for m in obj.data.materials])
        self.assertEqual(result["output"]["counts"]["materials"], len(data["materials"]))
        by_name = {m["name"]: m for m in data["materials"]}
        self.assertEqual(by_name["Living_Floor_PaintB"]["alpha_mode"], "blend")
        self.assertEqual(by_name["Living_Wall_PaintA"]["alpha_mode"], "opaque")
        self.assertEqual(by_name["Living_Wall_PaintA"]["material"], "paint_a")
        self.assertEqual(by_name["Exterior_Wall"]["surface"], "Wall_Outside")
        for entry in data["materials"]:
            for path in entry["maps"].values():
                if path:
                    self.assertTrue(os.path.isabs(path) and "\\" not in path)
                    self.assertTrue(os.path.isfile(path))

    # Slots with different materials have their own base color image, and materials.json names exactly the file
    # the image node uses, in the blend file and after the FBX is read back.
    def test_base_color_paths(self):
        entries = [self.make_material("paint_a"), self.make_material("paint_b")]
        self.build_open(self.with_materials("door", inside="paint_a", floor="paint_b", entries=entries))
        data = self.read_materials()
        listed = [normal_path(m["maps"]["base_color"]) for m in data["materials"] if m["material"]]
        self.assertEqual(len(listed), len(set(listed)))
        self.assertEqual(len(listed), 2)
        for entry in data["materials"]:
            mat = bpy.data.materials[entry["name"]]
            node = [n for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.label == "base_color"][0]
            self.assertEqual(normal_path(node.image.filepath), normal_path(entry["maps"]["base_color"]))
        scene.clear_scene()
        bpy.ops.import_scene.fbx(filepath=self.path("build", "building.fbx"))
        loaded = set()
        for mat in bpy.data.materials:
            if mat.node_tree is None:
                continue
            for node in mat.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image is not None and node.image.filepath:
                    loaded.add(normal_path(node.image.filepath))
        self.assertTrue(set(listed) <= loaded)

    # Two slots on one catalog material share its image files, and each has its own Blender material.
    def test_slots_share_files(self):
        entries = [self.make_material("paint_a")]
        params = self.with_materials(inside="paint_a", ceiling="paint_a", entries=entries)
        self.build_open(params)
        listed = {m["name"]: m for m in self.read_materials()["materials"]}
        self.assertEqual(listed["Living_Wall"]["maps"], listed["Living_Ceiling"]["maps"])
        self.assertEqual(listed["Living_Wall"]["material"], "paint_a")
        self.assertIsNot(bpy.data.materials["Living_Wall"], bpy.data.materials["Living_Ceiling"])
        self.assertEqual(self.images_of("Living_Wall"), self.images_of("Living_Ceiling"))

    # Without an fbx_file there is no materials.json.
    def test_no_fbx(self):
        params = self.with_materials(inside="paint_a", entries=[self.make_material("paint_a")])
        result = self.build(params, fbx_file=None, materials_file=None)
        self.assertTrue(result["ok"], result["error"])
        self.assertIsNone(result["output"]["materials_file"])
        self.assertFalse(os.path.exists(self.path("build", "materials.json")))
        self.assertFalse(glob.glob(self.path("build", "*.part*")))


if __name__ == "__main__":
    unittest.main()
