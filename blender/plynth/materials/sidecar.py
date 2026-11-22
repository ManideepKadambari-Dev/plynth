import json
import os
import re

from ..jobs import WorkerError

ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
TAG_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
MAP_CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
VALUE_CHANNELS = ("roughness", "metallic", "opacity", "occlusion")
FIELDS = ("format", "version", "id", "name", "groups", "tags", "license", "tile_size", "maps", "values",
          "base_color_value", "normal")


# Says whether value is a number from low to high.
def is_number(value, low, high):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and low <= value <= high


# Raises import_failed when the parsed material.json does not follow material-source.schema.json.
def check(data, label):
    def bad(why):
        raise WorkerError("import_failed", "%s is not valid: %s." % (label, why))

    if not isinstance(data, dict) or data.get("format") != "plynth-material-source" or data.get("version") != 1:
        bad("it is not a Plynth material source file")
    for key in data:
        if key not in FIELDS:
            bad("unknown field %s" % key)
    if "id" in data and not (isinstance(data["id"], str) and ID_PATTERN.match(data["id"])):
        bad("the id is not a lower case name")
    if "name" in data and not (isinstance(data["name"], str) and 0 < len(data["name"]) <= 120):
        bad("the name is empty or too long")
    for key, pattern in (("groups", ID_PATTERN), ("tags", TAG_PATTERN)):
        if key in data and not (isinstance(data[key], list)
                                and all(isinstance(item, str) and pattern.match(item) for item in data[key])):
            bad("%s must be a list of names" % key)
    if "license" in data and not (isinstance(data["license"], str) and ID_PATTERN.match(data["license"])):
        bad("the license is not a lower case name")
    if "tile_size" in data:
        size = data["tile_size"]
        if not (isinstance(size, dict) and is_number(size.get("width"), 0.01, 100)
                and is_number(size.get("height"), 0.01, 100)):
            bad("the tile size must be between 0.01 and 100 m")
    if "maps" in data:
        maps = data["maps"]
        if not (isinstance(maps, dict) and all(key in MAP_CHANNELS and isinstance(value, str) and value
                                               for key, value in maps.items())):
            bad("maps must name files for known channels")
    if "values" in data:
        values = data["values"]
        if not (isinstance(values, dict) and all(key in VALUE_CHANNELS and is_number(value, 0, 1)
                                                 for key, value in values.items())):
            bad("values must be numbers from 0 to 1")
    if "base_color_value" in data:
        color = data["base_color_value"]
        if not (isinstance(color, list) and len(color) == 3 and all(is_number(c, 0, 1) for c in color)):
            bad("base_color_value must be three numbers from 0 to 1")
    if "normal" in data and data["normal"] not in ("auto", "opengl", "directx"):
        bad("normal must be auto, opengl or directx")


# Reads and checks a material.json. Raises import_failed.
def read(path):
    label = os.path.basename(path)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as error:
        raise WorkerError("import_failed", "%s cannot be read: %s" % (label, error))
    check(data, label)
    return data


# Returns what material.json said as the result's source_info: only the fields that were there.
def info_of(data):
    return {key: data[key] for key in ("id", "name", "groups", "tags", "license") if key in data}
