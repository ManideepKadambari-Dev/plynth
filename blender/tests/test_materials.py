import json
import os
import unittest

import numpy as np

import helpers  # noqa: F401
from material_helpers import (BACKGROUND, CHANNELS, SAMPLES, MaterialTest, flat_image, gradient_image,
                              noise_image, tileable_image)
from plynth import registry
from plynth.materials import bakers, checks, names, sources, write_png

SAMPLE_IDS = ["white_paint", "red_brick", "oak_planks", "grey_tile", "painted_wood", "brushed_steel", "clear_glass",
              "roof_tiles", "concrete"]


class RegistryTests(unittest.TestCase):
    # The sources, bakers and checks are registry entries found by discovery.
    def test_entries_are_registered(self):
        for package in (sources, bakers, checks):
            registry.discover(package)
        self.assertIn("texture_set", registry.MATERIAL_SOURCES.ids())
        self.assertIn("direct", registry.BAKERS.ids())
        for check in ("map_sizes", "seams", "resolution"):
            self.assertIn(check, registry.MATERIAL_CHECKS.ids())
        self.assertLess(registry.BAKERS.get("direct").order, 20)


class NameTests(unittest.TestCase):
    # Names of every style land on their channel, and the convention is read from the name.
    def test_classify(self):
        cases = {
            "brick_color.jpg": ("base_color", None), "Wood_BaseColor.png": ("base_color", None),
            "stone_albedo_2k.png": ("base_color", None), "metal_plate_diff.png": ("base_color", None),
            "brick_nor_gl.png": ("normal", "opengl"), "NormalDX.png": ("normal", "directx"),
            "x_Normal_DirectX.png": ("normal", "directx"), "oak_Roughness.png": ("roughness", None),
            "tile_gloss.png": ("gloss", None), "steel_metalness.png": ("metallic", None),
            "leaf_opacity.png": ("opacity", None), "tile_AmbientOcclusion.png": ("occlusion", None),
            "rock_ao.png": ("occlusion", None), "rock_ARM.png": ("packed", None), "rock_orm.png": ("packed", None),
            "rock_height.png": ("ignored", None), "thumb.png": ("ignored", None), "notes.png": (None, None),
        }
        for name, expected in cases.items():
            self.assertEqual(names.classify(name), expected, name)

    # The largest image wins when several fit one channel.
    def test_match_takes_the_largest(self):
        found = names.match(["a_col.png", "b_col.png", "n_nor.png"], rank=lambda path: {"a_col.png": 1}.get(path, 5))
        self.assertEqual(found, {"base_color": "b_col.png", "normal": "n_nor.png"})


class SampleTests(MaterialTest):
    # Each sample cleans with no error, no warning and a result that matches the schema.
    def test_every_sample_cleans(self):
        for material_id in SAMPLE_IDS:
            with self.subTest(material_id):
                result = self.clean_ok(os.path.join(SAMPLES, material_id), output_folder=self.path("out", material_id))
                self.assertEqual(result["warnings"], [], material_id)
                output = result["output"]
                self.assertEqual(output["source_info"]["id"], material_id)
                self.assertEqual(output["source_info"]["license"], "cc0")
                for channel in ("base_color", "normal", "roughness", "metallic"):
                    self.assertTrue(os.path.isfile(self.map_path(result, channel)), channel)

    # The tile sizes of material.json arrive in the result.
    def test_tile_sizes(self):
        expected = {"red_brick": (1.2, 0.6), "oak_planks": (2.0, 2.0), "grey_tile": (0.6, 0.6),
                    "brushed_steel": (0.5, 0.5), "roof_tiles": (1.0, 1.0), "concrete": (2.0, 2.0)}
        for material_id, size in expected.items():
            with self.subTest(material_id):
                result = self.clean_ok(os.path.join(SAMPLES, material_id), output_folder=self.path("out", material_id))
                tile = result["output"]["tile_size"]
                self.assertEqual((tile["width"], tile["height"]), size)

    # The job's tile size beats material.json's.
    def test_job_tile_size_wins(self):
        result = self.clean_ok(os.path.join(SAMPLES, "red_brick"), tile_size={"width": 3.0, "height": 1.5})
        self.assertEqual(result["output"]["tile_size"], {"width": 3.0, "height": 1.5})

    # A png normal is copied byte for byte, and so is a jpg base color.
    def test_files_are_copied_as_they_are(self):
        result = self.clean_ok(os.path.join(SAMPLES, "red_brick"))
        maps = result["output"]["maps"]
        self.assertEqual(maps["normal"], {"file": "textures/normal.png", "from": "file", "detail": "brick_nor_gl.png"})
        self.assertEqual(maps["base_color"]["file"], "textures/base_color.jpg")
        self.assertEqual(self.read_bytes(self.map_path(result, "normal")),
                         self.read_bytes(os.path.join(SAMPLES, "red_brick", "brick_nor_gl.png")))
        self.assertEqual(self.read_bytes(self.map_path(result, "base_color")),
                         self.read_bytes(os.path.join(SAMPLES, "red_brick", "brick_color.jpg")))
        self.assertEqual(maps["roughness"]["detail"], "brick_rough.png")

    # A DirectX normal map has its green channel flipped, so a pixel of 0.25 green reads 0.75.
    def test_directx_normal_is_flipped(self):
        image = flat_image((100, 64, 255))
        folder = self.make_set("dx", {"stone_col.png": noise_image(32), "stone_NormalDX.png": image})
        result = self.clean_ok(folder)
        pixel = self.pixels(self.map_path(result, "normal"))[5, 7]
        self.assertEqual(tuple(pixel[:3]), (100, 191, 255))
        self.assertAlmostEqual(pixel[1] / 255.0, 0.75, delta=0.01)
        self.assertEqual(result["output"]["maps"]["normal"]["from"], "file")

    # The oak sample's DirectX normal is flipped against its source file.
    def test_oak_normal_flipped(self):
        result = self.clean_ok(os.path.join(SAMPLES, "oak_planks"))
        source = self.pixels(os.path.join(SAMPLES, "oak_planks", "NormalDX.png"))
        made = self.pixels(self.map_path(result, "normal"))
        self.assertTrue(np.array_equal(made[:, :, 1], 255 - source[:, :, 1]))
        self.assertTrue(np.array_equal(made[:, :, 0], source[:, :, 0]))

    # The normal option decides when the name does not say.
    def test_normal_option(self):
        folder = self.make_set("opt", {"stone_col.png": noise_image(32), "stone_n.png": flat_image((9, 64, 255))})
        result = self.clean_ok(folder, maps={"normal": os.path.join(folder, "stone_n.png").replace("\\", "/")},
                               normal="directx")
        self.assertEqual(self.pixels(self.map_path(result, "normal"))[0, 0][1], 191)

    # white_paint's roughness is a 4 pixel image of its value, and metallic and normal are constants too.
    def test_values_become_constant_images(self):
        result = self.clean_ok(os.path.join(SAMPLES, "white_paint"))
        maps = result["output"]["maps"]
        self.assertEqual(maps["roughness"], {"file": "textures/roughness.png", "from": "value", "detail": "0.9"})
        made = self.pixels(self.map_path(result, "roughness"))
        self.assertEqual(made.shape[:2], (4, 4))
        self.assertTrue(np.all(made[:, :, :3] == 230))
        self.assertEqual(maps["normal"]["detail"], "flat")
        self.assertEqual(tuple(self.pixels(self.map_path(result, "normal"))[0, 0][:3]), (128, 128, 255))
        self.assertEqual(maps["metallic"]["from"], "value")
        self.assertEqual(result["output"]["texture_size"], {"width": 512, "height": 512})

    # clear_glass has an opacity map and no image at all; brushed_steel has a roughness image and no opacity.
    def test_glass_and_steel(self):
        glass = self.clean_ok(os.path.join(SAMPLES, "clear_glass"))
        maps = glass["output"]["maps"]
        self.assertEqual(maps["opacity"], {"file": "textures/opacity.png", "from": "value", "detail": "0.2"})
        self.assertTrue(np.all(self.pixels(self.map_path(glass, "opacity"))[:, :, :3] == 51))
        self.assertEqual(maps["base_color"]["from"], "value")
        self.assertEqual(tuple(self.pixels(self.map_path(glass, "base_color"))[0, 0][:3]), (209, 230, 237))
        steel = self.clean_ok(os.path.join(SAMPLES, "brushed_steel"), output_folder=self.path("steel"))
        maps = steel["output"]["maps"]
        self.assertEqual(maps["opacity"], {"file": None, "from": "none", "detail": ""})
        self.assertEqual(maps["roughness"]["from"], "file")
        self.assertEqual(maps["metallic"], {"file": "textures/metallic.png", "from": "value", "detail": "1"})
        self.assertEqual(maps["occlusion"]["from"], "none")

    # grey_tile keeps its occlusion image.
    def test_occlusion_image(self):
        result = self.clean_ok(os.path.join(SAMPLES, "grey_tile"))
        self.assertEqual(result["output"]["maps"]["occlusion"]["detail"], "grey_tile_AmbientOcclusion.png")

    # A zip of red_brick gives the same maps as the folder, with or without a folder level inside.
    def test_zip_gives_the_same_maps(self):
        folder_result = self.clean_ok(os.path.join(SAMPLES, "red_brick"), output_folder=self.path("from_folder"))
        for prefix in ("", "red_brick/"):
            zip_file = self.zip_folder(os.path.join(SAMPLES, "red_brick"), "brick%s.zip" % len(prefix), prefix)
            target = self.path("from_zip_%d" % len(prefix))
            result = self.clean_ok(zip_file, output_folder=target)
            self.assertEqual(result["output"]["maps"], folder_result["output"]["maps"])
            self.assertEqual(result["output"]["source_info"], folder_result["output"]["source_info"])
            for channel in ("base_color", "normal", "roughness"):
                relative = result["output"]["maps"][channel]["file"].split("/")
                self.assertEqual(self.read_bytes(os.path.join(target, *relative)),
                                 self.read_bytes(os.path.join(self.path("from_folder"), *relative)))
        self.assertFalse(os.path.isdir(self.path("work", "unpacked_material")))

    # A cleanup never writes into the source folder.
    def test_sample_folders_are_untouched(self):
        before = sorted(os.listdir(os.path.join(SAMPLES, "red_brick")))
        self.clean_ok(os.path.join(SAMPLES, "red_brick"))
        self.assertEqual(sorted(os.listdir(os.path.join(SAMPLES, "red_brick"))), before)


class SourceTests(MaterialTest):
    # One image file alone is a texture set with that base color, whatever its name.
    def test_single_image(self):
        folder = self.make_set("one", {"granite.png": noise_image(32, 4)})
        result = self.clean_ok(os.path.join(folder, "granite.png"), tile_size=None)
        maps = result["output"]["maps"]
        self.assertEqual(maps["base_color"]["detail"], "granite.png")
        self.assertEqual(self.read_bytes(self.map_path(result, "base_color")),
                         self.read_bytes(os.path.join(folder, "granite.png")))
        self.assertEqual(maps["roughness"], {"file": "textures/roughness.png", "from": "value", "detail": "0.5"})
        self.assertIsNone(result["output"]["source_info"])
        self.assertEqual(result["output"]["tile_size"], {"width": 1.0, "height": 1.0})

    # An image of another file type is written as png.
    def test_other_image_types_become_png(self):
        folder = self.make_set("bmp", {})
        import bpy
        image = bpy.data.images.new("t", 16, 16, alpha=False)
        image.filepath_raw = os.path.join(folder, "wall_col.bmp")
        image.file_format = "BMP"
        image.save()
        bpy.data.images.remove(image)
        result = self.clean_ok(folder)
        self.assertEqual(result["output"]["maps"]["base_color"]["file"], "textures/base_color.png")

    # --map choices beat the names, and values beat material.json.
    def test_user_choices_win(self):
        folder = self.make_set("pick", {"a_col.png": noise_image(64, 1), "b_col.png": noise_image(32, 2),
                                        "a_rough.png": flat_image((10, 10, 10))})
        self.write_sidecar(folder, values={"roughness": 0.9, "metallic": 0.4})
        chosen = os.path.join(folder, "b_col.png").replace("\\", "/")
        result = self.clean_ok(folder, maps={"base_color": chosen}, values={"roughness": 0.3})
        maps = result["output"]["maps"]
        self.assertEqual(maps["base_color"]["detail"], "b_col.png")
        self.assertNotIn("ambiguous_map", self.codes(result))
        self.assertEqual(maps["roughness"], {"file": "textures/roughness.png", "from": "value", "detail": "0.3"})
        self.assertEqual(maps["metallic"]["detail"], "0.4")
        self.assertEqual(result["output"]["texture_size"], {"width": 32, "height": 32})

    # material.json values beat file names, and its maps beat names too.
    def test_sidecar_beats_names(self):
        folder = self.make_set("side", {"x_col.png": noise_image(32), "plain.png": flat_image((9, 9, 9)),
                                        "x_rough.png": flat_image((10, 10, 10))})
        self.write_sidecar(folder, maps={"roughness": "plain.png"}, base_color_value=[0.1, 0.2, 0.3])
        result = self.clean_ok(folder)
        self.assertEqual(result["output"]["maps"]["roughness"]["detail"], "plain.png")
        self.assertEqual(result["output"]["maps"]["base_color"]["detail"], "x_col.png")

    # Two base color candidates give ambiguous_map naming the one used, the larger.
    def test_ambiguous_map(self):
        folder = self.make_set("amb", {"a_col.png": noise_image(32, 1), "b_diffuse.png": noise_image(64, 2)})
        result = self.clean_ok(folder)
        self.assertEqual(self.codes(result), ["ambiguous_map"])
        message = result["warnings"][0]["message"]
        self.assertIn("b_diffuse.png", message)
        self.assertEqual(result["output"]["maps"]["base_color"]["detail"], "b_diffuse.png")

    # A stray notes.png gives unused_image, but a height map is quietly left out.
    def test_unused_image(self):
        folder = self.make_set("stray", {"x_col.png": noise_image(32), "notes.png": noise_image(32, 9),
                                         "x_height.png": noise_image(32, 8)})
        result = self.clean_ok(folder)
        self.assertEqual(self.codes(result), ["unused_image"])
        self.assertIn("notes.png", result["warnings"][0]["message"])

    # A refresh-style job on a cleaned item folder, with thumb.png and info.json beside the textures and the
    # current maps named in the job, gives no unused_image warning.
    def test_refresh_of_cleaned_folder(self):
        first = self.clean_ok(self.copy_sample("red_brick"), output_folder=self.path("item"))
        item = self.path("item")
        with open(os.path.join(item, "info.json"), "w", encoding="utf-8") as handle:
            handle.write("{}")
        write_png(os.path.join(item, "thumb.png"), noise_image(16, 3))
        maps = {c: self.map_path(first, c).replace("\\", "/") for c, m in first["output"]["maps"].items()
                if m["file"] and m["from"] != "value"}
        result = self.clean_ok(item, maps=maps, output_folder=self.path("item2"))
        self.assertNotIn("unused_image", self.codes(result))
        self.assertIsNone(result["output"]["source_info"])

    # A set with no base color is no_base_color.
    def test_no_base_color(self):
        folder = self.make_set("nobase", {"x_roughness.png": noise_image(32), "x_nor.png": noise_image(32)})
        result = self.clean(folder)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "no_base_color")
        self.assertIsNone(result["output"])
        self.assert_result_valid(result)

    # A folder without images or material.json is import_failed, and so are a missing path and a text file.
    def test_nothing_to_read(self):
        empty = self.make_set("empty", {})
        self.assertEqual(self.clean(empty)["error"]["code"], "import_failed")
        self.assertEqual(self.clean(self.path("in", "missing"))["error"]["code"], "import_failed")
        text = self.make_set("text", {"readme.txt": "hello"})
        self.assertEqual(self.clean(os.path.join(text, "readme.txt"))["error"]["code"], "import_failed")

    # A zip that is not a zip is unpack_failed.
    def test_bad_zip(self):
        folder = self.make_set("badzip", {"broken.zip": b"not a zip"})
        result = self.clean(os.path.join(folder, "broken.zip"))
        self.assertEqual(result["error"]["code"], "unpack_failed")

    # A broken material.json is import_failed with its name in the message.
    def test_bad_sidecar(self):
        folder = self.make_set("badside", {"x_col.png": noise_image(32)})
        self.write_sidecar(folder, values={"roughness": 7})
        result = self.clean(folder)
        self.assertEqual(result["error"]["code"], "import_failed")
        self.assertIn("material.json", result["error"]["message"])

    # A kind of source nothing is installed for is refused, and a bad job is bad_job.
    def test_unknown_source_and_bad_job(self):
        folder = self.make_set("kind", {"x_col.png": noise_image(32)})
        self.assertEqual(self.clean(folder, kind="fab_zip", check_job=False)["error"]["code"], "unsupported_format")
        params = self.clean_params(folder)
        del params["output_folder"]
        self.assertEqual(self.run_task("cleanup_material", params)["error"]["code"], "bad_job")

    # Gloss is inverted into roughness.
    def test_glossiness_is_inverted(self):
        folder = self.make_set("gloss", {"x_col.png": noise_image(32), "x_gloss.png": flat_image((200, 200, 200))})
        result = self.clean_ok(folder)
        self.assertEqual(result["output"]["maps"]["roughness"]["detail"], "x_gloss.png")
        self.assertTrue(np.all(self.pixels(self.map_path(result, "roughness"))[:, :, :3] == 55))

    # A packed ARM image is split into occlusion, roughness and metallic.
    def test_packed_image(self):
        packed = np.zeros((16, 16, 3), dtype=np.uint8)
        packed[:, :] = (255, 100, 30)
        folder = self.make_set("arm", {"x_col.png": noise_image(16), "x_arm.png": packed})
        result = self.clean_ok(folder)
        maps = result["output"]["maps"]
        for channel, value in (("occlusion", 255), ("roughness", 100), ("metallic", 30)):
            self.assertEqual(maps[channel]["detail"], "x_arm.png")
            self.assertTrue(np.all(self.pixels(self.map_path(result, channel))[:, :, :3] == value), channel)

    # The alpha of a base color becomes the opacity map, and an opaque base color gives none.
    def test_alpha_becomes_opacity(self):
        folder = self.make_set("alpha", {})
        rgba = np.zeros((16, 16, 4), dtype=np.uint8)
        rgba[:, :, 3] = 77
        write_png(os.path.join(folder, "leaf_col.png"), rgba)
        result = self.clean_ok(folder)
        self.assertEqual(result["output"]["maps"]["opacity"]["detail"], "leaf_col.png")
        self.assertTrue(np.all(self.pixels(self.map_path(result, "opacity"))[:, :, 0] == 77))
        opaque = self.make_set("opaque", {"leaf_col.png": noise_image(16)})
        self.assertEqual(self.clean_ok(opaque, output_folder=self.path("o2"))["output"]["maps"]["opacity"]["from"], "none")

    # An opacity image that is all 1 gives no opacity map, and an opacity value of 1 neither.
    def test_full_opacity_gives_no_map(self):
        folder = self.make_set("full", {"x_col.png": noise_image(16), "x_opacity.png": flat_image((255, 255, 255))})
        self.assertEqual(self.clean_ok(folder)["output"]["maps"]["opacity"]["from"], "none")
        result = self.clean_ok(folder, values={"opacity": 1.0}, output_folder=self.path("o2"))
        self.assertEqual(result["output"]["maps"]["opacity"]["from"], "none")


class CheckTests(MaterialTest):
    # Maps of different sizes give map_size_mismatch.
    def test_map_size_mismatch(self):
        folder = self.make_set("sizes", {"x_col.png": tileable_image(64), "x_rough.png": flat_image((9, 9, 9), 32)})
        result = self.clean_ok(folder, tile_size={"width": 0.1, "height": 0.1})
        self.assertEqual(self.codes(result), ["map_size_mismatch"])
        self.assertIn("x_rough.png", result["warnings"][0]["message"])

    # A base color whose left and right columns differ strongly gives visible_seams; a seamless one does not.
    def test_visible_seams(self):
        folder = self.make_set("seam", {"x_col.png": gradient_image(64)})
        result = self.clean_ok(folder, tile_size={"width": 0.1, "height": 0.1})
        self.assertEqual(self.codes(result), ["visible_seams"])
        good = self.make_set("good", {"x_col.png": tileable_image(64)})
        result = self.clean_ok(good, tile_size={"width": 0.1, "height": 0.1}, output_folder=self.path("o2"))
        self.assertEqual(result["warnings"], [])

    # A 128 pixel base color on a 2 m tile gives low_resolution, and on a 0.4 m tile it does not.
    def test_low_resolution(self):
        folder = self.make_set("res", {"x_col.png": tileable_image(128)})
        result = self.clean_ok(folder, tile_size={"width": 2.0, "height": 2.0})
        self.assertEqual(self.codes(result), ["low_resolution"])
        result = self.clean_ok(folder, tile_size={"width": 0.4, "height": 0.4}, output_folder=self.path("o2"))
        self.assertEqual(result["warnings"], [])


class ThumbnailTests(MaterialTest):
    # Returns the 8 bit RGB of a pixel of a picture.
    def pixel(self, picture, x, y):
        return tuple(int(v) for v in picture[y, x][:3])

    # The thumbnail is 512 px with the background in the corners, the sphere on the left and the tile on the right.
    def test_thumbnail_picture(self):
        spec = {"render": True, "size": 512, "background": BACKGROUND}
        result = self.clean_ok(os.path.join(SAMPLES, "red_brick"), thumbnail=spec)
        thumb = result["output"]["thumbnail"]
        self.assertEqual(thumb, {"file": "thumb.png", "engine": "eevee", "error": None})
        self.assertIn("rendered with Eevee in", self.printed)
        self.assertEqual(result["warnings"], [])
        picture = self.pixels(self.path("out", "thumb.png"))
        self.assertEqual(picture.shape[:2], (512, 512))
        wanted = [round(c * 255) for c in BACKGROUND]
        for x, y in ((0, 0), (511, 0), (0, 511), (511, 511), (256, 256)):
            for channel in range(3):
                self.assertLessEqual(abs(int(picture[y, x][channel]) - wanted[channel]), 2, (x, y))
        for x in (128, 384):
            centre = self.pixel(picture, x, 256)
            self.assertGreater(sum(abs(centre[c] - wanted[c]) for c in range(3)), 60, x)
        left = np.abs(picture[:, :256, :3].astype(int) - np.array(wanted)).sum()
        right = np.abs(picture[:, 256:, :3].astype(int) - np.array(wanted)).sum()
        self.assertGreater(left, 0)
        self.assertGreater(right, 0)

    # A see through material still renders.
    def test_glass_thumbnail(self):
        spec = {"render": True, "size": 128, "background": BACKGROUND}
        result = self.clean_ok(os.path.join(SAMPLES, "clear_glass"), thumbnail=spec)
        self.assertEqual(result["output"]["thumbnail"]["file"], "thumb.png")

    # The glass tile shows the checker panel behind it: its pixels vary a lot, where an opaque flat tile is smooth.
    def test_glass_thumbnail_shows_backdrop(self):
        spec = {"render": True, "size": 256, "background": BACKGROUND}
        glass = self.clean_ok(os.path.join(SAMPLES, "clear_glass"), thumbnail=spec)
        glass_picture = self.pixels(os.path.join(self.out_folder, "thumb.png")).astype(float)
        paint = self.clean_ok(os.path.join(SAMPLES, "white_paint"), thumbnail=spec, output_folder=self.path("out2"))
        paint_picture = self.pixels(os.path.join(self.path("out2"), "thumb.png")).astype(float)
        self.assertEqual(glass["output"]["thumbnail"]["file"], "thumb.png")
        tile = (slice(90, 160), slice(160, 225))
        self.assertGreater(glass_picture[tile][:, :, :3].std(), 12.0)
        self.assertLess(paint_picture[tile][:, :, :3].std() * 2.0, glass_picture[tile][:, :, :3].std())

    # The 1 m flat tile shows 1 m of real surface: a 2 m tile gives half a repeat, a 0.5 m tile gives two.
    def test_thumbnail_uv_scale(self):
        import bpy
        from plynth.materials import thumbnail as material_thumbnail
        material = bpy.data.materials.new("uv_check")
        for size, repeats in ((2.0, 0.5), (0.5, 2.0), (1.0, 1.0)):
            tile_size = {"width": size, "height": size}
            tile = material_thumbnail.make_tile(material, tile_size)
            sphere = material_thumbnail.make_sphere(material, tile_size)
            try:
                uvs = [tile.data.uv_layers.active.data[i].uv for i in range(4)]
                self.assertAlmostEqual(max(uv[0] for uv in uvs) - min(uv[0] for uv in uvs), repeats)
                self.assertAlmostEqual(max(uv[1] for uv in uvs) - min(uv[1] for uv in uvs), repeats)
                around = [uv.uv[0] for uv in sphere.data.uv_layers.active.data]
                self.assertAlmostEqual(max(around), round(max(around)))
                self.assertAlmostEqual(max(around), max(1, round(3.14159265 / size)))
            finally:
                bpy.data.objects.remove(tile)
                bpy.data.objects.remove(sphere)

    # render false gives no thumbnail and no error.
    def test_no_thumbnail_wanted(self):
        result = self.clean_ok(os.path.join(SAMPLES, "red_brick"))
        self.assertEqual(result["output"]["thumbnail"], {"file": None, "engine": None, "error": None})
        self.assertFalse(os.path.exists(self.path("out", "thumb.png")))

    # A broken thumbnail spec is reported in thumbnail.error and the job still succeeds with its maps.
    def test_broken_thumbnail_spec(self):
        for spec in ({"render": True, "size": "big", "background": BACKGROUND},
                     {"render": True, "size": 128, "background": [2, 0, 0]}):
            result = self.clean(os.path.join(SAMPLES, "white_paint"), check_job=False, thumbnail=spec)
            self.assertTrue(result["ok"], result["error"])
            self.assert_result_valid(result)
            thumb = result["output"]["thumbnail"]
            self.assertIsNone(thumb["file"])
            self.assertTrue(thumb["error"])
            self.assertTrue(os.path.isfile(self.map_path(result, "base_color")))

    # A map that will not load stops the job before anything is copied, naming the file.
    def test_unreadable_map(self):
        folder = self.make_set("bad", {"x_col.png": noise_image(32), "x_rough.png": b"not an image"})
        spec = {"render": True, "size": 64, "background": BACKGROUND}
        result = self.clean(folder, thumbnail=spec)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "import_failed")
        self.assertIn("x_rough.png", result["error"]["message"])


class NoOtherEngine(unittest.TestCase):
    # The material code never uses the engine that bakes, and never calls Blender's bake operator.
    def test_source_has_no_forbidden_words(self):
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plynth")
        word = "".join(["cy", "cles"])
        call = ".".join(["ops", "object", "bake"])
        for folder, _, files in os.walk(root):
            for name in files:
                if name.endswith(".py"):
                    with open(os.path.join(folder, name), "r", encoding="utf-8") as handle:
                        text = handle.read().lower()
                    self.assertNotIn(word, text, name)
                    self.assertNotIn(call, text, name)


if __name__ == "__main__":
    unittest.main()
