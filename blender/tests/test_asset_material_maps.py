import json
import os
import shutil
import unittest

import bpy
import numpy as np

import helpers
from blend_helpers import image_node, new_material, node, plain_material, write_noise_png
from cleanup_helpers import CleanupTest
from plynth import registry, thumbnail
from plynth.cleanup import material_maps as maps_step
from plynth.materials import bakers, read_pixels, read_size, to_bytes, write_png

SAMPLE_COUNTS = {"sofa": 2, "chair": 2, "table": 1, "tv": 2, "bed": 2}
SIX = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")


# Adds one cube with UVs at x and the materials in its slots, and returns it.
def add_cube(name, materials, x=0.0):
    bpy.ops.mesh.primitive_cube_add(size=0.5, location=(x, 0, 0.25))
    cube = bpy.context.active_object
    cube.name = name
    cube.data.name = name
    for material in materials:
        cube.data.materials.append(material)
    return cube


class MapTest(CleanupTest):
    # Saves the current scene as a .blend source and cleans it. Returns the result.
    def clean_scene(self, **overrides):
        source = self.path("in", "model.blend")
        os.makedirs(os.path.dirname(source), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=source, compress=False)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return self.cleanup(source, "blend", **overrides)

    # Starts an empty scene.
    def empty_scene(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)

    # Returns the material_maps entry of a material by name.
    def entry(self, result, name):
        return next(e for e in result["output"]["material_maps"] if e["name"] == name)

    # Returns the absolute path of a file of the item folder.
    def item(self, relative):
        return self.path("item", *relative.split("/"))

    # Returns the unique image files below a folder as paths relative to it.
    def files_under(self, relative):
        base = self.item(relative)
        found = []
        for root, _dirs, files in os.walk(base):
            found.extend(os.path.relpath(os.path.join(root, f), self.path("item")).replace("\\", "/") for f in files)
        return sorted(found)


class SampleTests(MapTest):
    # Each sample cleans with maps for every material, a valid result and four required maps per material.
    def test_samples(self):
        for name, count in SAMPLE_COUNTS.items():
            with self.subTest(name=name):
                folder = os.path.join(helpers.REPO_FOLDER, "samples", "models", name)
                target = self.path("in", name)
                shutil.copytree(folder, target)
                result = self.run_task("cleanup_asset", self.cleanup_params(
                    os.path.join(target, name + ".obj"), "obj", asset_id=name, output_folder=self.path("item_" + name)))
                self.assertTrue(result["ok"], result["error"])
                self.assert_result_valid(result)
                output = result["output"]
                self.assertEqual([m["name"] for m in output["material_maps"]], output["materials"])
                self.assertEqual(len(output["material_maps"]), count)
                for material in output["material_maps"]:
                    for channel in ("base_color", "normal", "roughness", "metallic"):
                        entry = material["maps"][channel]
                        self.assertNotEqual(entry["from"], "none")
                        self.assertTrue(os.path.isfile(self.path("item_" + name, *entry["file"].split("/"))))
                    self.assertEqual(list(material["maps"]), list(SIX))
                self.assertNotIn("material_approximated", self.codes(result))

    # The chair's wood image is copied byte for byte into its map folder.
    def test_image_copied_as_it_is(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        entry = self.entry(result, "Wood")["maps"]["base_color"]
        self.assertEqual(entry["from"], "file")
        self.assertEqual(entry["file"], "textures/maps/wood/base_color.png")
        with open(os.path.join(chair, "chair_wood.png"), "rb") as source, open(self.item(entry["file"]), "rb") as copy:
            self.assertEqual(source.read(), copy.read())

    # The scene saved in asset.blend has only image nodes pointing into textures/maps/, no preview paths.
    def test_reopened_blend_has_image_nodes_only(self):
        chair = self.copy_chair()
        self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        self.open_asset()
        for material in bpy.data.materials:
            kinds = {n.bl_idname for n in material.node_tree.nodes}
            self.assertLessEqual(kinds, {"ShaderNodeOutputMaterial", "ShaderNodeBsdfPrincipled", "ShaderNodeTexImage",
                                         "ShaderNodeNormalMap"}, material.name)
            for n in material.node_tree.nodes:
                if n.bl_idname == "ShaderNodeTexImage":
                    path = n.image.filepath.replace("\\", "/")
                    self.assertTrue(path.startswith("//textures/maps/"), path)
                    self.assertIsNone(n.image.packed_file)
        self.assertIn("//textures/maps/wood/base_color.png", {i.filepath.replace("\\", "/") for i in bpy.data.images})

    # The cleanup never calls the bake operator.
    def test_no_bake_operator(self):
        folder = os.path.join(helpers.BLENDER_FOLDER, "plynth")
        for root, _dirs, files in os.walk(folder):
            for name in files:
                if name.endswith(".py"):
                    with open(os.path.join(root, name), "r", encoding="utf-8") as handle:
                        self.assertNotIn("ops.object.bake", handle.read(), name)


class ValueTests(MapTest):
    # Metallic 1 gives a constant metallic map, alpha 0.3 an opacity map, and nothing see-through none.
    def test_values(self):
        self.empty_scene()
        steel = plain_material("Steel", metallic=1.0)
        glass = plain_material("Glass")
        glass.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.3
        add_cube("a", [steel], 0.0)
        add_cube("b", [glass], 1.0)
        result = self.clean_scene()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        metallic = self.entry(result, "Steel")["maps"]["metallic"]
        self.assertEqual((metallic["from"], metallic["detail"]), ("value", "1"))
        self.assertEqual(self.entry(result, "Steel")["maps"]["opacity"]["from"], "none")
        self.assertTrue(np.all(read_pixels(self.item(metallic["file"]))[:, :, 0] > 0.99))
        opacity = self.entry(result, "Glass")["maps"]["opacity"]
        self.assertEqual((opacity["from"], opacity["detail"]), ("value", "0.3"))
        self.assertAlmostEqual(float(read_pixels(self.item(opacity["file"]))[0, 0, 0]), 0.3, delta=0.01)
        self.assertEqual(self.codes(result), [])

    # Two materials that use one source image each get their own base color file.
    def test_shared_image_gets_two_files(self):
        self.empty_scene()
        os.makedirs(self.path("in"), exist_ok=True)
        picture = self.path("in", "shared.png")
        write_noise_png(picture, 16)
        made = []
        for name in ("One", "Two"):
            material, principled = new_material(name)
            texture = image_node(material.node_tree, picture, "sRGB")
            material.node_tree.links.new(texture.outputs["Color"], principled.inputs["Base Color"])
            made.append(material)
        add_cube("a", [made[0]], 0.0)
        add_cube("b", [made[1]], 1.0)
        result = self.clean_scene()
        files = [self.entry(result, n)["maps"]["base_color"]["file"] for n in ("One", "Two")]
        self.assertEqual(files, ["textures/maps/one/base_color.png", "textures/maps/two/base_color.png"])
        for file in files:
            with open(picture, "rb") as source, open(self.item(file), "rb") as copy:
                self.assertEqual(source.read(), copy.read())

    # A key that two names share is made unique.
    def test_keys_are_unique(self):
        used = set()
        self.assertEqual(maps_step.unique_key("Red Wood", used), "red_wood")
        self.assertEqual(maps_step.unique_key("red-wood", used), "red_wood_2")
        self.assertEqual(maps_step.unique_key("3 Legs", used), "a_3_legs")
        self.assertEqual(maps_step.unique_key("???", used), "asset")


class ApproximationTests(MapTest):
    # A noise texture on object coordinates is baked over the UV square and reported as approximated.
    def test_object_coordinates(self):
        self.empty_scene()
        material, principled = new_material("Rock")
        tree = material.node_tree
        coords = node(tree, "ShaderNodeTexCoord")
        noise = node(tree, "ShaderNodeTexNoise")
        tree.links.new(coords.outputs["Object"], noise.inputs["Vector"])
        tree.links.new(noise.outputs["Color"], principled.inputs["Base Color"])
        add_cube("a", [material])
        result = self.clean_scene()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(self.codes(result), ["material_approximated"])
        self.assertEqual(self.entry(result, "Rock")["maps"]["base_color"]["from"], "baked")

    # A noise texture over UV coordinates is not approximated.
    def test_uv_noise_is_exact(self):
        self.empty_scene()
        material, principled = new_material("Rock")
        tree = material.node_tree
        uv = node(tree, "ShaderNodeUVMap", uv_map="UVMap")
        noise = node(tree, "ShaderNodeTexNoise")
        tree.links.new(uv.outputs["UV"], noise.inputs["Vector"])
        tree.links.new(noise.outputs["Color"], principled.inputs["Base Color"])
        add_cube("a", [material])
        result = self.clean_scene()
        self.assertEqual(self.codes(result), [])
        self.assertEqual(self.entry(result, "Rock")["maps"]["base_color"]["from"], "baked")


class PreviewTests(MapTest):
    # A 2048 px base color gets a 1024 px copy under textures/preview/. asset.fbx names it, asset.blend keeps the
    # full map.
    def test_large_map_gets_preview_copy(self):
        self.empty_scene()
        os.makedirs(self.path("in"), exist_ok=True)
        picture = self.path("in", "big.png")
        write_png(picture, np.random.RandomState(2).randint(0, 256, (2048, 2048, 3)).astype(np.uint8))
        material, principled = new_material("Big")
        texture = image_node(material.node_tree, picture, "sRGB")
        material.node_tree.links.new(texture.outputs["Color"], principled.inputs["Base Color"])
        add_cube("a", [material])
        result = self.clean_scene()
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        entry = self.entry(result, "Big")
        self.assertEqual(entry["maps"]["base_color"]["file"], "textures/maps/big/base_color.png")
        self.assertEqual(entry["preview_maps"]["base_color"]["file"], "textures/preview/big_base_color.png")
        self.assertEqual(read_size(self.item("textures/maps/big/base_color.png")), (2048, 2048))
        self.assertEqual(read_size(self.item("textures/preview/big_base_color.png")), (1024, 1024))
        for channel in ("normal", "roughness", "metallic"):
            self.assertEqual(entry["preview_maps"][channel], entry["maps"][channel])
        self.assertEqual(self.files_under("textures/preview"), ["textures/preview/big_base_color.png"])
        with open(self.item("asset.fbx"), "rb") as handle:
            fbx = handle.read()
        self.assertIn(b"big_base_color.png", fbx)
        self.assertNotIn(b"maps\\big\\base_color.png", fbx.replace(b"/", b"\\"))
        self.open_asset()
        paths = {i.filepath.replace("\\", "/") for i in bpy.data.images}
        self.assertIn("//textures/maps/big/base_color.png", paths)
        self.assertFalse(any("preview" in p for p in paths))

    # Small maps get no preview copy and the preview entries are the same.
    def test_small_maps_have_no_copy(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        for entry in result["output"]["material_maps"]:
            self.assertEqual(entry["maps"], entry["preview_maps"])
        self.assertFalse(os.path.exists(self.item("textures/preview")))


class CheckB2(MapTest):
    # Renders the object with its own Principled material, the node output shown as emission, from the UV square's
    # position in a different place and size, and returns the pixels.
    def reference(self, material, size):
        scene = bpy.context.scene
        mesh = bpy.data.meshes.new("ref_plane")
        mesh.from_pydata([(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)], [], [(0, 1, 2, 3)])
        layer = mesh.uv_layers.new(name="UVMap")
        for loop, uv in zip(mesh.polygons[0].loop_indices, [(0, 0), (1, 0), (1, 1), (0, 1)]):
            layer.data[loop].uv = uv
        copy = material.copy()
        tree = copy.node_tree
        principled = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeBsdfPrincipled")
        source = principled.inputs["Base Color"].links[0].from_socket
        tree.links.new(source, principled.inputs["Emission Color"])
        principled.inputs["Emission Strength"].default_value = 1.0
        principled.inputs["Base Color"].default_value = (0, 0, 0, 1)
        principled.inputs["Roughness"].default_value = 1.0
        principled.inputs["Specular IOR Level"].default_value = 0.0
        mesh.materials.append(copy)
        plane = bpy.data.objects.new("ref_plane", mesh)
        plane.location = (4.0, -3.0, 0.0)
        scene.collection.objects.link(plane)
        data = bpy.data.cameras.new("ref_camera")
        data.type = "ORTHO"
        data.ortho_scale = 2.0
        camera = bpy.data.objects.new("ref_camera", data)
        camera.location = (4.0, -3.0, 2.0)
        scene.collection.objects.link(camera)
        scene.camera = camera
        target = self.path("work", "reference.png")
        thumbnail.configure_output(size, target)
        scene.view_settings.view_transform = "Standard"
        scene.render.filter_size = 0.0
        world = bpy.data.worlds.new("ref_world")
        world.color = (0, 0, 0)
        if world.node_tree is not None:
            for background in (n for n in world.node_tree.nodes if n.bl_idname == "ShaderNodeBackground"):
                background.inputs["Color"].default_value = (0, 0, 0, 1)
        scene.world = world
        thumbnail.select_eevee()
        thumbnail.try_set(scene.eevee, "taa_render_samples", 1)
        bpy.ops.render.render(write_still=True)
        return read_pixels(target)[:, :, :3]

    # A Noise texture over UV coordinates, rendered over the UV square, matches a direct render of an object with the
    # same material within 3 of 255 on most pixels.
    def test_uv_square_render_matches_direct_render(self):
        self.empty_scene()
        size = 256
        material, principled = new_material("Noise")
        tree = material.node_tree
        uv = node(tree, "ShaderNodeUVMap", uv_map="UVMap")
        noise = node(tree, "ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = 6.0
        noise.inputs["Detail"].default_value = 3.0
        tree.links.new(uv.outputs["UV"], noise.inputs["Vector"])
        tree.links.new(noise.outputs["Color"], principled.inputs["Base Color"])
        reference = self.reference(material, size)
        registry.discover(bakers)
        ctx = type("Ctx", (), {"params": {"bake_size": size}})()
        plan = maps_step.blend.plan_for(ctx, "base_color", principled.inputs["Base Color"], "Noise", "")
        self.assertEqual(plan.kind, "node")
        os.makedirs(self.path("work", "maps"), exist_ok=True)
        entry = maps_step.bake_plan(ctx, plan, self.path("work", "maps"))
        baked = to_bytes(read_pixels(self.path("work", "maps", os.path.basename(entry["file"])))[:, :, :3])
        direct = to_bytes(reference)
        difference = np.abs(baked.astype(int) - direct.astype(int)).max(axis=2)
        share = float((difference <= 3).mean())
        print("CHECK B2: share of pixels within 3/255 = %.4f, largest difference %d" % (share, difference.max()))
        self.assertGreater(float(baked.std()), 8.0, "the baked noise is not flat")
        self.assertGreaterEqual(share, 0.95)


if __name__ == "__main__":
    unittest.main()
