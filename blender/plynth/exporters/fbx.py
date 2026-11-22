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
    bpy.ops.export_scene.fbx(**options)


EXPORTERS.register("fbx", ExporterEntry(".fbx", export))
