import os
import shutil

from .. import (IMAGE_SUFFIXES, ChannelPlan, Loaded, SourceEntry, names, read_size, sidecar)
from ...importers import zip as zip_importer
from ...jobs import WorkerError
from ...registry import MATERIAL_SOURCES

SIDECAR_NAME = "material.json"
ALPHA_SUFFIXES = (".png", ".tga", ".tif", ".tiff", ".exr")
PACKED_CHANNELS = (("occlusion", 0), ("roughness", 1), ("metallic", 2))
DEFAULT_VALUES = {"roughness": 0.5, "metallic": 0.0}


# Says whether a file name is an image the set may use.
def is_image(path):
    return os.path.splitext(path)[1].lower() in IMAGE_SUFFIXES


# Returns every image under a folder, and the material.json nearest the top, if any.
def scan_folder(folder):
    images = []
    sidecars = []
    for current, directories, files in os.walk(folder):
        directories[:] = sorted(d for d in directories if not d.startswith(".") and d != "__MACOSX")
        depth = os.path.relpath(current, folder).count(os.sep) if current != folder else 0
        for name in sorted(files):
            if name.startswith("."):
                continue
            path = os.path.join(current, name)
            if name.lower() == SIDECAR_NAME:
                sidecars.append((depth, path))
            elif is_image(name):
                images.append(path)
    sidecars.sort()
    return images, (sidecars[0][1] if sidecars else None)


# Finds what the source path holds. Returns (images, sidecar path or None, true when it is one image file).
def locate(ctx, path):
    if os.path.isdir(path):
        images, side = scan_folder(path)
        return images, side, False
    if not os.path.isfile(path):
        raise WorkerError("import_failed", "%s does not exist." % path)
    suffix = os.path.splitext(path)[1].lower()
    if suffix == ".zip":
        folder = os.path.join(ctx.work_folder, "unpacked_material")
        shutil.rmtree(folder, ignore_errors=True)
        ctx.state.setdefault("temp_folders", []).append(folder)
        zip_importer.unpack(path, folder)
        images, side = scan_folder(folder)
        return images, side, False
    if suffix in IMAGE_SUFFIXES:
        return [path], None, True
    raise WorkerError("import_failed", "%s is not a folder, a zip or an image file." % os.path.basename(path))


# Raises import_failed when a file named by the user or by material.json is not there.
def require_file(path, where):
    if not os.path.isfile(path):
        raise WorkerError("import_failed", "The %s %s does not exist." % (where, os.path.basename(path)))
    return path


# Returns the normal map convention to use: the job's, then material.json's, then the file name's.
def normal_convention(source, side, filename):
    if source.get("normal", "auto") != "auto":
        return source["normal"]
    if side is not None and side.get("normal", "auto") != "auto":
        return side["normal"]
    return names.classify(filename)[1] or "opengl"


# Reads the source, decides which image or value feeds each channel and returns Loaded. Warns ambiguous_map
# and unused_image. Raises import_failed and no_base_color.
def load(ctx, source):
    images, sidecar_path, single = locate(ctx, source["path"])
    side = sidecar.read(sidecar_path) if sidecar_path else None
    side_folder = os.path.dirname(sidecar_path) if sidecar_path else None
    if not images and side is None:
        raise WorkerError("import_failed", "%s holds no image files." % os.path.basename(source["path"]))

    sizes = {}

    def rank(path):
        if path not in sizes:
            try:
                width, height = read_size(path)
            except ValueError:
                width = height = 0
            sizes[path] = (width * height, os.path.getsize(path))
        return sizes[path]

    request_maps = {c: require_file(p, "map") for c, p in source.get("maps", {}).items()}
    request_values = dict(source.get("values", {}))
    side_maps = {c: require_file(os.path.join(side_folder, p), "map") for c, p in (side or {}).get("maps", {}).items()}
    side_values = dict((side or {}).get("values", {}))
    groups = names.group(images)
    used = set()
    plans = {}

    def pick(channel, found):
        best = max(found, key=lambda path: (rank(path), path))
        if len(found) > 1:
            ctx.warn("ambiguous_map", "Several images fit %s; used %s." % (channel, os.path.basename(best)))
            used.update(found)
        return best

    def file_plan(channel, path, kind="file", node=None):
        used.add(path)
        return ChannelPlan(channel, kind, path, None, node)

    def value_plan(channel, value):
        return ChannelPlan(channel, "value", None, value, None)

    for channel in ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion"):
        path = request_maps.get(channel) or side_maps.get(channel)
        if path is None and channel in request_values:
            plans[channel] = value_plan(channel, request_values[channel])
            continue
        if path is None and channel in side_values:
            plans[channel] = value_plan(channel, side_values[channel])
            continue
        if path is None and channel == "base_color" and single:
            path = images[0]
        if path is None and channel in groups["channels"]:
            path = pick(channel, groups["channels"][channel])
        if path is None:
            continue
        kind = "file"
        if channel == "normal" and normal_convention(source, side, path) == "directx":
            kind = "directx"
        plans[channel] = file_plan(channel, path, kind)

    if "roughness" not in plans and groups["gloss"]:
        plans["roughness"] = file_plan("roughness", pick("roughness", groups["gloss"]), "gloss")
    packed = max(groups["packed"], key=lambda path: (rank(path), path)) if groups["packed"] else None
    if packed is not None:
        for channel, index in PACKED_CHANNELS:
            if channel not in plans:
                plans[channel] = file_plan(channel, packed, "packed", index)

    if "base_color" not in plans and side is not None and "base_color_value" in side:
        plans["base_color"] = ChannelPlan("base_color", "color", None, list(side["base_color_value"]), None)
    if "base_color" not in plans:
        raise WorkerError("no_base_color", "The set has no base color image or color.")

    base = plans["base_color"]
    if "opacity" not in plans and base.kind == "file" and os.path.splitext(base.file)[1].lower() in ALPHA_SUFFIXES:
        plans["opacity"] = ChannelPlan("opacity", "alpha", base.file, None, None)
    if "normal" not in plans:
        plans["normal"] = value_plan("normal", "flat")
    for channel, value in DEFAULT_VALUES.items():
        if channel not in plans:
            plans[channel] = value_plan(channel, value)

    for path in images:
        if path not in used and path not in groups["ignored"]:
            ctx.warn("unused_image", "%s matched no channel and was not used." % os.path.basename(path))
    tile = side.get("tile_size") if side else None
    return Loaded(plans, None, sidecar.info_of(side) if side is not None else None, tile)


MATERIAL_SOURCES.register("texture_set", SourceEntry(("texture_set",), load))
