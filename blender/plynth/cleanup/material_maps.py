import os
import re
import time

import bpy
import numpy as np

from . import Step
from .. import registry, shading
from ..jobs import WorkerError
from ..materials import CHANNELS, ImageMissing, read_size, to_bytes, write_png
from ..materials import bakers
from ..materials.sources import blend
from ..registry import CLEANUP_STEPS

ORDER = 90
PREVIEW_SIZE = 1024
MAPS_FOLDER = "textures/maps"
PREVIEW_FOLDER = "textures/preview"
MAX_KEY_LENGTH = 64
NONE_ENTRY = {"file": None, "from": "none", "detail": ""}

# Nodes whose result depends on the object or the view, so it looks different on the flat UV square.
OBJECT_BOUND = {
    "ShaderNodeNewGeometry", "ShaderNodeObjectInfo", "ShaderNodeCameraData", "ShaderNodeLayerWeight",
    "ShaderNodeFresnel", "ShaderNodeLightPath", "ShaderNodeAmbientOcclusion", "ShaderNodeBevel",
    "ShaderNodeTangent", "ShaderNodeAttribute", "ShaderNodeVertexColor", "ShaderNodeWireframe",
    "ShaderNodeParticleInfo", "ShaderNodeHairInfo", "ShaderNodePointInfo", "ShaderNodeVolumeInfo",
    "ShaderNodeUVAlongStroke", "ShaderNodeTexEnvironment", "ShaderNodeTexSky",
}
# Procedural textures that use generated (object bound) coordinates when nothing is linked to Vector.
GENERATED_BY_DEFAULT = {
    "ShaderNodeTexNoise", "ShaderNodeTexVoronoi", "ShaderNodeTexMusgrave", "ShaderNodeTexWave",
    "ShaderNodeTexMagic", "ShaderNodeTexGradient", "ShaderNodeTexChecker", "ShaderNodeTexBrick",
}
# The render settings the Eevee baker changes, put back after the maps are made.
SETTINGS = (
    ("render", ("engine", "resolution_x", "resolution_y", "resolution_percentage", "filepath", "filter_size",
                "use_compositing", "use_sequencer", "film_transparent", "dither_intensity", "use_file_extension")),
    ("view_settings", ("view_transform", "look", "exposure", "gamma")),
    ("display_settings", ("display_device",)),
)


# Makes a folder name from a material name the way the app makes catalog ids: lower case, other runs of
# characters become one underscore, at most 64 characters.
def catalog_key(name):
    key = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    if not key:
        return "asset"
    if key[0].isdigit():
        key = "a_" + key
    return key[:MAX_KEY_LENGTH].rstrip("_") or "asset"


# Returns a key no other material of the asset uses, adding _2, _3 and so on.
def unique_key(name, used):
    base = catalog_key(name)
    key, number = base, 2
    while key in used:
        suffix = "_%d" % number
        key = base[:MAX_KEY_LENGTH - len(suffix)] + suffix
        number += 1
    used.add(key)
    return key


# Returns the nodes that feed one node, the node included.
def upstream(node):
    seen = {}
    pending = [node]
    while pending:
        current = pending.pop()
        if current.name in seen:
            continue
        seen[current.name] = current
        for socket in current.inputs:
            for link in socket.links:
                pending.append(link.from_node)
    return list(seen.values())


# Says whether a node reads something the UV square does not have: object, view or generated coordinates.
def is_object_bound(node):
    kind = node.bl_idname
    if kind in OBJECT_BOUND:
        return True
    if kind == "ShaderNodeTexCoord":
        return any(output.is_linked and output.name != "UV" for output in node.outputs)
    if kind in GENERATED_BY_DEFAULT:
        return not node.inputs["Vector"].is_linked
    if kind == "ShaderNodeTexImage":
        return node.projection != "FLAT"
    if kind == "ShaderNodeGroup" and node.node_tree is not None:
        return any(is_object_bound(inner) for inner in node.node_tree.nodes)
    return False


# Returns the Principled BSDF to read a material from, possibly one made from a node group, a mix of shaders or
# another shader, or None.
def principled_of(material):
    tree = material.node_tree if material.use_nodes else None
    return blend.readable_principled(tree) if tree is not None else None


# Returns the plan for one channel: the blend source's plan, or a constant when it reads an image that is missing.
def plan_channel(ctx, channel, socket, material, folder):
    plan = blend.plan_for(ctx, channel, socket, material.name, folder)
    if plan.kind != "node":
        return plan
    node = material.node_tree.nodes[plan.node["node"]]
    if node.bl_idname == "ShaderNodeTexImage" and (node.image is None or node.image.size[0] == 0):
        return blend.value_plan(channel, socket)
    return plan


# Returns the plans of a material, or None when it has no Principled BSDF to read.
def plans_of(ctx, material, folder):
    principled = principled_of(material)
    if principled is None:
        return None
    return {channel: plan_channel(ctx, channel, principled.inputs[name], material, folder)
            for channel, name in blend.SOCKETS.items()}


# Returns constant plans for a material that has no Principled BSDF: its viewport color and metallic value.
def constant_plans(material):
    color = [blend.to_srgb(c) for c in material.diffuse_color[:3]]
    return {
        "base_color": blend.ChannelPlan("base_color", "color", None, color, None),
        "normal": blend.ChannelPlan("normal", "value", None, "flat", None),
        "roughness": blend.ChannelPlan("roughness", "value", None, 0.5, None),
        "metallic": blend.ChannelPlan("metallic", "value", None, float(material.metallic), None),
    }


# Returns the plan of a constant for a channel whose nodes could not be rendered: the Principled input's own value.
def default_plan(material, channel):
    principled = principled_of(material)
    if principled is None:
        return constant_plans(material)[channel]
    return blend.value_plan(channel, principled.inputs[blend.SOCKETS[channel]])


# Makes one map with the first baker that can. Returns its entry, or None when there is no map.
def bake_plan(ctx, plan, folder):
    entries = sorted(registry.BAKERS.all(), key=lambda pair: pair[1].order)
    baker = next((entry for _, entry in entries if entry.can_bake(ctx, plan)), None)
    if baker is None:
        raise WorkerError("bake_failed", "No baker can make the %s map." % plan.channel)
    return baker.bake(ctx, plan, folder)


# Makes the maps of one material into textures/maps/<key>/. Returns the entries by channel.
def make_material_maps(ctx, material, key):
    output = ctx.params["output_folder"]
    folder = os.path.join(output, *MAPS_FOLDER.split("/"), key)
    os.makedirs(folder, exist_ok=True)
    plans = plans_of(ctx, material, output)
    reasons = []
    if plans is None:
        plans = constant_plans(material)
        reasons.append("it has no Principled BSDF")
    maps = {}
    for channel in CHANNELS:
        plan = plans.get(channel)
        entry = None
        if plan is not None:
            label = channel.replace("_", " ")
            if plan.kind == "node":
                node = material.node_tree.nodes[plan.node["node"]]
                if any(is_object_bound(n) for n in upstream(node)):
                    reasons.append("its %s uses object or generated coordinates" % label)
            try:
                entry = bake_plan(ctx, plan, folder)
            except ImageMissing:
                raise
            except (WorkerError, ValueError, OSError) as error:
                message = getattr(error, "message", str(error))
                if plan.kind != "node":
                    raise WorkerError("save_failed", "The %s map of %s could not be made: %s" % (
                        channel, material.name, message))
                reasons.append("its %s could not be rendered" % label)
                entry = bake_plan(ctx, default_plan(material, channel), folder)
        maps[channel] = dict(entry) if entry else dict(NONE_ENTRY)
        if entry:
            maps[channel]["file"] = "%s/%s/%s" % (MAPS_FOLDER, key, os.path.basename(entry["file"]))
    if reasons:
        ctx.warn("material_approximated", "%s looks different as maps: %s." % (
            material.name, "; ".join(dict.fromkeys(reasons))))
    return maps


# Returns the absolute path of a map entry.
def absolute(output, entry):
    return os.path.join(output, *entry["file"].split("/"))


# Returns the pixels of a loaded image as floats, shape (height, width, 4), top row first.
def read_loaded(image):
    width, height = image.size
    buffer = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(buffer)
    return np.flipud(buffer.reshape(height, width, 4))


# Writes a copy of the map scaled so its longest side is PREVIEW_SIZE and returns its entry, or the same
# entry when the map is small enough.
def preview_entry(output, key, channel, entry):
    if not entry["file"]:
        return dict(entry)
    path = absolute(output, entry)
    width, height = read_size(path)
    if max(width, height) <= PREVIEW_SIZE:
        return dict(entry)
    factor = PREVIEW_SIZE / float(max(width, height))
    new_size = (max(1, int(round(width * factor))), max(1, int(round(height * factor))))
    folder = os.path.join(output, *PREVIEW_FOLDER.split("/"))
    os.makedirs(folder, exist_ok=True)
    image = bpy.data.images.load(path, check_existing=False)
    try:
        image.colorspace_settings.name = "Non-Color"
        image.alpha_mode = "CHANNEL_PACKED"
        image.scale(*new_size)
        pixels = to_bytes(read_loaded(image))
    finally:
        bpy.data.images.remove(image)
    name = "%s_%s.png" % (key, channel)
    write_png(os.path.join(folder, name), pixels[:, :, :3])
    copy = dict(entry)
    copy["file"] = "%s/%s" % (PREVIEW_FOLDER, name)
    return copy


# Points every image that has a preview copy at the copy, for an FBX the app loads. Returns what to give
# back to restore_images. Images with no copy in textures/preview/ are left alone.
def use_preview_images(folder):
    swapped = []
    for image in bpy.data.images:
        path = image.filepath.replace("\\", "/")
        match = re.match(r"^//textures/maps/([^/]+)/([^/]+)\.[A-Za-z]+$", path)
        if image.source != "FILE" or not match:
            continue
        name = "%s_%s.png" % (match.group(1), match.group(2))
        if os.path.isfile(os.path.join(folder, *PREVIEW_FOLDER.split("/"), name)):
            swapped.append((image, image.filepath))
            image.filepath = "//%s/%s" % (PREVIEW_FOLDER, name)
    return swapped


# Puts the images back to the files use_preview_images replaced.
def restore_images(swapped):
    for image, path in swapped:
        image.filepath = path


# Remembers the render settings the baker changes and returns a function that puts them back.
def remember_scene():
    scene = bpy.context.scene
    saved = []
    for holder, names in SETTINGS:
        target = getattr(scene, holder)
        saved.extend((target, name, getattr(target, name)) for name in names)
    saved.append((scene.eevee, "taa_render_samples", getattr(scene.eevee, "taa_render_samples", None)))
    world, camera = scene.world, scene.camera

    def restore():
        for target, name, value in saved:
            try:
                if value is not None:
                    setattr(target, name, value)
            except (AttributeError, TypeError, RuntimeError):
                pass
        scene.world = world if world is not None and world.name in bpy.data.worlds else None
        scene.camera = camera if camera is not None and camera.name in bpy.data.objects else None
    return restore


# Rebuilds a material from its maps in textures/maps/. Saving asset.blend makes the image paths relative.
def rebuild_material(output, material, maps):
    paths = {channel: absolute(output, entry) for channel, entry in maps.items() if entry["file"]}
    shading.apply_maps(material, paths)


# Lists the image files the rebuilt materials use, relative to the output folder, and deletes the copies of
# the source images that nothing uses any more.
def update_textures(ctx):
    output = ctx.params["output_folder"]
    used = set()
    for image in bpy.data.images:
        if image.source == "FILE" and image.users > 0:
            absolute_path = os.path.abspath(bpy.path.abspath(image.filepath, start=output))
            used.add(os.path.relpath(absolute_path, output).replace("\\", "/"))
    for name in ctx.state.get("textures", []):
        if name not in used and os.path.isfile(os.path.join(output, *name.split("/"))):
            os.remove(os.path.join(output, *name.split("/")))
    ctx.state["textures"] = sorted(used)


# Makes the maps of every material of the asset, rebuilds the materials from them and writes the preview copies.
def run(ctx):
    registry.discover(bakers)
    output = ctx.params["output_folder"]
    ctx.state["material_maps"] = []
    started = time.monotonic()
    restore = remember_scene()
    used = set()
    made = []
    try:
        for name in ctx.state.get("materials", []):
            material = bpy.data.materials.get(name)
            if material is not None:
                key = unique_key(name, used)
                made.append((material, key, make_material_maps(ctx, material, key)))
    finally:
        restore()
    for material, key, maps in made:
        try:
            rebuild_material(output, material, maps)
            previews = {channel: preview_entry(output, key, channel, entry) for channel, entry in maps.items()}
        except (shading.MapError, ValueError, OSError) as error:
            raise WorkerError("save_failed", "The maps of %s could not be used: %s" % (material.name, error))
        ctx.state["material_maps"].append({"name": material.name, "maps": maps, "preview_maps": previews})
    bpy.data.orphans_purge(do_recursive=True)
    update_textures(ctx)
    print("Made the maps of %d materials in %.2f s" % (len(made), time.monotonic() - started))


CLEANUP_STEPS.register("material_maps", Step(ORDER, "Making the material maps", run))
