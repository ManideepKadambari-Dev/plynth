import glob
import os
import shutil

import bpy

ENV_NAME = "BLENDER_USER_EXTENSIONS"
SOURCE_ENV_NAME = "PLYNTH_TEST_ADDONS"
MARKER = ".plynth-prepared"
KNOWN_ADDONS = ("archimesh", "asset_optimizer", "bagapie", "bool_tool", "home_builder_5")
USER_FOLDERS = (("BLENDER_USER_CONFIG", "config"), ("BLENDER_USER_SCRIPTS", "scripts"),
                (ENV_NAME, "extensions"), ("BLENDER_USER_DATAFILES", "datafiles"))


# Says whether a path is one of the folders or inside one, judged from the text of the paths only.
def is_inside(path, folders):
    wanted = os.path.normcase(os.path.abspath(path))
    for folder in folders:
        if not folder:
            continue
        root = os.path.normcase(os.path.abspath(folder)).rstrip(os.sep)
        if wanted == root or wanted.startswith(root + os.sep):
            return True
    return False


# Returns where the user's own Blender folders are, without looking inside them.
def own_folders():
    found = [os.path.join(os.path.dirname(bpy.app.binary_path), "portable")]
    if os.environ.get("APPDATA"):
        found.append(os.path.join(os.environ["APPDATA"], "Blender Foundation"))
    return found


# Returns the folders this run of Blender uses as its user folders.
def running_folders():
    return [bpy.utils.resource_path("USER"), bpy.utils.user_resource("EXTENSIONS")]


# Says whether this run's extensions folder was prepared for it. A folder that is only private does not count,
# and the variable alone says nothing, because every job sets it.
def is_private():
    folder = os.environ.get(ENV_NAME)
    return bool(folder) and not is_inside(folder, own_folders()) and os.path.isfile(os.path.join(folder, MARKER))


# Returns how many files the bundled libraries of an extensions folder hold, so a run can show it left them alone.
def shared_library_count(folder):
    pattern = os.path.join(glob.escape(folder), ".local", "lib", "python*", "site-packages")
    return sum(len(files) for library in glob.glob(pattern) for _, _, files in os.walk(library))


# Raises when add-ons must not be copied from the folder: the user's own or one this Blender runs in. Links are
# followed only after the path itself is known to be allowed.
def check_source(folder):
    forbidden = own_folders() + running_folders()
    if is_inside(folder, forbidden) or is_inside(os.path.realpath(folder), forbidden):
        raise ValueError("Add-ons are not copied from a Blender user folder or a private one: %s" % folder)


# Makes folder a prepared extensions folder: copies the named extensions from source into folder/user_default,
# marks the folder and returns the names copied. Source is only read and has no default, so no run can reach
# the user's add-ons by accident. Without a source the folder is prepared empty.
def prepare(folder, source=None, short_names=KNOWN_ADDONS):
    if is_inside(folder, own_folders() + running_folders()):
        raise ValueError("A folder Blender already uses cannot be prepared: %s" % folder)
    if source:
        check_source(source)
        if os.path.isfile(os.path.join(source, MARKER)) or not os.path.isdir(source):
            raise ValueError("The folder to copy add-ons from is missing or is itself prepared: %s" % source)
    target = os.path.join(folder, "user_default")
    os.makedirs(target, exist_ok=True)
    copied = copy_addons(source, target, short_names) if source else []
    with open(os.path.join(folder, MARKER), "w", encoding="utf-8"):
        pass
    return copied


# Copies the named extensions of every repository folder in source into target and returns the names copied.
def copy_addons(source, target, short_names):
    wanted = {name.lower() for name in short_names}
    copied = []
    for repo in sorted(glob.glob(os.path.join(glob.escape(source), "*"))):
        check_source(repo)
        if not os.path.isdir(repo) or os.path.basename(repo).startswith("."):
            continue
        for addon in sorted(glob.glob(os.path.join(glob.escape(repo), "*"))):
            name = os.path.basename(addon)
            if name.lower() not in wanted or name in copied:
                continue
            check_source(addon)
            if os.path.isfile(os.path.join(addon, "blender_manifest.toml")):
                shutil.copytree(addon, os.path.join(target, name), ignore=shutil.ignore_patterns("__pycache__"),
                                dirs_exist_ok=True)
                copied.append(name)
    return copied


# Returns the environment for a Blender process that has all five user folders inside root, making the folders
# first because Blender ignores a variable whose folder is missing. Its extensions folder is root/extensions.
def environment(root):
    env = dict(os.environ)
    os.makedirs(root, exist_ok=True)
    env["BLENDER_USER_RESOURCES"] = root
    for name, part in USER_FOLDERS:
        os.makedirs(os.path.join(root, part), exist_ok=True)
        env[name] = os.path.join(root, part)
    return env
