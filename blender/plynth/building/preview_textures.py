import os

import bpy

CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
PNG_EXTENSIONS = (".png",)
JPEG_EXTENSIONS = (".jpg", ".jpeg")


# Returns a path the way two spellings of one file compare equal: absolute, forward slashes, lower case.
def normal_path(path):
    return os.path.abspath(bpy.path.abspath(path)).replace("\\", "/").lower()


# Returns the file name stem each material of the scene is copied under: the catalog material id for a built
# surface that shows one, the asset id and material name for furniture. Neutral looks are left out, their
# images are single colour and tiny.
def owners(ctx):
    result = {}
    for entry in ctx.state.get("built_materials", []):
        if entry["material"]:
            result[entry["name"]] = entry["material"]
    for entry in ctx.state.get("asset_materials", []):
        result[entry["name"]] = "%s_%s" % (entry["asset"], entry["name"])
    return result


# Writes a copy of the image no larger than limit pixels on its longest side, in the same format when that
# is PNG or JPEG and as PNG otherwise, with the same colour space. Returns the new file's path.
def write_copy(image, source, target_stem, folder, limit):
    width, height = image.size
    factor = limit / float(max(width, height))
    extension = os.path.splitext(source)[1].lower()
    if extension not in PNG_EXTENSIONS + JPEG_EXTENSIONS:
        extension = ".png"
    target = os.path.join(folder, target_stem + extension).replace("\\", "/")
    scaled = bpy.data.images.load(source, check_existing=False)
    try:
        scaled.colorspace_settings.name = image.colorspace_settings.name
        scaled.scale(max(1, int(round(width * factor))), max(1, int(round(height * factor))))
        scaled.filepath_raw = target
        scaled.file_format = "JPEG" if extension in JPEG_EXTENSIONS else "PNG"
        scaled.save()
    finally:
        bpy.data.images.remove(scaled)
    return target


# Deletes the png and jpg files of the preview folder, which are the copies an earlier build made, and
# removes the folder when it ends up empty. Other files are left alone.
def clear_old_copies(folder):
    if not os.path.isdir(folder):
        return
    for name in os.listdir(folder):
        path = os.path.join(folder, name)
        if os.path.isfile(path) and name.lower().endswith(PNG_EXTENSIONS + JPEG_EXTENSIONS):
            os.remove(path)
    if not os.listdir(folder):
        os.rmdir(folder)


# Points every image node of the given objects' materials that holds an image larger than preview_texture_size
# on its longest side at a scaled copy in the job's preview_textures folder, one copy per source file. The
# copies are named <owner>_<channel>. Call it only after building.blend is saved, because it changes the
# scene. Returns a dictionary from the normal_path of each source file to the path of its copy.
def make_preview_copies(ctx, objects):
    limit = ctx.params["preview_texture_size"]
    folder = os.path.join(ctx.params["output_folder"], ctx.params["preview_textures_folder"])
    clear_old_copies(folder)
    names = owners(ctx)
    copies = {}
    loaded = {}
    used = set()
    done = set()
    for obj in objects:
        for mat in obj.data.materials:
            if mat is None or mat.name in done or mat.node_tree is None or mat.name not in names:
                continue
            done.add(mat.name)
            for node in mat.node_tree.nodes:
                if node.type != "TEX_IMAGE" or node.image is None or not node.image.filepath:
                    continue
                source = bpy.path.abspath(node.image.filepath)
                if not os.path.isfile(source) or max(node.image.size) <= limit:
                    continue
                key = normal_path(source)
                if key not in copies:
                    channel = node.label if node.label in CHANNELS else "image"
                    stem = unique_stem("%s_%s" % (names[mat.name], channel), used)
                    os.makedirs(folder, exist_ok=True)
                    copies[key] = write_copy(node.image, source, stem, folder, limit)
                if key not in loaded:
                    image = bpy.data.images.load(copies[key], check_existing=False)
                    image.colorspace_settings.name = node.image.colorspace_settings.name
                    loaded[key] = image
                node.image = loaded[key]
    return copies


# Returns the stem, with a number added when it is already used, and remembers it.
def unique_stem(stem, used):
    candidate = stem
    number = 2
    while candidate.lower() in used:
        candidate = "%s_%d" % (stem, number)
        number += 1
    used.add(candidate.lower())
    return candidate


# Returns the maps dictionary of a materials file entry with every path that has a preview copy replaced by
# the copy.
def repoint_maps(maps, copies):
    result = {}
    for channel, path in maps.items():
        result[channel] = copies.get(normal_path(path), path) if path else path
    return result
