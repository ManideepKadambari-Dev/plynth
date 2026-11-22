import copy
import json
import os

import bpy

import helpers
from build_helpers import load_params
from generator_helpers import GeneratorTest, job_item
from plynth import registry

EXTRA_NAMES = ("leftover_object", "leftover_mesh", "leftover_empty", "unused_mesh", "unused_material",
               "unused_nodes", "unused_collection", "plynth_generator_work")


# Returns the vertex positions of an object as a sorted list of rounded tuples.
def positions(obj):
    return sorted(tuple(round(c, 6) for c in v.co) for v in obj.data.vertices)


class SampleBuildTests(GeneratorTest):
    # Builds the sample job and returns the result.
    def build_samples(self, params=None):
        return self.build_open(params or self.sample_params())

    # The column and the railing are one mesh each, placed at the item, with identity scale.
    def test_objects(self):
        result = self.build_samples()
        self.assertEqual(result["output"]["problems"], [])
        self.assertEqual(result["output"]["counts"]["generators"], 2)
        column = bpy.data.objects["SM_column_i5"]
        railing = bpy.data.objects["SM_railing_i6"]
        for obj, location, rotation in ((column, (2.0, -2.0, 0.0), 0.0), (railing, (2.0, -0.2, 0.0), -0.5)):
            self.assertEqual(obj.type, "MESH")
            self.assertEqual(tuple(round(s, 6) for s in obj.scale), (1.0, 1.0, 1.0))
            self.assertEqual(tuple(round(c, 6) for c in obj.location), location)
            self.assertAlmostEqual(obj.rotation_euler.z, rotation, 6)
        self.assertEqual(column["plynth_kind"], "generator")
        self.assertEqual(column["plynth_generator"], "column")
        self.assertEqual(column["plynth_id"], "i5")
        built = [o for o in bpy.data.objects if o.type == "MESH" and o.get("plynth_kind") == "generator"]
        self.assertEqual(len(built), 2)

    # The bottom sits on the floor and the size is within 1 cm of the job's size.
    def test_size(self):
        self.build_samples()
        for name, size in (("SM_column_i5", (0.3, 0.3, 3.0)), ("SM_railing_i6", (1.5, 0.05, 0.9))):
            obj = bpy.data.objects[name]
            xs = [v.co.x for v in obj.data.vertices]
            ys = [v.co.y for v in obj.data.vertices]
            zs = [v.co.z for v in obj.data.vertices]
            self.assertAlmostEqual(min(zs), 0.0, 6)
            self.assertAlmostEqual(max(xs) - min(xs), size[0], delta=0.01)
            self.assertAlmostEqual(max(ys) - min(ys), size[1], delta=0.01)
            self.assertAlmostEqual(max(zs), size[2], delta=0.01)

    # UVs are in metres divided by the tile size of the material; the slot is the material id.
    def test_uvs_and_slots(self):
        self.build_samples(self.sample_params(tile=(2.0, 1.0)))
        column = bpy.data.objects["SM_column_i5"]
        self.assertEqual([m.name for m in column.data.materials], ["white_paint"])
        layer = column.data.uv_layers["UVMap"]
        checked = 0
        for polygon in column.data.polygons:
            if abs(polygon.normal.z) > 0.9:
                for index in polygon.loop_indices:
                    co = column.data.vertices[column.data.loops[index].vertex_index].co
                    self.assertAlmostEqual(layer.uv[index].vector.x, co.x / 2.0, 5)
                    self.assertAlmostEqual(layer.uv[index].vector.y, co.y / 1.0, 5)
                    checked += 1
        self.assertGreater(checked, 0)
        railing = bpy.data.objects["SM_railing_i6"]
        self.assertEqual([m.name for m in railing.data.materials], ["brushed_steel"])

    # A null material gives the neutral Generated slot with its constant image in the textures folder.
    def test_neutral_material(self):
        params = self.sample_params()
        params["floors"][0]["generator_items"][0]["params"]["material"] = None
        self.build_samples(params)
        column = bpy.data.objects["SM_column_i5"]
        self.assertEqual([m.name for m in column.data.materials], ["Generated"])
        files = os.listdir(self.path("build", "textures"))
        self.assertTrue([f for f in files if f.startswith("Generated")], files)

    # A square column has four side faces.
    def test_square(self):
        params = self.sample_params()
        values = params["floors"][0]["generator_items"][0]["params"]
        values.update({"shape": "square", "plinth": False})
        self.build_samples(params)
        column = bpy.data.objects["SM_column_i5"]
        sides = [p for p in column.data.polygons if abs(p.normal.z) < 0.1]
        self.assertEqual(len(sides), 4)

    # The same job twice gives the same vertex positions.
    def test_repeatable(self):
        self.build_samples()
        first = {n: positions(bpy.data.objects[n]) for n in ("SM_column_i5", "SM_railing_i6")}
        self.build_samples()
        second = {n: positions(bpy.data.objects[n]) for n in ("SM_column_i5", "SM_railing_i6")}
        self.assertEqual(first, second)

    # The meshes are closed.
    def test_closed(self):
        self.build_samples()
        from plynth import meshes
        for name in ("SM_column_i5", "SM_railing_i6"):
            self.assertTrue(meshes.is_manifold(bpy.data.objects[name]), name)

    # The generator objects are in the FBX and in materials.json.
    def test_fbx_and_materials_file(self):
        self.build_samples()
        self.assertTrue(os.path.getsize(self.path("build", "building.fbx")) > 0)
        with open(self.path("build", "materials.json"), "r", encoding="utf-8") as handle:
            text = json.dumps(json.load(handle))
        self.assertIn("white_paint", text)
        self.assertIn("brushed_steel", text)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=self.path("build", "building.fbx"))
        names = [o.name for o in bpy.data.objects]
        self.assertTrue([n for n in names if "SM_column_i5" in n], names)
        self.assertTrue([n for n in names if "SM_railing_i6" in n], names)

    # The job fixture matches the job schema.
    def test_job_valid(self):
        self.assert_job_valid("generators_room")


class ProblemTests(GeneratorTest):
    # A generator that raises gives generator_failed with the last line, and the build carries on.
    def test_raises(self):
        params = self.room_params({"raises": "raises", "seeded": "uses_seed"},
                                  [job_item("i1", "raises", {"material": None}, [1.0, -1.0, 0.0]),
                                   job_item("i2", "seeded", {"material": None}, [3.0, -3.0, 0.0])])
        result = self.build_open(params)
        failed = self.problems(result, "generator_failed")
        self.assertEqual(len(failed), 1)
        self.assertEqual(failed[0]["severity"], "error")
        self.assertEqual(failed[0]["object"], "i1")
        self.assertTrue(failed[0]["message"].endswith("the box fell over"), failed[0]["message"])
        self.assertIsNone(bpy.data.objects.get("SM_raises_i1"))
        self.assertIsNotNone(bpy.data.objects.get("SM_seeded_i2"))
        self.assertEqual(result["output"]["counts"]["generators"], 1)

    # A generator that returns nothing gives generator_failed.
    def test_nothing(self):
        params = self.room_params({"nothing": "nothing"}, [job_item("i1", "nothing", {"material": None}, [1, -1, 0])])
        result = self.build_open(params)
        self.assertEqual([p["object"] for p in self.problems(result, "generator_failed")], ["i1"])
        self.assertEqual(result["output"]["counts"]["generators"], 0)

    # A file that cannot be loaded gives generator_failed for its item.
    def test_broken_file(self):
        params = self.room_params({"nofn": "no_function"}, [job_item("i1", "nofn", {"material": None}, [1, -1, 0])])
        result = self.build_open(params)
        self.assertEqual([p["object"] for p in self.problems(result, "generator_failed")], ["i1"])

    # A generator that needs an add-on that is not there is a warning and nothing is built.
    def test_missing_addon(self):
        params = self.room_params({"needs": "needs_missing_addon"},
                                  [job_item("i1", "needs", {"material": None}, [1, -1, 0])])
        result = self.build_open(params)
        found = self.problems(result, "generator_unavailable")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["severity"], "warning")
        self.assertEqual(self.problems(result, "generator_failed"), [])
        self.assertIsNone(bpy.data.objects.get("SM_needs_i1"))

    # Extra objects and unused data a generator leaves are removed.
    def test_clean(self):
        params = self.room_params({"extra": "leaves_extra"},
                                  [job_item("i1", "extra", {"material": None}, [1, -1, 0])])
        self.build_open(params)
        for name in EXTRA_NAMES:
            for kind in ("objects", "meshes", "materials", "node_groups", "collections"):
                self.assertNotIn(name, getattr(bpy.data, kind), "%s in %s" % (name, kind))
        self.assertEqual([m.name for m in bpy.data.meshes if m.users == 0], [])
        self.assertIsNotNone(bpy.data.objects.get("SM_extra_i1"))

    # A result 5 cm too wide gives generator_size but is still built.
    def test_size_warning(self):
        params = self.room_params({"wide": "too_wide"}, [job_item("i1", "wide", {"material": None}, [1, -1, 0])])
        result = self.build_open(params)
        found = self.problems(result, "generator_size")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["severity"], "warning")
        self.assertIsNotNone(bpy.data.objects.get("SM_wide_i1"))

    # A job without generators builds as before and has no generators count.
    def test_no_generators(self):
        params = load_params("square_room")
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertNotIn("generators", result["output"]["counts"])
        self.assertEqual(result["output"]["problems"], [])

    # The step is registered at order 70, and no file of the worker uses the render engine bake or a bake call.
    def test_registry_and_text(self):
        from plynth import building
        registry.discover(building)
        self.assertEqual(registry.BUILD_STEPS.get("generators").order, 70)
        for folder in ("generating", "building"):
            self.check_text(os.path.join(helpers.BLENDER_FOLDER, "plynth", folder))
        self.check_text(os.path.join(helpers.REPO_FOLDER, "samples", "generators"))

    # Fails when a python file under the folder uses the render engine bake or calls a bake function.
    def check_text(self, folder):
        for root, _, files in os.walk(folder):
            for name in files:
                if name.endswith(".py"):
                    text = open(os.path.join(root, name), encoding="utf-8").read().lower()
                    self.assertNotIn("bake(", text, name)
                    self.assertNotIn("cyc" + "les", text, name)


class TimingTests(GeneratorTest):
    # Twenty columns: prints the time of the step.
    def test_twenty_columns(self):
        params = self.sample_params()
        items = []
        for i in range(20):
            item = copy.deepcopy(params["floors"][0]["generator_items"][0])
            item["id"] = "c%d" % i
            item["location"] = [0.5 + (i % 5) * 0.7, -0.5 - (i // 5) * 0.7, 0.0]
            items.append(item)
        params["floors"][0]["generator_items"] = items
        result = self.build_open(params)
        self.assertEqual(result["output"]["counts"]["generators"], 20)
        step = [s for s in result["output"]["steps"] if s["step"] == "generators"][0]
        print("GENERATORS 20 columns: %d ms" % step["ms"])
