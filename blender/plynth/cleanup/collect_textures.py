import os
import shutil

import bpy

from . import Step
from ..registry import CLEANUP_STEPS

ORDER = 70
SKIPPED_SOURCES = {"GENERATED", "VIEWER"}
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tif", ".tiff", ".exr", ".hdr", ".webp", ".dds"}
EXTENSIONS = {"PNG": ".png", "JPEG": ".jpg", "BMP": ".bmp", "TARGA": ".tga", "TIFF": ".tif", "OPEN_EXR": ".exr",
              "HDR": ".hdr", "WEBP": ".webp"}


# Looks for a file name under the given folders, ignoring letter case. Returns the path or None.
def find_by_name(name, folders):
    wanted = name.lower()
    for folder in folders:
        for root, _dirs, files in os.walk(folder):
            for candidate in sorted(files):
                if candidate.lower() == wanted:
                    return os.path.join(root, candidate)
    return None


# Returns a file name for a packed image, with an ending that matches its format.
def packed_name(image):
    name = os.path.basename(bpy.path.abspath(image.filepath)) if image.filepath else ""
    name = name or image.name
    if not os.path.splitext(name)[1]:
        name += EXTENSIONS.get(image.file_format, ".png")
    return "".join(c if c not in '<>:"/\\|?*' else "_" for c in name)


# Returns a name that is not used yet in the textures folder, adding _2, _3 and so on.
def free_name(name, used):
    stem, ext = os.path.splitext(name)
    candidate, number = name, 2
    while candidate.lower() in used:
        candidate = "%s_%d%s" % (stem, number, ext)
        number += 1
    return candidate


# Copies every image the model uses into textures/ beside asset.blend and links it by its full path, so it
# still loads while the scene is unsaved; saving asset.blend makes the paths relative. Packed images are written
# there too. Images whose file cannot be found are listed as missing.
def run(ctx):
    output = ctx.params["output_folder"]
    textures = os.path.join(output, "textures")
    bpy.data.orphans_purge(do_recursive=True)

    used = set()
    copied = {}
    names = []
    missing = []
    for image in bpy.data.images:
        if image.source in SKIPPED_SOURCES or image.type != "IMAGE":
            continue
        if image.packed_file is not None:
            name = free_name(packed_name(image), used)
            os.makedirs(textures, exist_ok=True)
            with open(os.path.join(textures, name), "wb") as handle:
                handle.write(image.packed_file.data)
            image.unpack(method="REMOVE")
            image.filepath = os.path.join(textures, name)
            image.reload()
        else:
            original = bpy.path.abspath(image.filepath) if image.filepath else ""
            loadable = os.path.isfile(original) and image.size[0] > 0
            if not loadable:
                original = find_by_name(os.path.basename(original or image.filepath), ctx.state["search_folders"])
            if (original is None or not os.path.isfile(original)
                    or os.path.splitext(original)[1].lower() not in IMAGE_SUFFIXES):
                missing.append(os.path.basename(image.filepath) or image.name)
                continue
            key = os.path.normcase(os.path.abspath(original))
            if key in copied:
                name = copied[key]
            else:
                name = free_name(os.path.basename(original), used)
                os.makedirs(textures, exist_ok=True)
                shutil.copyfile(original, os.path.join(textures, name))
                copied[key] = name
            image.filepath = os.path.join(textures, name)
            if not loadable:
                image.reload()
                if image.size[0] == 0:
                    os.remove(os.path.join(textures, name))
                    copied.pop(key, None)
                    missing.append(os.path.basename(original))
                    continue
        used.add(name.lower())
        if "textures/" + name not in names:
            names.append("textures/" + name)

    ctx.state["textures"] = sorted(names)
    ctx.state["missing_textures"] = sorted(set(missing))


CLEANUP_STEPS.register("collect_textures", Step(ORDER, "Collecting textures", run))
