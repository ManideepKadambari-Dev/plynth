"""Cleanup cases found with real store models: packed and unsaved images, hidden helpers, large faces, shaders
in node groups and mixes, and dense models. Every source is a small .blend made by the test."""
import os
import time
import unittest

import bmesh
import bpy
import numpy as np

import blend_helpers as bh
from cleanup_helpers import CleanupTest
from material_helpers import MaterialTest
from plynth import support_finder
from plynth.building import assets as build_assets
from plynth.materials import file_is_magenta, is_magenta, read_pixels


# Returns the mean sRGB bytes of a map file.
def mean_rgb(path):
    return np.rint(read_pixels(path)[:, :, :3].reshape(-1, 3).mean(axis=0) * 255.0)


# Adds a mesh object made of the given points and faces to the scene and returns it.
def add_mesh(name, points, faces, edges=()):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(points, list(edges), faces)
    mesh.uv_layers.new(name="UVMap")
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# Adds a box of the given size whose bottom sits on z = 0 and returns it.
def add_box(name, size, x=0.0):
    sx, sy, sz = size
    points = [(x + px * sx, py * sy, pz * sz) for pz in (0, 1) for py in (-0.5, 0.5) for px in (-0.5, 0.5)]
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    return add_mesh(name, points, faces)


# Returns the points of a flat round disc of n corners at height z.
def disc_points(n, radius, z, x=0.0):
    return [(x + radius * np.cos(2 * np.pi * i / n), radius * np.sin(2 * np.pi * i / n), z) for i in range(n)]


# A material whose base color goes through a Hue/Saturation node from an image, so it is rendered by Eevee.
def hue_material(name, image):
    material, principled = bh.new_material(name)
    tree = material.node_tree
    texture = bh.node(tree, "ShaderNodeTexImage", image=image)
    hue = bh.node(tree, "ShaderNodeHueSaturation")
    tree.links.new(texture.outputs["Color"], hue.inputs["Color"])
    tree.links.new(hue.outputs["Color"], principled.inputs["Base Color"])
    return material


# Makes a flat 32 px png of one colour, as the store files' images, and returns the image loaded from it.
def flat_png(path, rgb):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bh.write_png(path, np.full((32, 32, 3), rgb, dtype=np.uint8))
    image = bpy.data.images.load(path, check_existing=False)
    image.colorspace_settings.name = "sRGB"
    return image


class PackedImageTests(CleanupTest):
    # Saves the scene as the source .blend and cleans it.
    def clean_scene(self):
        source = self.path("in", "model.blend")
        os.makedirs(os.path.dirname(source), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return self.cleanup(source, "blend")

    # An image packed in the .blend, read through a node, bakes to its own colour and not to magenta. The packed
    # file names a folder beside the download that does not exist, as the store's files do.
    def test_packed_image_through_a_node_is_not_magenta(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        image = flat_png(self.path("pictures", "walnut.png"), (120, 80, 40))
        image.pack()
        image.filepath = "//textures_2k/walnut_diffuse.png"
        os.remove(self.path("pictures", "walnut.png"))
        box = add_box("Stand", (1.0, 0.4, 0.4))
        box.data.materials.append(hue_material("Walnut", image))
        result = self.clean_scene()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        entry = result["output"]["material_maps"][0]["maps"]["base_color"]
        self.assertEqual(entry["from"], "baked")
        path = self.path("item", *entry["file"].split("/"))
        self.assertFalse(file_is_magenta(path))
        np.testing.assert_allclose(mean_rgb(path), [120, 80, 40], atol=3)
        self.assertFalse(file_is_magenta(self.path("item", "thumb.png")))
        self.open_asset()
        for image in bpy.data.images:
            if image.source == "FILE":
                self.assertTrue(image.filepath.replace("\\", "/").startswith("//textures/"), image.filepath)

    # An image on disk beside the source, read through a node, bakes to its own colour too.
    def test_image_file_through_a_node_is_not_magenta(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        image = flat_png(self.path("in", "tex", "oak.png"), (200, 150, 90))
        box = add_box("Box", (0.5, 0.5, 0.5))
        box.data.materials.append(hue_material("Oak", image))
        result = self.clean_scene()
        self.assertTrue(result["ok"], result["error"])
        entry = result["output"]["material_maps"][0]["maps"]["base_color"]
        np.testing.assert_allclose(mean_rgb(self.path("item", *entry["file"].split("/"))), [200, 150, 90], atol=3)

    # An image that is missing and read through a node stops the import with a sentence naming it, and writes
    # no magenta map.
    def test_missing_image_through_a_node_fails_clearly(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        image = flat_png(self.path("in", "tex", "gone.png"), (10, 200, 10))
        box = add_box("Box", (0.5, 0.5, 0.5))
        box.data.materials.append(hue_material("Lost", image))
        source = self.path("in", "model.blend")
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        os.remove(self.path("in", "tex", "gone.png"))
        result = self.cleanup(source, "blend")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "import_failed")
        self.assertIn("gone.png", result["error"]["message"])
        self.assertIn("Lost", result["error"]["message"])
        for root, _dirs, files in os.walk(self.path("item")):
            for name in files:
                if name.endswith(".png"):
                    self.assertFalse(file_is_magenta(os.path.join(root, name)), name)

    # The magenta test takes pure magenta and nothing else.
    def test_magenta_test(self):
        magenta = np.zeros((8, 8, 4), dtype=np.float32)
        magenta[:, :, 0] = magenta[:, :, 2] = magenta[:, :, 3] = 1.0
        self.assertTrue(is_magenta(magenta))
        pink = magenta.copy()
        pink[:, :, 1] = 0.4
        self.assertFalse(is_magenta(pink))
        self.assertFalse(is_magenta(np.zeros((0, 0, 4))))


class HiddenObjectTests(CleanupTest):
    # Objects hidden from render, by themselves or by their collection, are removed and not measured.
    def test_hidden_helpers_are_removed(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        add_box("Rug", (3.0, 2.0, 0.01))
        helper = add_box("Helper", (6.0, 1.8, 0.4))
        helper.hide_render = True
        helper.hide_viewport = True
        spare = add_box("Spare", (5.0, 1.0, 0.4))
        bpy.context.scene.collection.objects.unlink(spare)
        variants = bpy.data.collections.new("Variants")
        bpy.context.scene.collection.children.link(variants)
        variants.objects.link(spare)
        variants.hide_render = True
        source = self.path("in", "rug.blend")
        os.makedirs(os.path.dirname(source), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        result = self.cleanup(source, "blend")
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        size = result["output"]["size"]
        self.assertAlmostEqual(size["width"], 3.0, places=4)
        self.assertAlmostEqual(size["depth"], 2.0, places=4)
        self.assertAlmostEqual(size["height"], 0.01, places=4)
        self.assertEqual(result["output"]["objects"], 1)
        messages = [w["message"] for w in result["warnings"] if w["code"] == "removed_objects"]
        self.assertEqual(messages, ["Removed 2 hidden objects."])

    # A file whose every mesh is hidden from render still imports, with all of them.
    def test_all_hidden_is_kept(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        box = add_box("Only", (1.0, 1.0, 1.0))
        box.hide_render = True
        source = self.path("in", "only.blend")
        os.makedirs(os.path.dirname(source), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        result = self.cleanup(source, "blend")
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["objects"], 1)


class LargeFaceTests(CleanupTest):
    # Saves a model with a round 64 corner face on a material of its own, a loose edge and a loose point.
    def make_source(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        points = [(-0.3, -0.3, 0.0), (0.3, -0.3, 0.0), (0.3, 0.3, 0.0), (-0.3, 0.3, 0.0)]
        points += disc_points(64, 0.1, 0.0, x=1.0)
        points += [(2.0, 0.0, 0.0), (2.0, 0.0, 0.1), (3.0, 0.0, 0.0)]
        faces = [(0, 1, 2, 3), tuple(range(4, 68))]
        obj = add_mesh("Knob", points, faces, edges=[(68, 69)])
        obj.data.materials.append(bh.plain_material("Chrome"))
        obj.data.materials.append(bh.plain_material("Black", color=(0.02, 0.02, 0.02, 1.0)))
        obj.data.polygons[1].material_index = 1
        source = self.path("in", "knob.blend")
        os.makedirs(os.path.dirname(source), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return source

    # Returns the largest corner count and the loose edges and points of a mesh.
    def shape(self, mesh):
        used_edges = {key for polygon in mesh.polygons for key in polygon.edge_keys}
        used_points = {index for polygon in mesh.polygons for index in polygon.vertices}
        loose_edges = sum(1 for edge in mesh.edges if edge.key not in used_edges)
        loose_points = sum(1 for vertex in mesh.vertices if vertex.index not in used_points)
        return max(len(p.vertices) for p in mesh.polygons), loose_edges, loose_points

    # The cleaned asset has only triangles and quads and nothing loose, and asset.fbx holds no face of more
    # than four corners.
    def test_cleanup_splits_large_faces(self):
        result = self.cleanup(self.make_source(), "blend")
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        objects = self.open_asset()
        for obj in objects:
            self.assertEqual(self.shape(obj.data), (4, 0, 0))
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=self.path("item", "asset.fbx"))
        for obj in bpy.context.scene.objects:
            if obj.type == "MESH":
                self.assertLessEqual(max(len(p.vertices) for p in obj.data.polygons), 4)

    # A build splits the large faces of an asset that was imported before the cleanup did, without changing
    # the asset file.
    def test_build_splits_large_faces_of_old_assets(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        obj = add_mesh("Old", disc_points(64, 0.1, 0.0), [tuple(range(64))])
        collection = bpy.data.collections.new("old")
        bpy.context.scene.collection.children.link(collection)
        bpy.context.scene.collection.objects.unlink(obj)
        collection.objects.link(obj)
        path = self.path("lib", "asset.blend")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=path, compress=False)
        before = os.path.getmtime(path)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        loaded, why = build_assets.load_asset({"blend_file": path, "collection": "old"})
        self.assertEqual(why, "")
        for item in loaded.all_objects:
            self.assertLessEqual(max(len(p.vertices) for p in item.data.polygons), 4)
            self.assertEqual(len(item.data.polygons), 62)
        self.assertEqual(os.path.getmtime(path), before)


class DenseModelTests(CleanupTest):
    # A model with many small upward faces is cleaned in seconds, and its top is found as one support.
    def test_dense_model_is_quick(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        add_box("Table", (1.2, 0.8, 0.75))
        bpy.ops.mesh.primitive_grid_add(x_subdivisions=300, y_subdivisions=300, size=1.0, location=(0, 0, 0.75))
        source = self.path("in", "dense.blend")
        os.makedirs(os.path.dirname(source), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        started = time.monotonic()
        result = self.cleanup(source, "blend")
        elapsed = time.monotonic() - started
        self.assertTrue(result["ok"], result["error"])
        self.assertGreater(result["output"]["triangles"], 170000)
        self.assertLess(elapsed, 60.0)
        tops = [s for s in result["output"]["supports"] if abs(s["z"] - 0.75) < 1e-3]
        self.assertEqual(len(tops), 1)
        self.assertAlmostEqual(tops[0]["x1"] - tops[0]["x0"], 1.2, places=3)
        self.assertIn("Found 1 supports", self.printed)

    # The support finder joins a grid of 40000 touching squares into one place quickly.
    def test_finder_joins_a_large_grid(self):
        triangles = []
        step = 0.005
        for i in range(200):
            for j in range(200):
                x, y = i * step, j * step
                a, b, c, d = (x, y, 0.5), (x + step, y, 0.5), (x + step, y + step, 0.5), (x, y + step, 0.5)
                triangles += [[a, b, c], [a, c, d]]
        started = time.monotonic()
        supports = support_finder.find_supports(triangles)
        self.assertLess(time.monotonic() - started, 20.0)
        self.assertEqual(len(supports), 1)
        self.assertAlmostEqual(supports[0]["x1"] - supports[0]["x0"], 1.0, places=6)


class ShaderLayoutTests(MaterialTest):
    # Writes one material into a .blend file and cleans it as a material import. Returns the result.
    def clean_material(self, material):
        name = material.name
        path = self.path("in", name + ".blend")
        bh.write_blend(path, [material])
        params = self.clean_params(path, kind="blend", bake_size=256)
        params["source"]["material"] = name
        params["tile_size"] = {"width": 1.0, "height": 1.0}
        params["thumbnail"]["render"] = False
        self.out_folder = params["output_folder"]
        return self.run_task("cleanup_material", params)

    # Returns the mean bytes of a made map.
    def mean(self, result, channel):
        return mean_rgb(self.map_path(result, channel))

    # A Principled BSDF inside a node group, its base color from a colour node in the group, is read.
    def test_principled_inside_a_group(self):
        group = bpy.data.node_groups.new("Tiles", "ShaderNodeTree")
        group.interface.new_socket(name="BSDF", in_out="OUTPUT", socket_type="NodeSocketShader")
        exit_node = group.nodes.new("NodeGroupOutput")
        inner = group.nodes.new("ShaderNodeBsdfPrincipled")
        color = group.nodes.new("ShaderNodeRGB")
        color.outputs[0].default_value = (0.2, 0.4, 0.8, 1.0)
        group.links.new(color.outputs[0], inner.inputs["Base Color"])
        inner.inputs["Roughness"].default_value = 0.25
        group.links.new(inner.outputs["BSDF"], exit_node.inputs[0])
        material, principled = bh.new_material("Grouped")
        tree = material.node_tree
        tree.nodes.remove(principled)
        holder = tree.nodes.new("ShaderNodeGroup")
        holder.node_tree = group
        output = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        tree.links.new(holder.outputs[0], output.inputs["Surface"])
        result = self.clean_material(material)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        maps = result["output"]["maps"]
        self.assertEqual(maps["base_color"]["from"], "baked")
        np.testing.assert_allclose(self.mean(result, "base_color"), [124, 170, 231], atol=3)
        self.assertEqual(maps["roughness"]["from"], "value")
        self.assertEqual(maps["roughness"]["detail"], "0.25")

    # A mix of two Principled BSDFs gives the mix of their base colors.
    def test_mix_of_two_principled(self):
        material, first = bh.new_material("Mixed")
        tree = material.node_tree
        first.inputs["Base Color"].default_value = (1.0, 0.0, 0.0, 1.0)
        second = tree.nodes.new("ShaderNodeBsdfPrincipled")
        second.inputs["Base Color"].default_value = (0.0, 0.0, 1.0, 1.0)
        mix = tree.nodes.new("ShaderNodeMixShader")
        mix.inputs[0].default_value = 0.5
        tree.links.new(first.outputs["BSDF"], mix.inputs[1])
        tree.links.new(second.outputs["BSDF"], mix.inputs[2])
        output = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        tree.links.new(mix.outputs[0], output.inputs["Surface"])
        result = self.clean_material(material)
        self.assertTrue(result["ok"], result["error"])
        np.testing.assert_allclose(self.mean(result, "base_color"), [188, 0, 188], atol=3)

    # A diffuse shader alone is read as a rough Principled BSDF of its colour.
    def test_diffuse_shader(self):
        material, principled = bh.new_material("Matte")
        tree = material.node_tree
        tree.nodes.remove(principled)
        diffuse = tree.nodes.new("ShaderNodeBsdfDiffuse")
        diffuse.inputs["Color"].default_value = (0.0, 1.0, 0.0, 1.0)
        output = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        tree.links.new(diffuse.outputs[0], output.inputs["Surface"])
        result = self.clean_material(material)
        self.assertTrue(result["ok"], result["error"])
        maps = result["output"]["maps"]
        self.assertEqual(maps["base_color"]["from"], "value")
        np.testing.assert_allclose(self.mean(result, "base_color"), [0, 255, 0], atol=1)
        self.assertEqual(maps["roughness"]["detail"], "1")

    # A shader Plynth cannot read is still refused with a sentence.
    def test_unreadable_shader_is_refused(self):
        material, principled = bh.new_material("Glass")
        tree = material.node_tree
        tree.nodes.remove(principled)
        glass = tree.nodes.new("ShaderNodeBsdfTransparent")
        output = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        tree.links.new(glass.outputs[0], output.inputs["Surface"])
        result = self.clean_material(material)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "no_base_color")
        self.assertIn("Glass", result["error"]["message"])


if __name__ == "__main__":
    unittest.main()
