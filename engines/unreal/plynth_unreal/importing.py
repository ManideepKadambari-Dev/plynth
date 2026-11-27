import unreal

from . import ue
from .errors import LinkError


# Returns the FBX import options Plynth uses: static meshes only, no materials or textures from the file,
# collision from the UCX_ meshes, normals from the file, scene converted to Unreal's axes and centimetres.
def fbx_options(combine):
    options = unreal.FbxImportUI()
    ue.try_set(options, "automated_import_should_detect_type", False)
    ue.try_set(options, "import_mesh", True)
    ue.try_set(options, "import_as_skeletal", False)
    mesh_type = ue.enum_value("FBXImportType", "FBXIT_STATIC_MESH")
    if mesh_type is not None:
        ue.try_set(options, "mesh_type_to_import", mesh_type)
        ue.try_set(options, "original_import_type", mesh_type)
    ue.try_set(options, "import_materials", False)
    ue.try_set(options, "import_textures", False)
    ue.try_set(options, "import_animations", False)
    ue.try_set(options, "create_physics_asset", False)
    data = options.get_editor_property("static_mesh_import_data")
    ue.try_set(data, "combine_meshes", combine)
    ue.try_set(data, "auto_generate_collision", False)
    ue.try_set(data, "one_convex_hull_per_ucx", True)
    ue.try_set(data, "generate_lightmap_u_vs", True)
    ue.try_set(data, "remove_degenerates", True)
    ue.try_set(data, "import_mesh_lo_ds", False)
    ue.try_set(data, "reorder_material_to_fbx_order", True)
    ue.try_set(data, "convert_scene", True)
    ue.try_set(data, "convert_scene_unit", True)
    ue.try_set(data, "force_front_x_axis", False)
    ue.try_set(data, "import_uniform_scale", 1.0)
    normals = ue.enum_value("FBXNormalImportMethod", "FBXNIM_IMPORT_NORMALS")
    if normals is not None:
        ue.try_set(data, "normal_import_method", normals)
    options.set_editor_property("static_mesh_import_data", data)
    return options


# Runs one import task into a content folder and returns the package paths it made or updated.
def run_task(filename, folder, name, options=None, factory=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name or "")
    task.set_editor_property("replace_existing", True)
    ue.try_set(task, "replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    if options is not None:
        task.set_editor_property("options", options)
    if factory is not None:
        task.set_editor_property("factory", factory)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = ue.try_get(task, "imported_object_paths", []) or []
    return [ue.package_path(path) for path in paths]


# Returns True when the file holds UCX_ collision meshes.
def has_collision_meshes(filename):
    try:
        with open(filename, "rb") as handle:
            return b"UCX_" in handle.read()
    except OSError:
        return False


# Returns the names of the expected meshes that are missing, or have no collision when the file has some.
def missing_meshes(folder, names, need_collision):
    missing = []
    for name in names:
        mesh = ue.load(folder + "/" + name)
        if not isinstance(mesh, unreal.StaticMesh):
            missing.append(name)
        elif need_collision and ue.mesh_tools().get_simple_collision_count(mesh) <= 0:
            missing.append(name)
    return missing


# Deletes what an import made beside the expected assets (materials or textures it brought from the file),
# leaving anything that was there before it.
def drop_side_products(made, expected, existed_before):
    for path in made:
        if path not in expected and path not in existed_before:
            if not ue.delete(path):
                ue.warn("could not delete %s, which the import made beside the meshes" % path)


# Imports an FBX file into a folder, as one mesh named name or as one mesh per node when name is None, over
# the same asset paths when they exist. Lets the engine pick its importer first (Interchange when the project
# uses it for FBX), and imports again with the classic FBX importer when expected meshes or their collision
# are missing. Returns which importer worked.
def import_fbx(filename, folder, name, expected_names):
    ue.make_folder(folder)
    expected = set(folder + "/" + item for item in expected_names)
    existed_before = set(ue.list_folder(folder, False))
    need_collision = has_collision_meshes(filename)
    combine = name is not None
    attempts = [("engine default", None)]
    if hasattr(unreal, "FbxFactory"):
        attempts.append(("classic FBX", unreal.FbxFactory))
    problems = []
    for label, factory_class in attempts:
        try:
            made = run_task(filename, folder, name, fbx_options(combine),
                            factory_class() if factory_class is not None else None)
        except Exception as error:
            problems.append("%s importer: %s" % (label, error))
            continue
        drop_side_products(made, expected, existed_before)
        missing = missing_meshes(folder, expected_names, need_collision)
        if not missing:
            ue.log("imported %s with the %s importer" % (filename, label))
            return label
        problems.append("%s importer left out or gave no collision to %s" % (label, ", ".join(missing[:5])))
        ue.warn(problems[-1])
    raise LinkError("import_failed", "Unreal Engine could not import %s: %s." % (filename, "; ".join(problems)))


# Imports one image as a texture asset over the same path when it exists, and returns the texture.
def import_texture(filename, folder, name):
    ue.make_folder(folder)
    path = folder + "/" + name
    existed_before = set(ue.list_folder(folder, False))
    # No factory is set, so the engine picks its texture importer, which handles every image format.
    made = run_task(filename, folder, name)
    drop_side_products(made, {path}, existed_before)
    texture = ue.load(path)
    if not isinstance(texture, unreal.Texture):
        raise LinkError("material_failed", "Unreal Engine could not import the texture %s." % filename)
    return texture
