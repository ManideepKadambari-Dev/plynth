import os

import unreal

TAG_BUILDING = "PlynthBuilding"
TAG_PROJECT = "PlynthProject"
TAG_JOB = "PlynthJob"
TAG_SOURCE = "PlynthSource"


# Returns the editor's asset subsystem.
def assets():
    return unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


# Returns the editor's actor subsystem.
def actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


# Returns the static mesh editing subsystem.
def mesh_tools():
    return unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)


# Returns the world open in the level editor.
def editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


# Writes one line to the editor's output log.
def log(message):
    unreal.log("Plynth: %s" % message)


# Writes one warning line to the editor's output log.
def warn(message):
    unreal.log_warning("Plynth: %s" % message)


# Returns the engine version as major.minor.patch, for example 5.8.0.
def engine_version():
    try:
        full = str(unreal.SystemLibrary.get_engine_version())
    except Exception:
        return ""
    return full.split("-")[0].split("+")[0]


# Returns the name of the open project.
def project_name():
    try:
        name = str(unreal.SystemLibrary.get_game_name())
        if name:
            return name
    except Exception:
        pass
    try:
        return os.path.splitext(os.path.basename(str(unreal.Paths.get_project_file_path())))[0]
    except Exception:
        return ""


# Returns the project's Content folder on disk, with forward slashes and no trailing slash.
def content_folder_on_disk():
    folder = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    return str(folder).replace("\\", "/").rstrip("/")


# Returns the folder on disk that a /Game path stands for.
def disk_path(game_path):
    relative = game_path[len("/Game"):].lstrip("/")
    return content_folder_on_disk() + ("/" + relative if relative else "")


# Returns the package path of an object path, for example /Game/A/B.B becomes /Game/A/B.
def package_path(path):
    path = str(path)
    tail = path.rsplit("/", 1)[-1]
    if "." in tail:
        path = path[: len(path) - len(tail)] + tail.split(".", 1)[0]
    return path


# Returns True when an asset exists at the package path.
def exists(path):
    return bool(assets().does_asset_exist(path))


# Loads the asset at the package path, or returns None.
def load(path):
    if not exists(path):
        return None
    return assets().load_asset(path)


# Makes a content folder when it is missing.
def make_folder(path):
    tool = assets()
    if not tool.does_directory_exist(path):
        tool.make_directory(path)


# Returns the package paths of every asset under a content folder.
def list_folder(path, recursive=True):
    tool = assets()
    if not tool.does_directory_exist(path):
        return []
    found = tool.list_assets(path, recursive, False)
    return sorted(set(package_path(item) for item in found))


# Deletes the asset at the package path. Returns True when it is gone.
def delete(path):
    if not exists(path):
        return True
    return bool(assets().delete_asset(path))


# Deletes a content folder when no asset is left in it.
def delete_folder_if_empty(path):
    tool = assets()
    if tool.does_directory_exist(path) and not list_folder(path):
        tool.delete_directory(path)


# Returns the value of a metadata tag on an asset, or an empty text.
def get_tag(asset, tag):
    try:
        return str(assets().get_metadata_tag(asset, tag) or "")
    except Exception:
        return ""


# Sets a metadata tag on an asset.
def set_tag(asset, tag, value):
    assets().set_metadata_tag(asset, tag, value)


# Returns the result's kind for an asset: static_mesh, texture, material, material_instance or file.
def kind_of(asset):
    if isinstance(asset, unreal.StaticMesh):
        return "static_mesh"
    if isinstance(asset, unreal.MaterialInstanceConstant):
        return "material_instance"
    if isinstance(asset, unreal.Material):
        return "material"
    if isinstance(asset, unreal.Texture):
        return "texture"
    return "file"


# Saves every changed asset under a content folder.
def save_folder(path):
    tool = assets()
    try:
        tool.save_directory(path, True, True)
        return
    except Exception as error:
        warn("saving the folder at once did not work (%s), saving each asset" % error)
    for item in list_folder(path):
        asset = load(item)
        if asset is not None:
            tool.save_loaded_asset(asset, True)


# Sets an editor property and returns True, or logs and returns False when this engine has no such property.
def try_set(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as error:
        warn("could not set %s on %s: %s" % (name, type(obj).__name__, error))
        return False


# Returns an editor property, or the fallback when this engine has no such property.
def try_get(obj, name, fallback=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return fallback


# Returns an enum value by name, or None when this engine does not have it.
def enum_value(enum_name, value_name):
    enum = getattr(unreal, enum_name, None)
    return getattr(enum, value_name, None) if enum is not None else None
