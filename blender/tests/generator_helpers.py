import copy
import os

import bpy

import helpers
from build_helpers import BuildTest, load_params
from plynth import shading
from slot_fixtures import pascal

SAMPLES = os.path.join(helpers.REPO_FOLDER, "samples", "generators")
TEST_GENERATORS = os.path.join(helpers.BLENDER_FOLDER, "tests", "data", "generators")
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
BOX_SIZE = {"width": 1.0, "depth": 1.0, "height": 1.0}
GENERATED = "Generated"
RESERVED_GENERATORS = {"door", "frame", "glass", "handle", "threshold", "stair", "roof", "soffit", "fascia", "wall",
                       "exterior", "flat", "generated"}
RESERVED_PARAMS = {"wall", "floor", "ceiling"}


# Returns a generator item of the build job. Without slots, room_params fills them in.
def job_item(item_id, generator, params, location, rotation=0.0, size=None, wall=None, slots=None):
    item = {"id": item_id, "generator": generator, "params": params, "location": location, "rotation_z": rotation,
            "size": size or dict(BOX_SIZE), "room": None, "wall": wall}
    if slots is not None:
        item["slots"] = dict(slots)
    return item


# Returns the base slot name of a material parameter of a generator, as core names it: both ids in PascalCase,
# each cut to 24 characters, with Generator added to a generator part that is reserved or a reserved parameter.
def generator_slot_base(generator_id, param_id):
    label = pascal(generator_id)[:24]
    param = pascal(param_id)[:24]
    if label.lower() in RESERVED_GENERATORS or param.lower() in RESERVED_PARAMS:
        label += "Generator"
    return "%s_%s" % (label, param)


# Fills in the slots of every item that has none and returns the slot table rows they use: one per material
# parameter of a generator, the material added when the items of one generator show more than one material,
# and Generated when a generator has no material parameter. material_params maps a generator id to the ids of
# its material parameters. This follows core's naming for jobs without name clashes.
def fill_generator_slots(items, material_params):
    shown = {}
    for item in items:
        for param_id in material_params.get(item["generator"], []):
            base = generator_slot_base(item["generator"], param_id)
            shown.setdefault(base, set()).add(item["params"].get(param_id))
    rows = {}
    for item in items:
        params = material_params.get(item["generator"], [])
        if "slots" not in item:
            item["slots"] = {}
            for param_id in params:
                base = generator_slot_base(item["generator"], param_id)
                material = item["params"].get(param_id)
                name = base
                if len(shown[base]) > 1:
                    name = "%s_%s" % (base, pascal(material) if material else "Neutral")
                item["slots"][param_id] = name
                rows[name] = {"name": name, "material": material, "surface": GENERATED}
        if not params:
            rows[GENERATED] = {"name": GENERATED, "material": None, "surface": GENERATED}
    return [rows[name] for name in sorted(rows)]


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
    # data folder) and items on its floor. Items without slots get them, and the slot table gets their rows in
    # place of the fixture's generator rows.
    def room_params(self, files, items):
        params = copy.deepcopy(load_params("generators_room"))
        params["generators"] = []
        material_params = {}
        for generator_id, path in sorted(files.items()):
            if not os.path.isabs(path):
                path = os.path.join(TEST_GENERATORS, path + ".py")
            declaration = self.declaration_of(path)
            params["generators"].append({"id": generator_id, "file": path.replace("\\", "/"),
                                         "addons": declaration.get("addons", [])})
            material_params[generator_id] = [p["id"] for p in declaration.get("params", [])
                                             if p.get("type") == "material"]
        params["floors"][0]["generator_items"] = items
        rows = fill_generator_slots(items, material_params)
        kept = [row for row in params["slots"] if row["surface"] != GENERATED]
        params["slots"] = sorted(kept + rows, key=lambda row: row["name"])
        return params

    # Reads the declaration of a generator file, or an empty one when it cannot be read.
    def declaration_of(self, path):
        from plynth import generating
        try:
            return generating.read_declaration(path)
        except generating.GeneratorError:
            return {}

    # Reads the add-ons a generator file lists.
    def addons_of(self, path):
        return self.declaration_of(path).get("addons", [])

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
