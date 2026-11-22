import copy
import os

import bpy

import helpers
from build_helpers import BuildTest, load_params
from plynth import shading

SAMPLES = os.path.join(helpers.REPO_FOLDER, "samples", "generators")
TEST_GENERATORS = os.path.join(helpers.BLENDER_FOLDER, "tests", "data", "generators")
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
BOX_SIZE = {"width": 1.0, "depth": 1.0, "height": 1.0}


# Returns a generator item of the build job.
def job_item(item_id, generator, params, location, rotation=0.0, size=None, wall=None):
    return {"id": item_id, "generator": generator, "params": params, "location": location, "rotation_z": rotation,
            "size": size or dict(BOX_SIZE), "room": None, "wall": wall}


class GeneratorTest(BuildTest):
    # Writes the map images of a library material and returns its job entry.
    def make_material(self, material_id, tile=(2.0, 1.0)):
        folder = self.path("library", material_id)
        os.makedirs(folder, exist_ok=True)
        maps = {}
        for channel in CHANNELS:
            if channel in ("opacity", "occlusion"):
                maps[channel] = None
                continue
            path = os.path.join(folder, channel + ".png").replace("\\", "/")
            shading.write_constant_png(path, (200, 200, 200) if channel == "base_color" else (128, 128, 128))
            maps[channel] = path
        return {"id": material_id, "tile_size": {"width": tile[0], "height": tile[1]}, "maps": maps}

    # Returns the sample fixture params: the square room, a column i5 and a railing i6, with the sample files.
    def sample_params(self, tile=(2.0, 1.0)):
        params = copy.deepcopy(load_params("generators_room"))
        for entry in params["generators"]:
            entry["file"] = entry["file"].replace("@GENERATORS@", SAMPLES.replace("\\", "/"))
        params["materials"] = [self.make_material("brushed_steel", tile), self.make_material("white_paint", tile)]
        return params

    # Returns params of the empty square room with the given generator files (id to path or stem in the test
    # data folder) and items on its floor.
    def room_params(self, files, items):
        params = copy.deepcopy(load_params("generators_room"))
        params["generators"] = []
        for generator_id, path in sorted(files.items()):
            if not os.path.isabs(path):
                path = os.path.join(TEST_GENERATORS, path + ".py")
            params["generators"].append({"id": generator_id, "file": path.replace("\\", "/"),
                                         "addons": self.addons_of(path)})
        params["floors"][0]["generator_items"] = items
        return params

    # Reads the add-ons a generator file lists.
    def addons_of(self, path):
        from plynth import generating
        try:
            return generating.read_declaration(path).get("addons", [])
        except generating.GeneratorError:
            return []

    # Builds, checks the result against the schema and opens building.blend.
    def build_open(self, params, **overrides):
        result = self.build(params, **overrides)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return result

    # Returns the problems of a result with this code.
    def problems(self, result, code):
        return [p for p in result["output"]["problems"] if p["code"] == code]
