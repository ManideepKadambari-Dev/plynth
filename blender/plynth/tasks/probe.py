import ast
import json
import os
import platform
import tomllib

import bpy

from ..jobs import WorkerError
from ..registry import TASKS
from ..userprefs import (BLENDERKIT_MODULE, PreferencesError, Repository, UNREADABLE_MESSAGE, read_preferences,
                         short_name)

EXTENSION_PREFIX = "bl_ext."
FOLDER_KEYS = ("config", "scripts", "extensions", "blenderkit_data")
MAX_SOURCE_BYTES = 8 * 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024
DEFAULT_REPOSITORIES = (("blender_org", False), ("user_default", False), ("system", True))


# Reads the user's folders from the job. There are no defaults, so a missing or relative one is a bad job.
def user_folders(params):
    given = params.get("user_folders")
    if not isinstance(given, dict):
        raise WorkerError("bad_job", "The probe job has no user folders.")
    folders = {}
    for key in FOLDER_KEYS:
        value = given.get(key)
        if not isinstance(value, str) or not value or not os.path.isabs(value):
            raise WorkerError("bad_job", "The probe job has no usable '%s' user folder." % key)
        folders[key] = value
    return folders


# Lists the names in a folder with one directory listing, or an empty set when it cannot be listed.
def listing(folder, cache):
    key = os.path.normcase(os.path.abspath(folder))
    if key not in cache:
        try:
            with os.scandir(folder) as entries:
                cache[key] = {entry.name for entry in entries}
        except OSError:
            cache[key] = set()
    return cache[key]


# Joins a version given as numbers or text into dotted text.
def version_text(version):
    if isinstance(version, (tuple, list)):
        return ".".join(str(part) for part in version)
    return str(version or "")


# Reads bl_info from a legacy add-on source file without importing it. Gives (name, version), empty when unreadable.
def read_bl_info(path):
    try:
        if os.path.getsize(path) > MAX_SOURCE_BYTES:
            return "", ""
        with open(path, "rb") as handle:
            tree = ast.parse(handle.read())
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "bl_info" for t in node.targets):
                info = ast.literal_eval(node.value)
                return str(info.get("name") or ""), version_text(info.get("version"))
    except Exception:
        pass
    return "", ""


# Reads name and version from an extension's manifest. Returns None when there is no usable manifest.
def read_manifest(path):
    try:
        if os.path.getsize(path) > MAX_MANIFEST_BYTES:
            return None
        with open(path, "rb") as handle:
            data = tomllib.load(handle)
        return str(data.get("name") or ""), version_text(data.get("version"))
    except Exception:
        return None


# The folders Blender itself searches for bundled add-ons, leaving out the probe's own private folder.
def system_addon_folders():
    private = os.path.normcase(os.path.abspath(bpy.utils.resource_path("USER")))
    found = []
    try:
        import addon_utils
        for folder in addon_utils.paths():
            if not os.path.normcase(os.path.abspath(folder)).startswith(private):
                found.append(folder)
    except Exception:
        pass
    return found


# The folder of Blender's own extensions repository.
def system_extensions_folder():
    try:
        folder = bpy.utils.system_resource("EXTENSIONS")
        if folder:
            return folder
    except Exception:
        pass
    return os.path.join(bpy.utils.resource_path("SYSTEM"), "extensions")


# Builds the repository list Blender starts with, for a user who has no saved settings.
def default_repositories():
    return [Repository(module, True, system, "") for module, system in DEFAULT_REPOSITORIES]


# Works out the folder a repository keeps its extensions in.
def repository_folder(repository, folders):
    if repository.custom_folder:
        return repository.custom_folder
    if repository.system:
        return os.path.join(system_extensions_folder(), repository.module)
    return os.path.join(folders["extensions"], repository.module)


class Finder:
    # Looks for add-ons and extensions on disk by name, one directory listing for each folder.
    def __init__(self, prefs, folders):
        self.cache = {}
        self.repositories = prefs.repositories if prefs.exists else default_repositories()
        self.folders = folders
        self.addon_folders = [os.path.join(folders["scripts"], "addons")]
        self.addon_folders += [os.path.join(extra, "addons") for extra in prefs.script_folders]
        self.addon_folders += system_addon_folders()

    # Finds an extension in its repository. Returns (name, version) or None.
    def extension(self, repo_module, extension_id):
        for repository in self.repositories:
            if repository.module != repo_module or not repository.enabled:
                continue
            folder = repository_folder(repository, self.folders)
            if extension_id in listing(folder, self.cache):
                found = read_manifest(os.path.join(folder, extension_id, "blender_manifest.toml"))
                if found is not None:
                    return found
        return None

    # Finds a legacy add-on, a single file or a package, in the add-on folders. Returns (name, version) or None.
    def legacy(self, module):
        for folder in self.addon_folders:
            names = listing(folder, self.cache)
            if module + ".py" in names:
                return read_bl_info(os.path.join(folder, module + ".py"))
            if module in names and os.path.isfile(os.path.join(folder, module, "__init__.py")):
                return read_bl_info(os.path.join(folder, module, "__init__.py"))
        return None

    # Finds an enabled module by its full name.
    def find(self, module):
        if module.startswith(EXTENSION_PREFIX):
            parts = module[len(EXTENSION_PREFIX):].split(".")
            return self.extension(parts[0], parts[1]) if len(parts) == 2 else None
        return self.legacy(module)

    # Says whether BlenderKit is on disk, enabled or not.
    def blenderkit_on_disk(self):
        if self.legacy(BLENDERKIT_MODULE) is not None:
            return True
        return any(self.extension(repository.module, BLENDERKIT_MODULE) is not None
                   for repository in self.repositories)


# Says whether the BlenderKit sign-in file holds a key. Only true or false comes out, and the file is left alone.
def json_sign_in(folder):
    path = os.path.join(folder, "config", "preferences.json")
    try:
        if os.path.getsize(path) > MAX_JSON_BYTES:
            return False
        with open(path, "rb") as handle:
            data = json.loads(handle.read())
        key = data.get("api_key") if isinstance(data, dict) else None
        result = isinstance(key, str) and len(key.strip()) > 0
        del key, data
        return result
    except Exception:
        return False


# Works out whether BlenderKit has a saved sign-in, from the settings first and the JSON file second.
def blenderkit_signed_in(prefs, folders):
    if prefs.blenderkit_has_key:
        return True
    given = prefs.blenderkit_global_dir
    folder = given if given and os.path.isabs(given) else folders["blenderkit_data"]
    return json_sign_in(folder)


# Reports the Blender version, the enabled add-ons found on disk and the state of BlenderKit, all read as data.
def run(ctx):
    folders = user_folders(ctx.params)
    try:
        prefs = read_preferences(folders["config"])
    except PreferencesError:
        raise WorkerError("preferences_unreadable", UNREADABLE_MESSAGE) from None

    if not prefs.exists:
        prefs.addons = list(bpy.context.preferences.addons.keys())
    finder = Finder(prefs, folders)
    addons = []
    kit_enabled = False
    for module in sorted(set(prefs.addons)):
        found = finder.find(module)
        if found is None:
            continue
        addons.append({"module": module, "name": found[0], "version": found[1]})
        if short_name(module) == BLENDERKIT_MODULE:
            kit_enabled = True

    kit_installed = kit_enabled or finder.blenderkit_on_disk()
    kit_signed_in = kit_enabled and blenderkit_signed_in(prefs, folders)

    build_date = bpy.app.build_date
    if isinstance(build_date, bytes):
        build_date = build_date.decode("utf-8", "replace")

    ctx.output = {
        "blender": {
            "version": ".".join(str(part) for part in bpy.app.version),
            "version_text": str(bpy.app.version_string),
            "python": platform.python_version(),
            "build_date": str(build_date),
        },
        "addons": addons,
        "blenderkit": {
            "installed": kit_installed,
            "enabled": kit_enabled,
            "signed_in": kit_signed_in,
        },
    }


TASKS.register("probe", run)
