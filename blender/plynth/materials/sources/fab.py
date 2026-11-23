import json
import os
import re

from .. import ChannelPlan, Loaded, SourceEntry
from ...importers import zip as zip_importer
from ...jobs import WorkerError
from ...registry import MATERIAL_SOURCES
from ...stores import fab_materials, fab_names

PACKED_CHANNELS = (("occlusion", 0), ("roughness", 1), ("metallic", 2))
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
DEFAULT_VALUES = {"roughness": 0.5, "metallic": 0.0}
NORMAL_CHOICES = ("auto", "opengl", "directx")
SIZE = re.compile(r"(\d+(?:\.\d+)?)\s*(?:x|\*|by)\s*(\d+(?:\.\d+)?)\s*(cm|mm|m)?", re.IGNORECASE)
UNITS = {"m": 1.0, "cm": 0.01, "mm": 0.001}


# Reads source.fab.json for a material and checks the fields this source uses. Raises import_failed.
def read_source(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as error:
        raise WorkerError("import_failed", "The Fab source file cannot be read: %s" % error)
    problem = None
    if not isinstance(data, dict) or data.get("format") != "plynth-fab-source" or data.get("version") != 1:
        problem = "is not a Fab source file"
    elif data.get("kind") != "material":
        problem = "is not for a material"
    elif not isinstance(data.get("zip"), str) or not isinstance(data.get("unpack_folder"), str):
        problem = "has no zip or unpack folder"
    elif not isinstance(data.get("files"), list) or not all(isinstance(e, str) for e in data["files"]):
        problem = "has no list of files"
    elif not isinstance(data.get("textures"), list) or not all(e in data["files"] for e in data["textures"]):
        problem = "has textures that it does not list as files"
    elif data.get("metadata") is not None and data["metadata"] not in data["files"]:
        problem = "has metadata that it does not list as a file"
    elif not fab_materials.maps_are_valid(data.get("maps"), data["files"]) or \
            data.get("normal") not in NORMAL_CHOICES:
        problem = "has bad maps or normal fields"
    if problem:
        raise WorkerError("import_failed", "The Fab source file %s." % problem)
    return data


# Reads a size such as "2x2", "60 x 60 cm" or "1.5x1 m" into metres. Returns (width, height) or None.
def parse_size(text):
    found = SIZE.search(text) if isinstance(text, str) else None
    if not found:
        return None
    scale = UNITS[(found.group(3) or "m").lower()]
    width, height = float(found.group(1)) * scale, float(found.group(2)) * scale
    return (width, height) if 0.01 <= width <= 100 and 0.01 <= height <= 100 else None


# Looks through the metadata for a physical size. Megascans files hold it under physicalSize, or in a
# list of key and value pairs under meta.
def find_size(data):
    if isinstance(data.get("physicalSize"), str):
        return parse_size(data["physicalSize"])
    items = data.get("meta") if isinstance(data.get("meta"), list) else []
    for item in items:
        if isinstance(item, dict) and item.get("key") in ("scanArea", "physicalSize", "size"):
            size = parse_size(item.get("value"))
            if size:
                return size
    return None


# Returns (name or None, tile size dictionary or None) from the metadata file, ignoring anything unknown.
def read_metadata(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None, None
    if not isinstance(data, dict):
        return None, None
    tags = data.get("semanticTags") if isinstance(data.get("semanticTags"), dict) else {}
    name = tags.get("name") if isinstance(tags.get("name"), str) else data.get("name")
    name = name.strip()[:120] if isinstance(name, str) and name.strip() else None
    size = find_size(data)
    return name, ({"width": size[0], "height": size[1]} if size else None)


# Picks the set that makes the material: the one with the most channels, then the later name.
def pick_set(sets):
    usable = {key: entry for key, entry in sets.items() if fab_materials.has_maps(entry)}
    if not usable:
        return None

    # Ranks a set by how many channels it fills, then by its name.
    def count(key):
        entry = usable[key]
        return len(entry["channels"]) + bool(entry["gloss"]) + bool(entry["packed"]), key

    return usable[max(usable, key=count)]


# Unpacks the textures the file lists, then plans each channel from the user's picks, then the one texture
# set, then plain values. Warns ambiguous_map and unused_image. Raises import_failed and no_base_color.
def load(ctx, source):
    data = read_source(source["path"])
    folder = data["unpack_folder"]
    members = sorted(set(data["textures"]) | ({data["metadata"]} if data["metadata"] else set()))
    zip_importer.unpack(data["zip"], folder, members)

    # Returns where an entry of the zip was unpacked.
    def local(entry):
        return os.path.join(folder, *entry.split("/"))

    name, tile = (None, None)
    if data["metadata"]:
        name, tile = read_metadata(local(data["metadata"]))

    picks = {}
    for channel, entry in data["maps"].get("*", {}).items():
        if not os.path.isfile(local(entry)):
            raise WorkerError("import_failed", "The picked texture %s is not in the zip." % entry)
        picks[channel] = local(entry)
    for channel, path in source.get("maps", {}).items():
        if not os.path.isfile(path):
            raise WorkerError("import_failed", "The map %s does not exist." % os.path.basename(path))
        picks[channel] = path
    values = dict(source.get("values", {}))

    files = [local(entry) for entry in data["textures"]
             if not fab_names.is_preview(entry) and os.path.isfile(local(entry))]
    chosen = pick_set(fab_materials.make_sets(files)) or {"channels": {}, "gloss": [], "packed": [], "ignored": []}
    used = set(picks.values())
    plans = {}
    normal_option = source.get("normal", "auto")
    if normal_option == "auto":
        normal_option = data["normal"]

    for channel in CHANNELS:
        if channel in picks:
            path = picks[channel]
        elif channel in values:
            plans[channel] = ChannelPlan(channel, "value", None, values[channel], None)
            continue
        elif channel in chosen["channels"]:
            path = fab_materials.best_file(ctx, channel, chosen["channels"][channel], used)
        else:
            continue
        kind = "file"
        if channel == "normal":
            convention = normal_option if normal_option != "auto" else (fab_names.classify(path)[1] or "opengl")
            kind = "directx" if convention == "directx" else "file"
        plans[channel] = ChannelPlan(channel, kind, path, None, None)

    if "roughness" not in plans and chosen["gloss"]:
        path = fab_materials.best_file(ctx, "roughness", chosen["gloss"], used)
        plans["roughness"] = ChannelPlan("roughness", "gloss", path, None, None)
    else:
        used.update(chosen["gloss"])
    if chosen["packed"]:
        packed = fab_materials.best_file(ctx, "packed", chosen["packed"], used)
        for channel, index in PACKED_CHANNELS:
            if channel not in plans:
                plans[channel] = ChannelPlan(channel, "packed", packed, None, index)
    used.update(chosen["ignored"])

    if "base_color" not in plans:
        raise WorkerError("no_base_color", "The set has no base color image or color.")
    if "normal" not in plans:
        plans["normal"] = ChannelPlan("normal", "value", None, "flat", None)
    for channel, value in DEFAULT_VALUES.items():
        if channel not in plans:
            plans[channel] = ChannelPlan(channel, "value", None, value, None)

    for path in sorted(p for p in files if p not in used):
        if fab_names.classify(path)[0] is not None:
            ctx.warn("unused_image", "%s matched no channel and was not used." % os.path.basename(path))
    return Loaded(plans, None, {"name": name} if name else None, tile)


MATERIAL_SOURCES.register("fab", SourceEntry(("fab",), load))
