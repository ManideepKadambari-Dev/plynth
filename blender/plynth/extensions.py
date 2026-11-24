import glob
import os
import re
import shutil

import bpy

ENV_NAME = "BLENDER_USER_EXTENSIONS"
SOURCE_ENV_NAME = "PLYNTH_TEST_ADDONS"
MARKER = ".plynth-prepared"
KNOWN_ADDONS = ("archimesh", "asset_optimizer", "bagapie", "bool_tool", "home_builder_5")
USER_FOLDERS = (("BLENDER_USER_CONFIG", "config"), ("BLENDER_USER_SCRIPTS", "scripts"),
                (ENV_NAME, "extensions"), ("BLENDER_USER_DATAFILES", "datafiles"))
# The other variables that can name a folder of the user, and the folder each gets in a child's root.
PRIVATE_FOLDERS = (("APPDATA", "appdata"), ("LOCALAPPDATA", "local"), ("USERPROFILE", "home"), ("HOME", "home"),
                   ("XDG_DATA_HOME", "xdg"), ("PLYNTH_BLENDER_PRIVATE", "plynth-private"),
                   ("PLYNTH_CONFIG_DIR", "plynth-config"))
USER_FOLDER_NAME = "blender foundation"


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


# Says whether the path holds one of the folders, judged from the text of the paths only.
def holds(path, folders):
    return any(folder and is_inside(folder, [path]) for folder in folders)


# Says whether any folder in the path is named Blender Foundation, in any case, with dots and spaces at its end
# and a stream name after a colon ignored, judged from the text alone. This holds whatever APPDATA says, because
# a run with a scratch APPDATA would hide the user's real folder from every other check.
def names_user_folder(path):
    text = os.fsdecode(os.fspath(path))
    for candidate in (text, os.path.abspath(text)):
        for part in re.split(r"[\\/]", candidate):
            if part.split(":")[0].rstrip(". ").lower() == USER_FOLDER_NAME:
                return True
    return False


# Raises when a folder is a Blender user folder by its name, before anything on disk is looked at.
def refuse_by_name(folder, message):
    if names_user_folder(folder):
        raise ValueError(message % folder)


# Raises when a folder, as written or with links followed, is named as a user folder, lies in one of the
# forbidden folders or holds one. Links are followed only after the text alone is known to be allowed.
def refuse_folder(folder, forbidden, message):
    refuse_by_name(folder, message)
    real = os.path.realpath(folder)
    if names_user_folder(real):
        raise ValueError(message % folder)
    for path in (folder, real):
        if is_inside(path, forbidden) or holds(path, forbidden):
            raise ValueError(message % folder)


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
    if not folder:
        return False
    try:
        refuse_folder(folder, own_folders(), "%s")
    except ValueError:
        return False
    return os.path.isfile(os.path.join(folder, MARKER))


# Returns how many files the bundled libraries of an extensions folder hold, so a run can show it left them alone.
def shared_library_count(folder):
    pattern = os.path.join(glob.escape(folder), ".local", "lib", "python*", "site-packages")
    return sum(len(files) for library in glob.glob(pattern) for _, _, files in os.walk(library))


# Raises when add-ons must not be copied from the folder: the user's own, one this Blender runs in, or one that
# holds either. Links are followed only after the path itself is known to be allowed.
def check_source(folder):
    refuse_folder(folder, own_folders() + running_folders(),
                  "Add-ons are not copied from a Blender user folder or a private one: %s")


# Makes folder a prepared extensions folder: copies the named extensions from source into folder/user_default,
# marks the folder and returns the names copied. Source is only read and has no default, so no run can reach
# the user's add-ons by accident. Without a source the folder is prepared empty.
def prepare(folder, source=None, short_names=KNOWN_ADDONS):
    target_message = "A folder Blender already uses cannot be prepared: %s"
    refuse_by_name(folder, target_message)
    if source:
        refuse_by_name(source, "Add-ons are not copied from a Blender user folder or a private one: %s")
    refuse_folder(folder, own_folders() + running_folders(), target_message)
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


# Returns the short names of the known add-ons that are not among the names copied.
def missing_addons(copied, short_names=KNOWN_ADDONS):
    found = {name.lower() for name in copied}
    return [name for name in short_names if name.lower() not in found]


# Returns the environment for a Blender process that has all five user folders and every other folder of the
# user inside root, making the folders first because Blender ignores a variable whose folder is missing. Other
# Blender user variables are dropped. Its extensions folder is root/extensions.
def environment(root):
    env = {name: value for name, value in os.environ.items() if not name.upper().startswith("BLENDER_USER_")}
    os.makedirs(root, exist_ok=True)
    env["BLENDER_USER_RESOURCES"] = root
    for name, part in USER_FOLDERS + PRIVATE_FOLDERS:
        os.makedirs(os.path.join(root, part), exist_ok=True)
        env[name] = os.path.join(root, part)
    return env
