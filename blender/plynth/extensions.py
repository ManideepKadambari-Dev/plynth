import glob
import os
import shutil

import bpy

ENV_NAME = "BLENDER_USER_EXTENSIONS"
KNOWN_ADDONS = ("archimesh", "asset_optimizer", "bagapie", "bool_tool", "home_builder_5")


# Returns the extensions folder this Blender uses for the current user.
def user_extensions_folder():
    return bpy.utils.user_resource("EXTENSIONS")


# Says whether this run uses a job-private extensions folder instead of the shared one.
def is_private():
    return bool(os.environ.get(ENV_NAME))


# Returns the shared folders where the extensions' bundled libraries live.
def shared_library_folders():
    root = user_extensions_folder()
    return glob.glob(os.path.join(root, ".local", "lib", "python*", "site-packages"))


# Returns how many files the shared libraries folders hold, so a run can show it left them alone.
def shared_library_count():
    return sum(len(files) for folder in shared_library_folders() for _, _, files in os.walk(folder))


# Copies the named extensions from the shared folder into folder/user_default and returns the names copied.
# The shared folder is only read.
def prepare(folder, short_names=KNOWN_ADDONS):
    wanted = {name.lower() for name in short_names}
    target = os.path.join(folder, "user_default")
    os.makedirs(target, exist_ok=True)
    copied = []
    root = user_extensions_folder()
    for repo in sorted(glob.glob(os.path.join(root, "*"))):
        if not os.path.isdir(repo) or os.path.basename(repo).startswith("."):
            continue
        for source in sorted(glob.glob(os.path.join(repo, "*"))):
            name = os.path.basename(source)
            if name.lower() in wanted and name not in copied and os.path.isfile(os.path.join(source, "blender_manifest.toml")):
                shutil.copytree(source, os.path.join(target, name), ignore=shutil.ignore_patterns("__pycache__"), dirs_exist_ok=True)
                copied.append(name)
    return copied


# Returns the environment for a Blender process that must use the private folder.
def environment(folder):
    env = dict(os.environ)
    env[ENV_NAME] = folder
    return env
