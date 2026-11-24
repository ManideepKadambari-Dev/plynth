import os
import tempfile
import unittest
from unittest import mock

import bmesh
import bpy

import helpers  # noqa: F401
from plynth import bridges, extensions, registry
from plynth.generating import AddonUnavailable, addons

TOLERANCE = 0.01


# Returns why the add-on cannot be used here, or None when it can.
def skip_reason(short_names, label):
    if bridges.find_module(short_names) is None:
        return "%s is not installed in this Blender" % label
    if not bridges.enable_addon(short_names):
        return "%s is installed but cannot be enabled in a background run" % label
    return None


# Returns the number of edges that do not have exactly two faces, which is 0 for a closed mesh.
def open_edges(obj):
    mesh = bmesh.new()
    mesh.from_mesh(obj.data)
    count = sum(1 for edge in mesh.edges if len(edge.link_faces) != 2)
    mesh.free()
    return count


# Returns how many data blocks of each kind exist.
def counts():
    kinds = ("objects", "meshes", "curves", "materials", "node_groups", "collections")
    return {kind: len(getattr(bpy.data, kind)) for kind in kinds}


# Removes every object of the file.
def clear_objects():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


class BridgeTest(unittest.TestCase):
    addon = ()
    label = ""

    # Skips the test when its add-on is not available, and starts each test with no objects.
    def setUp(self):
        reason = skip_reason(self.addon, self.label)
        if reason:
            self.skipTest(reason)
        clear_objects()
        self.before = counts()

    # Checks the objects are plain meshes with no modifiers and that nothing else is left in the file.
    def check_clean(self, made, expected_objects=1):
        self.assertEqual(len(made), expected_objects)
        for obj in made:
            self.assertEqual(obj.type, "MESH")
            self.assertEqual(len(obj.modifiers), 0)
            self.assertGreater(len(obj.data.vertices), 0)
        self.assertEqual(sorted(o.name for o in bpy.data.objects), sorted(o.name for o in made))
        after = counts()
        for kind in ("curves", "node_groups", "collections"):
            self.assertLessEqual(after[kind], self.before[kind], kind)

    # Checks the size of an object against the one asked for, within a centimetre.
    def check_size(self, obj, expected):
        size = bridges.mesh_size(obj)
        for axis in range(3):
            self.assertAlmostEqual(size[axis], expected[axis], delta=TOLERANCE, msg="axis %d of %s" % (axis, size))


class ArchimeshTests(BridgeTest):
    addon = ("archimesh",)
    label = "Archimesh"

    # A round and a square column are one clean closed mesh as tall as asked and standing on z 0.
    def test_column(self):
        from plynth.bridges import archimesh
        for shape in ("round", "square"):
            clear_objects()
            made = archimesh.column(height=2.6, width=0.3, shape=shape, plates=False)
            self.check_clean(made)
            self.assertEqual(open_edges(made[0]), 0)
            self.assertAlmostEqual(bridges.mesh_size(made[0])[2], 2.6, delta=TOLERANCE)
            self.assertAlmostEqual(min(v.co.z for v in made[0].data.vertices), 0.0, delta=1e-4)

    # The base and cap plates make the column as wide as the plates.
    def test_column_with_plates(self):
        from plynth.bridges import archimesh
        made = archimesh.column(height=2.5, width=0.3, plates=True, plate_width=0.4)
        self.check_clean(made)
        self.check_size(made[0], (0.4, 0.4, 2.5))

    # Calling twice gives the same vertex count and leaves nothing extra.
    def test_repeat(self):
        from plynth.bridges import archimesh
        first = archimesh.column(height=2.4)
        count = len(first[0].data.vertices)
        clear_objects()
        second = archimesh.column(height=2.4)
        self.assertEqual(len(second[0].data.vertices), count)
        self.check_clean(second)

    # The stairs are one clean mesh with the width and height asked for.
    def test_stairs(self):
        from plynth.bridges import archimesh
        made = archimesh.stairs(steps=10, width=1.0, depth=0.25, height=2.8)
        self.check_clean(made)
        size = bridges.mesh_size(made[0])
        self.assertAlmostEqual(size[0], 1.0, delta=TOLERANCE)
        self.assertAlmostEqual(size[2], 2.8, delta=0.06)


class HomeBuilderTests(BridgeTest):
    addon = ("home_builder_5", "home_builder")
    label = "Home Builder 5"

    # A run of three cabinets is one clean mesh as wide as the run, standing on z 0.
    def test_base_cabinets(self):
        from plynth.bridges import home_builder
        made = home_builder.base_cabinets(count=3, width=0.6, height=0.87, depth=0.6)
        self.check_clean(made)
        size = bridges.mesh_size(made[0])
        self.assertAlmostEqual(size[0], 1.8, delta=0.05)
        self.assertAlmostEqual(size[2], 0.87, delta=0.05)
        self.assertAlmostEqual(min(v.co.z for v in made[0].data.vertices), 0.0, delta=1e-4)

    # Building twice gives the same vertex count, and the add-on's own input functions are put back.
    def test_repeat_and_restore(self):
        from plynth.bridges import home_builder
        types = home_builder.package_module("hb_types")
        original = types.GeoNodeObject.__dict__["set_input"]
        first = home_builder.base_cabinets(count=1)
        count = len(first[0].data.vertices)
        clear_objects()
        second = home_builder.base_cabinets(count=1)
        self.assertEqual(len(second[0].data.vertices), count)
        self.assertIs(types.GeoNodeObject.__dict__["set_input"], original)

    # Thirty builds in a row all come out at the asked size.
    def test_size_is_stable(self):
        from plynth.bridges import home_builder
        for _ in range(40):
            made = home_builder.base_cabinets(count=3, width=0.6, height=0.87, depth=0.546)
            size = bridges.mesh_size(made[0])
            self.assertAlmostEqual(size[0], 1.8, delta=0.005)
            self.assertAlmostEqual(size[1], 0.6, delta=0.01)
            clear_objects()


class BoolToolTests(BridgeTest):
    addon = ("bool_tool",)
    label = "Bool Tool"

    # A cylinder cut through a box leaves one closed mesh with more faces, the cutter gone and the size kept.
    def test_difference(self):
        from plynth.bridges import bool_tool
        bpy.ops.mesh.primitive_cube_add(size=2.0)
        box = bpy.context.object
        bpy.ops.mesh.primitive_cylinder_add(radius=0.3, depth=4.0, vertices=16)
        cutter = bpy.context.object
        faces_before = len(box.data.polygons)
        made = bool_tool.difference(box, [cutter])
        self.check_clean(made)
        self.assertGreater(len(made[0].data.polygons), faces_before)
        self.assertEqual(open_edges(made[0]), 0)
        self.check_size(made[0], (2.0, 2.0, 2.0))

    # A thin cutter through a small box removes material (the fast solver keeps only the overlap).
    def test_difference_volume(self):
        from plynth.bridges import bool_tool
        bpy.ops.mesh.primitive_cube_add(size=1.2)
        box = bpy.context.object
        bpy.ops.mesh.primitive_cylinder_add(radius=0.2, depth=3.0, vertices=16)
        made = bool_tool.difference(box, [bpy.context.object])
        builder = bmesh.new()
        builder.from_mesh(made[0].data)
        volume = builder.calc_volume()
        builder.free()
        self.assertGreater(volume, 1.2 ** 3 * 0.8)
        self.assertLess(volume, 1.2 ** 3)

    # Cutting with two cutters twice gives the same vertex count.
    def test_repeat(self):
        from plynth.bridges import bool_tool
        results = []
        for _ in range(2):
            clear_objects()
            bpy.ops.mesh.primitive_cube_add(size=2.0)
            box = bpy.context.object
            cutters = []
            for x in (-0.5, 0.5):
                bpy.ops.mesh.primitive_cylinder_add(radius=0.2, depth=4.0, vertices=12, location=(x, 0, 0))
                cutters.append(bpy.context.object)
            made = bool_tool.difference(box, cutters)
            self.check_clean(made)
            results.append(len(made[0].data.vertices))
        self.assertEqual(results[0], results[1])


class BagapieTests(BridgeTest):
    addon = ("bagapie",)
    label = "Bagapie"

    # A fence along a straight path is one clean mesh, with no curve and no modifier left.
    def test_fence(self):
        from plynth.bridges import bagapie
        made = bagapie.fence([(0.0, 0.0, 0.0), (3.0, 0.0, 0.0)], height=1.0, post_spacing=1.0)
        self.check_clean(made)
        size = bridges.mesh_size(made[0])
        self.assertAlmostEqual(size[2], 1.0, delta=0.06)
        self.assertGreater(size[0], 2.9)
        self.assertEqual(len(bpy.data.curves), self.before["curves"])

    # Building twice gives the same vertex count.
    def test_repeat(self):
        from plynth.bridges import bagapie
        seen = []
        for _ in range(2):
            made = bagapie.fence([(0.0, 0.0, 0.0), (2.0, 0.0, 0.0)])
            seen.append(len(made[0].data.vertices))
            clear_objects()
        self.assertEqual(seen[0], seen[1])


class AssetOptimizerTests(BridgeTest):
    addon = ("asset_optimizer",)
    label = "Asset Optimizer"

    # Makes two cubes on the same spot in one mesh object, so half the vertices are doubles.
    def doubled_cube(self):
        mesh = bpy.data.meshes.new("doubled")
        builder = bmesh.new()
        bmesh.ops.create_cube(builder, size=1.0)
        bmesh.ops.create_cube(builder, size=1.0)
        builder.to_mesh(mesh)
        builder.free()
        obj = bpy.data.objects.new("doubled", mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj

    # Merging removes the doubled vertices and keeps the size.
    def test_merge_vertices(self):
        from plynth.bridges import asset_optimizer
        obj = self.doubled_cube()
        asset_optimizer.merge_vertices([obj], 0.0001)
        self.assertEqual(len(obj.data.vertices), 8)
        self.assertEqual(len(bpy.data.objects), 1)
        self.check_size(obj, (1.0, 1.0, 1.0))

    # The lightmap unwrap adds the second UV map and leaves the first as it was.
    def test_lightmap_uvs(self):
        from plynth.bridges import asset_optimizer
        obj = self.doubled_cube()
        obj.data.uv_layers.new(name="UVMap")
        asset_optimizer.lightmap_uvs([obj])
        self.assertEqual([layer.name for layer in obj.data.uv_layers], ["UVMap", "UVMap_Lightmap"])
        self.assertEqual(len(bpy.data.objects), 1)


class RegistryTests(unittest.TestCase):
    # Every bridge file registers itself under its add-on short name.
    def test_all_registered(self):
        registry.discover(bridges)
        for name in ("archimesh", "home_builder", "home_builder_5", "bool_tool", "asset_optimizer", "bagapie"):
            self.assertIsNotNone(registry.ADDON_BRIDGES.get(name), name)

    # An add-on that is not installed raises AddonUnavailable.
    def test_unknown_addon(self):
        with self.assertRaises(AddonUnavailable):
            bridges.get("plynth_test_missing")

    # No bridge calls the LOD generator or a bake.
    def test_no_lod_or_bake(self):
        folder = os.path.dirname(bridges.__file__)
        for name in os.listdir(folder):
            if name.endswith(".py"):
                with open(os.path.join(folder, name), encoding="utf-8") as handle:
                    text = handle.read()
                self.assertNotIn("generate_lods", text)
                self.assertNotIn("bake(", text)


class SharedFolderTest(unittest.TestCase):
    # Enables every add-on a bridge uses and checks the folder they were copied from loses no library files.
    def test_shared_libraries_untouched(self):
        if not extensions.is_private():
            self.skipTest("this run does not use a prepared extensions folder")
        source = os.environ.get(extensions.SOURCE_ENV_NAME)
        if not source:
            self.skipTest("%s names no folder of add-ons" % extensions.SOURCE_ENV_NAME)
        before = extensions.shared_library_count(source)
        for short_names in (("archimesh",), ("asset_optimizer",), ("bagapie",), ("bool_tool",), ("home_builder_5",)):
            bridges.enable_addon(short_names)
        after = extensions.shared_library_count(source)
        print("Shared extension library files: %d before, %d after." % (before, after))
        self.assertGreaterEqual(after, before)

    # Checks an add-on is never enabled when the run does not use a private folder.
    def test_refuses_without_private_folder(self):
        saved = os.environ.pop(extensions.ENV_NAME, None)
        try:
            self.assertFalse(bridges.enable_addon(("bool_tool",)))
        finally:
            if saved is not None:
                os.environ[extensions.ENV_NAME] = saved

    # A private folder that was not prepared, as every job has, does not let an add-on be enabled.
    def test_refuses_private_but_unprepared(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.dict(os.environ, {extensions.ENV_NAME: folder}):
            self.assertFalse(extensions.is_private())
            self.assertFalse(bridges.enable_addon(("bool_tool",)))
            self.assertFalse(addons.enable("bool_tool"))


class PrepareTest(unittest.TestCase):
    # Makes a scratch folder with an add-on to copy and a pretend APPDATA.
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.source = os.path.join(scratch.name, "source")
        self.target = os.path.join(scratch.name, "target")
        self.root = scratch.name
        addon = os.path.join(self.source, "some_repo", "bool_tool")
        os.makedirs(addon)
        with open(os.path.join(addon, "blender_manifest.toml"), "w", encoding="utf-8") as handle:
            handle.write('id = "bool_tool"\n')
        appdata = os.path.join(scratch.name, "appdata")
        self.own = os.path.join(appdata, "Blender Foundation", "Blender", "5.2", "extensions")
        patcher = mock.patch.dict(os.environ, {"APPDATA": appdata})
        patcher.start()
        self.addCleanup(patcher.stop)

    # The given folder's add-ons are copied and the result counts as prepared.
    def test_copies_from_given_source(self):
        self.assertEqual(extensions.prepare(self.target, self.source), ["bool_tool"])
        manifest = os.path.join(self.target, "user_default", "bool_tool", "blender_manifest.toml")
        self.assertTrue(os.path.isfile(manifest))
        with mock.patch.dict(os.environ, {extensions.ENV_NAME: self.target}):
            self.assertTrue(extensions.is_private())

    # Without a source nothing is copied and the folder still counts as prepared.
    def test_no_default_source(self):
        self.assertEqual(extensions.prepare(self.target), [])
        self.assertEqual(os.listdir(os.path.join(self.target, "user_default")), [])
        self.assertTrue(os.path.isfile(os.path.join(self.target, extensions.MARKER)))

    # The folders this run uses, a prepared folder and the user's own folder are never copied from.
    def test_refuses_sources(self):
        prepared = os.path.join(self.root, "prepared")
        extensions.prepare(prepared)
        for source in [self.own, prepared] + [f for f in extensions.running_folders() if f]:
            with self.assertRaises(ValueError):
                extensions.prepare(self.target, source)
        self.assertFalse(os.path.exists(self.target))

    # The user's own folder and the folders this run uses are never prepared.
    def test_refuses_targets(self):
        for target in [self.own] + [f for f in extensions.running_folders() if f]:
            with self.assertRaises(ValueError):
                extensions.prepare(target, self.source)
        self.assertFalse(os.path.exists(self.own))

    # A child Blender gets all five user folders inside its own root, and they exist.
    def test_environment(self):
        root = os.path.join(self.root, "child")
        env = extensions.environment(root)
        for name in ("RESOURCES", "CONFIG", "SCRIPTS", "EXTENSIONS", "DATAFILES"):
            value = env["BLENDER_USER_" + name]
            self.assertTrue(extensions.is_inside(value, [root]), name)
            self.assertTrue(os.path.isdir(value), name)


if __name__ == "__main__":
    unittest.main()
