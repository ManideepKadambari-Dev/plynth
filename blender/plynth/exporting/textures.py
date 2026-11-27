import os
import shutil

import bpy

from . import CHANNEL_WORDS, Step, pascal, sanitize, unique_name
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS

ORDER = 40
EXTENSIONS = (".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".exr")


# Returns a path the way two spellings of one file compare equal.
def normal_path(path):
    return os.path.abspath(bpy.path.abspath(path)).replace("\\", "/").lower()


# Returns the file name stem of one image of a material, after the profile's prefix: <Material>_<Channel> for
# a catalog material, the slot name for a neutral look, the furniture material name for furniture, and the
# image file name for a material the user made by hand.
def stem_for(final, info, node, source, prefix=""):
    entry = info["entry"]
    channel = CHANNEL_WORDS.get(node.label)
    file_part = sanitize(os.path.splitext(os.path.basename(source))[0])
    if entry is None:
        return "%s%s_%s" % (prefix, final, file_part)
    if entry.get("asset") or not entry.get("material"):
        owner = final
    else:
        owner = pascal(entry["material"])
    return "%s%s_%s" % (prefix, owner, channel or file_part)


# Works out which source file gets which name, each starting with the prefix. Only images wired into the
# material are planned: the FBX writes those alone, so an occlusion map held in a loose node is not copied.
# Returns a list of plan entries (source, name, nodes) in a fixed order and a list of missing files as
# (material name, path).
def plan_textures(materials, prefix=""):
    taken = set()
    by_source = {}
    plan = []
    missing = []
    for final in sorted(materials):
        info = materials[final]
        tree = info["material"].node_tree
        if tree is None:
            continue
        for node in tree.nodes:
            if node.type != "TEX_IMAGE" or node.image is None or not node.image.filepath:
                continue
            if not any(output.is_linked for output in node.outputs):
                continue
            source = bpy.path.abspath(node.image.filepath)
            if not os.path.isfile(source):
                missing.append((final, source))
                continue
            key = normal_path(source)
            if key not in by_source:
                extension = os.path.splitext(source)[1].lower()
                keep = extension in EXTENSIONS
                stem = unique_name(stem_for(final, info, node, source, prefix), taken)
                entry = {"source": source, "name": stem + (extension if keep else ".png"), "convert": not keep,
                         "images": []}
                by_source[key] = entry
                plan.append(entry)
            if node.image not in by_source[key]["images"]:
                by_source[key]["images"].append(node.image)
    return plan, missing


# Writes a copy of an image file as a PNG, for a format the engine does not take.
def convert_to_png(source, target):
    image = bpy.data.images.load(source, check_existing=False)
    try:
        image.filepath_raw = target
        image.file_format = "PNG"
        image.save()
    finally:
        bpy.data.images.remove(image)


# Names every image after its material, with the texture prefix of the job's profile. When the job writes,
# copies each source file once, byte for byte, into Textures/ of the output folder and points the image nodes
# at the copies. When it only checks, nothing is copied and the scene keeps its own images. The plan stays in
# ctx.state either way.
def run(ctx):
    plan, missing = plan_textures(ctx.state["materials"], ctx.state["profile"].texture_prefix)
    ctx.state["texture_plan"] = plan
    ctx.state["texture_missing"] = missing
    ctx.state["textures"] = []
    if not ctx.params["write"]:
        return
    folder = os.path.join(ctx.params["output_folder"], ctx.params["textures_folder"])
    try:
        os.makedirs(folder, exist_ok=True)
        for entry in plan:
            target = os.path.join(folder, entry["name"])
            if entry["convert"]:
                convert_to_png(entry["source"], target)
            else:
                shutil.copyfile(entry["source"], target)
            for image in entry["images"]:
                image.filepath = target.replace("\\", "/")
            entry["target"] = target
    except OSError as error:
        raise WorkerError("export_failed", "The textures could not be copied: %s" % error)
    ctx.state["textures"] = [entry["name"] for entry in plan]


EXPORT_STEPS.register("textures", Step(ORDER, "Collecting textures", run))
