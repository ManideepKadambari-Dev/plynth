import contextlib
import importlib

import bpy

from . import ExporterEntry
from ..registry import EXPORTERS


# Exports objects (or the whole scene when None) to an FBX file with the one Plynth profile: metres, Y up,
# meshes and empties only, modifiers applied, face smoothing.
def export(objects, filepath, path_mode):
    options = dict(
        filepath=filepath,
        use_selection=False,
        object_types={"MESH", "EMPTY"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        global_scale=1.0,
        axis_forward="-Z",
        axis_up="Y",
        use_space_transform=True,
        bake_space_transform=False,
        mesh_smooth_type="FACE",
        use_mesh_modifiers=True,
        add_leaf_bones=False,
        bake_anim=False,
        embed_textures=False,
        use_custom_props=True,
        path_mode=path_mode,
    )
    if objects is not None:
        options["use_selection"] = True
        for obj in bpy.context.scene.collection.all_objects:
            obj.select_set(False)
        for obj in objects:
            obj.select_set(True)
    with relative_texture_names(path_mode):
        bpy.ops.export_scene.fbx(**options)


# While active, the FBX exporter writes the relative texture path in both the absolute and the relative file
# name fields. Its absolute field would otherwise name the folder the file was written into, which is a
# staging folder that is renamed afterwards. When the path mode is not RELATIVE or this Blender has no such
# function to wrap, nothing is changed and a line is printed.
@contextlib.contextmanager
def relative_texture_names(path_mode="RELATIVE"):
    module = None
    if path_mode == "RELATIVE":
        try:
            package = bpy.types.EXPORT_SCENE_OT_fbx.__module__
            module = importlib.import_module(package + ".export_fbx_bin")
            original = module._gen_vid_path
        except (AttributeError, ImportError):
            module = None
            print("fbx export: texture path wrapper not available in this Blender, exporting without it")
    if module is None:
        yield
        return

    def relative_only(img, scene_data):
        _, relative = original(img, scene_data)
        return relative, relative

    module._gen_vid_path = relative_only
    try:
        yield
    finally:
        module._gen_vid_path = original


EXPORTERS.register("fbx", ExporterEntry(".fbx", export))
