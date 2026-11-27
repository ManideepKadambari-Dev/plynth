import copy
import json
import os
import unittest
from unittest import mock

import bmesh
import bpy

import helpers
from build_helpers import DATA_FOLDER, load_params
from generator_helpers import SAMPLES, GeneratorTest
from plynth import meshes, scene, shading
from schema_check import load_schema, validate
from slot_fixtures import upgrade

CORE_COPY = os.path.join(helpers.REPO_FOLDER, "tests", "core", "data", "buildjob", "from_core_generators.json")
USER_PARTS = (("APPDATA", "appdata"), ("LOCALAPPDATA", "local"), ("USERPROFILE", "profile"), ("HOME", "home"),
              ("XDG_DATA_HOME", "xdg"), ("TEMP", "temp"), ("TMP", "temp"), ("BLENDER_USER_RESOURCES", "resources"),
              ("BLENDER_USER_CONFIG", "config"), ("BLENDER_USER_SCRIPTS", "scripts"),
              ("BLENDER_USER_EXTENSIONS", "extensions"), ("BLENDER_USER_DATAFILES", "datafiles"),
              ("PLYNTH_CONFIG_DIR", "settings"), ("PLYNTH_BLENDER_PRIVATE", "private"))
BOX_FACES = [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]
L_FACES = [[5, 4, 3, 2, 1, 0], [6, 7, 8, 9, 10, 11], [0, 1, 7, 6], [1, 2, 8, 7], [2, 3, 9, 8], [3, 4, 10, 9],
           [4, 5, 11, 10], [5, 0, 6, 11]]


# Points every user and Plynth folder variable at folders of this test until it ends.
def isolate(test):
    values = {}
    for name, part in USER_PARTS:
        values[name] = test.path("user", part)
        os.makedirs(values[name], exist_ok=True)
    patcher = mock.patch.dict(os.environ, values)
    patcher.start()
    test.addCleanup(patcher.stop)


# Returns the declaration of a material parameter that starts empty.
def material_param(param_id="material"):
    return {"id": param_id, "label": "Material", "type": "material", "default": None}


# Returns the world positions of the vertices of an object.
def world_points(obj):
    return [obj.matrix_world @ v.co for v in obj.data.vertices]


# Returns true when every vertex lies behind every face.
def is_convex(obj):
    points = [v.co for v in obj.data.vertices]
    return all((point - face.center).dot(face.normal) <= 1e-5 for face in obj.data.polygons for point in points)


class SlotBuildCase(GeneratorTest):
    # Starts with user folders of its own and an empty list of generators and slots.
    def setUp(self):
        super().setUp()
        isolate(self)
        self.generators = []
        self.rows = []
        self.materials = []

    # Writes a generator file of a box, an L shape or a flat square that then runs the body lines on obj.
    def add_generator(self, generator_id, params=None, body="", addons=(), shape="box"):
        params = [material_param()] if params is None else params
        if not params:
            params = [{"id": "amount", "label": "Amount", "type": "number", "default": 1.0}]
        declaration = {"format": "plynth-generator", "version": 1, "name": "Test part",
                       "description": "A part for tests.", "addons": list(addons),
                       "size": {"width": 2.0, "depth": 2.0, "height": 2.0}, "params": params}
        lines = ["def generate(gen):", "    w, d, h = gen.size", "    x, y = w / 2, d / 2"]
        if shape == "box":
            lines += ["    ring = [(-x, -y), (x, -y), (x, y), (-x, y)]", "    faces = %r" % BOX_FACES]
        elif shape == "l":
            lines += ["    ring = [(-x, -y), (x, -y), (x, 0), (0, 0), (0, y), (-x, y)]", "    faces = %r" % L_FACES]
        if shape == "flat":
            lines += ["    verts = [(-x, -y, 0), (x, -y, 0), (x, y, 0), (-x, y, 0)]", "    faces = [[0, 1, 2, 3]]"]
        else:
            lines += ["    verts = [(px, py, z) for z in (0, h) for px, py in ring]"]
        lines += ["    obj = gen.mesh_object('part', verts, faces)"]
        lines += ["    " + line for line in body.splitlines()]
        lines += ["    return [obj]"]
        path = self.path("generators", generator_id + ".py")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write('"""plynth-generator\n%s\n"""\n\n\n%s\n' % (json.dumps(declaration), "\n".join(lines)))
        self.generators.append({"id": generator_id, "file": path, "addons": list(addons)})

    # Adds a row to the slot table of the job.
    def add_slot(self, name, material=None, surface="Generated"):
        self.rows.append({"name": name, "material": material, "surface": surface})

    # Returns a generator item with the slot of each material parameter.
    def item(self, item_id, generator, params, slots, location=(2.0, -2.0, 0.0), rotation=0.0, size=(1.0, 0.6, 1.2)):
        return {"id": item_id, "generator": generator, "params": params, "location": list(location),
                "rotation_z": rotation, "size": dict(zip(("width", "depth", "height"), size)), "room": None,
                "wall": None, "slots": slots}

    # Returns the job params of the square room with these items, and of a second bare floor when it has any.
    def job(self, items, upstairs=()):
        params = upgrade(copy.deepcopy(load_params("square_room")))
        params["settings"]["bevel_width"] = 0.0
        params["materials"] = self.materials
        params["generators"] = sorted(self.generators, key=lambda entry: entry["id"])
        params["floors"][0]["generator_items"] = list(items)
        if upstairs:
            upper = copy.deepcopy(params["floors"][0])
            upper.update({"id": "f2", "elevation": 3.2, "wall_solids": [], "openings": [], "rooms": [], "items": [],
                          "collision": [], "stairs": [], "roofs": [], "generator_items": list(upstairs)})
            params["floors"].append(upper)
        params["slots"] = sorted(params["slots"] + self.rows, key=lambda row: row["name"])
        return params

    # Builds, opens building.blend and reads materials.json.
    def built(self, params):
        result = self.build_open(params)
        bpy.context.view_layer.update()
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(validate(data, load_schema("build-materials")), [])
        self.entries = {entry["name"]: entry for entry in data["materials"]}
        self.object_slots = {entry["name"]: entry["slots"] for entry in data["objects"]}
        return result

    # Returns the names of the materials of an object.
    def material_names(self, name):
        return [m.name for m in bpy.data.objects[name].data.materials]


class SlotNameTests(SlotBuildCase):
    # The slot name is the material name in building.blend and materials.json, and gen.params keeps the id.
    def test_slot_name(self):
        body = "assert gen.params['material'] == 'white_paint', repr(gen.params['material'])\n"
        body += "mat = gen.material('material')\nassert mat.name == 'Box_Material', mat.name\n"
        body += "obj.data.materials.append(mat)"
        self.add_generator("box", body=body)
        self.materials = [self.make_material("white_paint")]
        self.add_slot("Box_Material", "white_paint")
        result = self.built(self.job([self.item("g1", "box", {"material": "white_paint"}, {"material": "Box_Material"})]))
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(self.material_names("box_g1"), ["Box_Material"])
        self.assertEqual(self.object_slots["box_g1"], ["Box_Material"])
        self.assertEqual(self.entries["Box_Material"]["material"], "white_paint")
        self.assertIsNone(self.entries["Box_Material"]["surface"])
        self.assertNotIn("white_paint", self.entries)

    # Items with other materials take the names the job gives, also the neutral one.
    def test_names_come_from_the_job(self):
        self.add_generator("box", body="obj.data.materials.append(gen.material('material'))")
        self.materials = [self.make_material("oak_planks"), self.make_material("white_paint")]
        cases = (("g1", "white_paint", "Box_Material_WhitePaint"), ("g2", "oak_planks", "Box_Material_OakPlanks"),
                 ("g3", None, "Box_Material_Neutral"))
        items = []
        for number, (item_id, material, slot) in enumerate(cases):
            self.add_slot(slot, material)
            items.append(self.item(item_id, "box", {"material": material}, {"material": slot},
                                   location=(0.7 + number, -1.0, 0.0), size=(0.5, 0.5, 1.0)))
        result = self.built(self.job(items))
        self.assertEqual(result["output"]["problems"], [])
        for item_id, material, slot in cases:
            self.assertEqual(self.material_names("box_%s" % item_id), [slot])
            self.assertEqual(self.entries[slot]["material"], material)
        self.assertEqual(self.entries["Box_Material_Neutral"]["surface"], "Generated")
        self.assertNotIn("Generated", self.entries)

    # Two parameters give two slots on the right faces, each with the tile size of its material.
    def test_two_materials(self):
        body = ("obj.data.materials.append(gen.material('body'))\nobj.data.materials.append(gen.material('trim'))\n"
                "obj.data.polygons[1].material_index = 1")
        self.add_generator("two", [material_param("body"), material_param("trim")], body)
        self.materials = [self.make_material("oak_planks", (4.0, 4.0)), self.make_material("white_paint", (2.0, 1.0))]
        self.add_slot("Two_Body", "white_paint")
        self.add_slot("Two_Trim", "oak_planks")
        result = self.built(self.job([self.item("g1", "two", {"body": "white_paint", "trim": "oak_planks"},
                                                {"body": "Two_Body", "trim": "Two_Trim"})]))
        self.assertEqual(result["output"]["problems"], [])
        obj = bpy.data.objects["two_g1"]
        self.assertEqual(sorted(self.material_names("two_g1")), ["Two_Body", "Two_Trim"])
        layer = obj.data.uv_layers["UVMap"]
        for face in obj.data.polygons:
            name = obj.data.materials[face.material_index].name
            self.assertEqual(name, "Two_Trim" if face.normal.z > 0.9 else "Two_Body")
            if abs(face.normal.z) > 0.9:
                tile = 4.0 if name == "Two_Trim" else 2.0
                for index in face.loop_indices:
                    x = obj.data.vertices[obj.data.loops[index].vertex_index].co.x
                    self.assertAlmostEqual(layer.uv[index].vector.x, x / tile, 5)
        self.assertEqual(self.entries["Two_Trim"]["material"], "oak_planks")

    # A face with any other material takes the slot of the first declared parameter.
    def test_other_material(self):
        body = ("gen.material('trim')\nimport bpy\n"
                "obj.data.materials.append(bpy.data.materials.new('not_a_slot'))")
        self.add_generator("two", [material_param("body"), material_param("trim")], body)
        self.add_slot("Two_Body")
        self.add_slot("Two_Trim")
        self.built(self.job([self.item("g1", "two", {"body": None, "trim": None},
                                       {"body": "Two_Body", "trim": "Two_Trim"})]))
        self.assertEqual(self.material_names("two_g1"), ["Two_Body"])
        self.assertNotIn("not_a_slot", bpy.data.materials)
        self.assertNotIn("Two_Trim", self.entries)
        self.assertNotIn("Generated", self.entries)

    # A generator without a material parameter uses Generated.
    def test_generated(self):
        self.add_generator("plain", [])
        self.add_slot("Generated")
        result = self.built(self.job([self.item("g1", "plain", {"amount": 1.0}, {})]))
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(self.material_names("plain_g1"), ["Generated"])
        self.assertEqual(self.entries["Generated"]["surface"], "Generated")
        self.assertIsNone(self.entries["Generated"]["material"])


class FurnitureMaterialTests(SlotBuildCase):
    # Writes a furniture asset whose only material is red and has the given name.
    def make_asset(self, material_name, with_maps=False):
        scene.clear_scene()
        collection = bpy.data.collections.new("stool")
        bpy.context.scene.collection.children.link(collection)
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=0.4)
        mesh = bpy.data.meshes.new("stool")
        bm.to_mesh(mesh)
        bm.free()
        material = bpy.data.materials.new(material_name)
        material.use_nodes = True
        shader = next(n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        shader.inputs["Base Color"].default_value = (1.0, 0.0, 0.0, 1.0)
        mesh.materials.append(material)
        collection.objects.link(bpy.data.objects.new("stool", mesh))
        path = self.path("library", "stool", "asset.blend")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=path)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        listed = []
        if with_maps:
            listed = [{"name": material_name, "maps": self.make_maps("stool_maps")}]
        return {"id": "stool", "blend_file": path, "collection": "stool", "materials": listed}

    # Writes the maps of the red furniture material.
    def make_maps(self, name):
        maps = self.make_material(name)["maps"]
        shading.write_constant_png(maps["base_color"], (255, 0, 0))
        return maps

    # Builds the red stool beside a generator box and checks that each kept its own material.
    def check_stool(self, furniture_material, plain=False, with_maps=False):
        asset = self.make_asset(furniture_material, with_maps)
        self.materials = [self.make_material("white_paint")]
        slot = "Generated" if plain else "Box_Material"
        if plain:
            self.add_generator("box", [])
            self.add_slot(slot)
            placed = self.item("g1", "box", {"amount": 1.0}, {})
        else:
            self.add_generator("box", body="obj.data.materials.append(gen.material('material'))")
            self.add_slot(slot, "white_paint")
            placed = self.item("g1", "box", {"material": "white_paint"}, {"material": slot})
        params = self.job([placed])
        params["assets"] = [asset]
        params["floors"][0]["items"] = [{"id": "i1", "asset": "stool", "location": [1.0, -1.0, 0.0], "rotation_z": 0.0,
                                         "size": {"width": 0.4, "depth": 0.4, "height": 0.4}, "wall": None}]
        self.built(params)
        stool = bpy.data.collections["stool"].all_objects[0].data.materials[0]
        shader = next(n for n in stool.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        color = shader.inputs["Base Color"]
        if with_maps:
            self.assertTrue(color.is_linked)
            image = os.path.abspath(bpy.path.abspath(color.links[0].from_node.image.filepath))
            wanted = os.path.abspath(asset["materials"][0]["maps"]["base_color"])
            self.assertEqual(os.path.normcase(image), os.path.normcase(wanted), "the furniture maps were written over")
            entry = self.entries[stool.name]
            self.assertEqual(entry["asset"], "stool")
            self.assertEqual(entry["maps"], asset["materials"][0]["maps"])
            self.assertIsNone(self.entries[slot].get("asset"))
            self.assertEqual(self.entries[slot]["maps"], self.materials[0]["maps"])
        else:
            self.assertFalse(color.is_linked, "the furniture material was written over")
            self.assertEqual(tuple(round(c, 4) for c in color.default_value)[:3], (1.0, 0.0, 0.0))
        built = bpy.data.objects["box_g1"].data.materials[0]
        self.assertNotEqual(built, stool)
        self.assertEqual(built.name, slot)
        self.assertEqual(self.entries[slot]["material"], None if plain else "white_paint")

    # A generator material id that is also the name of a furniture material leaves that material as it was.
    def test_same_as_material_id(self):
        self.check_stool("white_paint")

    # So does a furniture material with the name of the slot.
    def test_same_as_slot(self):
        self.check_stool("Box_Material")

    # So does one named Generated.
    def test_same_as_generated(self):
        self.check_stool("Generated", plain=True)

    # One that lists maps keeps them and its asset, and the slot keeps its own.
    def test_same_as_slot_with_maps(self):
        self.check_stool("Box_Material", with_maps=True)


class GeneratorHullTests(SlotBuildCase):
    # Returns the collision objects of the built file.
    def hulls(self):
        return sorted((o for o in bpy.data.objects if o.get("plynth_kind") == "collision"), key=lambda o: o.name)

    # Each item gets one closed convex hull of its own mesh, turned and placed with it.
    def test_one_hull_each(self):
        self.add_generator("box", [])
        self.add_slot("Generated")
        items = [self.item("g1", "box", {"amount": 1.0}, {}, (1.0, -1.0, 0.0), 0.7, (1.0, 0.4, 1.2)),
                 self.item("g2", "box", {"amount": 1.0}, {}, (3.0, -3.0, 0.25), 0.0, (0.3, 0.5, 2.0))]
        result = self.built(self.job(items))
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["collision"], 2)
        hulls = self.hulls()
        self.assertEqual([o.name for o in hulls], ["box_g1_Collision_00", "box_g2_Collision_00"])
        for hull, item in zip(hulls, items):
            target = bpy.data.objects["box_%s" % item["id"]]
            self.assertEqual(hull["plynth_id"], item["id"])
            self.assertEqual(hull["plynth_collision_for"], target.name)
            self.assertIn(hull, list(bpy.data.collections["Collision"].objects))
            self.assertTrue(meshes.is_manifold(hull))
            self.assertTrue(is_convex(hull))
            points = world_points(hull)
            corners = world_points(target)
            self.assertEqual(len(points), 8)
            for corner in corners:
                self.assertLess(min((corner - point).length for point in points), 1e-4)
            self.assertNotIn(hull.name, self.object_slots)

    # A hollow shape gets its convex hull, not a copy of itself.
    def test_hull_of_l_shape(self):
        self.add_generator("bend", [], shape="l")
        self.add_slot("Generated")
        self.built(self.job([self.item("g1", "bend", {"amount": 1.0}, {}, size=(1.0, 1.0, 1.0))]))
        hulls = self.hulls()
        self.assertEqual([o.name for o in hulls], ["bend_g1_Collision_00"])
        self.assertTrue(is_convex(hulls[0]))
        self.assertAlmostEqual(meshes.volume(hulls[0]), 0.875, places=4)

    # An item that failed or was left out has no hull and no collision warning.
    def test_none_for_failed(self):
        self.add_generator("broken", [], "raise ValueError('it fell over')")
        self.add_generator("needs", [], addons=("plynth_test_missing",))
        self.add_generator("box", [])
        self.add_slot("Generated")
        result = self.built(self.job([self.item("g1", "broken", {"amount": 1.0}, {}),
                                      self.item("g2", "needs", {"amount": 1.0}, {}),
                                      self.item("g3", "box", {"amount": 1.0}, {})]))
        self.assertEqual([o.name for o in self.hulls()], ["box_g3_Collision_00"])
        self.assertEqual([(p["code"], p["object"]) for p in result["output"]["problems"]],
                         [("generator_failed", "g1"), ("generator_unavailable", "g2")])
        self.assertEqual(result["output"]["counts"]["collision"], 1)

    # A flat result is built and warns that it has no hull.
    def test_flat(self):
        self.add_generator("mat", [], shape="flat")
        self.add_slot("Generated")
        result = self.built(self.job([self.item("g1", "mat", {"amount": 1.0}, {})]))
        self.assertIsNotNone(bpy.data.objects.get("mat_g1"))
        self.assertEqual(self.hulls(), [])
        self.assertEqual([(p["code"], p["severity"], p["floor"], p["object"], p["step"])
                          for p in result["output"]["problems"]],
                         [("collision_failed", "warning", "f1", "g1", "collision")])
        self.assertEqual(result["output"]["counts"]["generators"], 1)

    # An item of floor 2 and its hull stand on that floor.
    def test_floor_two(self):
        self.add_generator("box", [])
        self.add_slot("Generated")
        result = self.built(self.job([], [self.item("g9", "box", {"amount": 1.0}, {}, (2.0, -2.0, 3.2))]))
        self.assertEqual(result["output"]["problems"], [])
        obj = bpy.data.objects["box_g9"]
        self.assertAlmostEqual(min(p.z for p in world_points(obj)), 3.2, places=5)
        hulls = self.hulls()
        self.assertEqual([o.name for o in hulls], ["box_g9_Collision_00"])
        self.assertAlmostEqual(min(p.z for p in world_points(hulls[0])), 3.2, places=5)


class FromCoreGeneratorTests(GeneratorTest):
    # Starts with user folders of its own.
    def setUp(self):
        super().setUp()
        isolate(self)

    # Returns the saved job, which must be the same file the core test keeps.
    def saved_job(self):
        path = os.path.join(DATA_FOLDER, "from_core_generators.json")
        self.assertTrue(os.path.isfile(path), "from_core_generators.json is missing")
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        with open(CORE_COPY, "r", encoding="utf-8") as handle:
            self.assertEqual(text, handle.read())
        return json.loads(text)

    # Returns a copy with the folder placeholders filled in.
    def fill(self, value):
        if isinstance(value, dict):
            return {key: self.fill(part) for key, part in value.items()}
        if isinstance(value, list):
            return [self.fill(part) for part in value]
        if isinstance(value, str):
            value = value.replace("@PROJECT@", self.path("project")).replace("@LIBRARY@", self.path("library"))
            return value.replace("@GENERATORS@", SAMPLES.replace("\\", "/"))
        return value

    # The job core writes builds with no problems, every item in its slot and with its hull.
    def test_builds(self):
        job = self.saved_job()
        self.assertEqual(validate(job, load_schema("blender-job")), [])
        params = self.fill(job["params"])
        for material in params["materials"]:
            for channel, path in material["maps"].items():
                if path:
                    os.makedirs(os.path.dirname(path), exist_ok=True)
                    shading.write_constant_png(path, (200, 180, 160) if channel == "base_color" else (128, 128, 128))
        items = [(floor, item) for floor in params["floors"] for item in floor["generator_items"]]
        self.assertEqual(len(items), 4)
        result = self.build_open(params)
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["generators"], 4)
        names = [o.name for o in bpy.data.objects]
        for floor, item in items:
            name = "%s_%s" % (item["generator"], item["id"])
            obj = bpy.data.objects[name]
            self.assertEqual([m.name for m in obj.data.materials], list(item["slots"].values()))
            self.assertAlmostEqual(obj.location.z, item["location"][2], places=5)
            self.assertIn("%s_Collision_00" % name, names)


if __name__ == "__main__":
    unittest.main()
