"""Makes a Blender user config with the fake BlenderKit add-on installed, for tests and for the scratch runs.

    python make_fake_blenderkit_config.py <folder> [--signed-out] [--disabled] [--missing] [--blender <exe>]

The folder gets config/, scripts/, extensions/, download/ and the other folders of a made-up user. Start Blender
with the environment that child_env(folder) returns and it sees the fake add-on and never the user's own
settings. A folder in or around the user's own Blender, BlenderKit or Plynth folders is refused. The script can
also be imported (make_config).
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FAKE_ADDON = os.path.join(HERE, "..", "data", "fake_blenderkit", "blenderkit")
FAKE_KEY = "fake-api-key-do-not-leak-0123456789"
# Where each variable of a child points inside the folder. Blender ignores one whose folder is missing.
PRIVATE_FOLDERS = {
    "BLENDER_USER_RESOURCES": "resources",
    "BLENDER_USER_CONFIG": "config",
    "BLENDER_USER_SCRIPTS": "scripts",
    "BLENDER_USER_EXTENSIONS": "extensions",
    "BLENDER_USER_DATAFILES": "datafiles",
    "APPDATA": "appdata",
    "LOCALAPPDATA": "local",
    "USERPROFILE": "home",
    "HOME": "home",
    "XDG_DATA_HOME": "xdg",
    "PLYNTH_BLENDER_PRIVATE": "plynth_private",
    "PLYNTH_CONFIG_DIR": "plynth_config",
}


# Lists the user's own folders by name from the environment. Never looks at them on disk.
def real_user_folders():
    places = (("APPDATA", "Blender Foundation"), ("APPDATA", "Plynth"), ("LOCALAPPDATA", "Blender Foundation"),
              ("LOCALAPPDATA", "Plynth"), ("USERPROFILE", "blenderkit_data"), ("XDG_DATA_HOME", "blenderkit_data"))
    found = [os.path.join(os.environ[name], part) for name, part in places if os.environ.get(name)]
    found += [value for name, value in os.environ.items() if name.startswith("BLENDER_USER_") and value]
    return found


# Returns the folder as an absolute path. Refuses one inside or around the user's own folders.
def private_root(folder):
    root = os.path.abspath(folder)
    given = os.path.join(os.path.normcase(root), "")
    for path in real_user_folders():
        own = os.path.join(os.path.normcase(os.path.abspath(path)), "")
        if given.startswith(own) or own.startswith(given):
            raise ValueError("The fake config needs a scratch folder, away from the user's own folders.")
    return root


# Makes the private folders in folder and returns an environment that points every user variable at them.
def child_env(folder):
    root = private_root(folder)
    env = {name: value for name, value in os.environ.items() if not name.upper().startswith("BLENDER_USER_")}
    for name, part in PRIVATE_FOLDERS.items():
        env[name] = os.path.join(root, part)
        os.makedirs(env[name], exist_ok=True)
    return env


# Finds a Blender executable: the running one, the given path, PLYNTH_BLENDER, the installed copies, or PATH.
def find_blender(given=None):
    try:
        import bpy
        if bpy.app.binary_path:
            return bpy.app.binary_path
    except ImportError:
        pass
    if given:
        return given
    if os.environ.get("PLYNTH_BLENDER"):
        return os.environ["PLYNTH_BLENDER"]
    for base in (os.environ.get("ProgramFiles", ""), os.environ.get("ProgramFiles(x86)", "")):
        found = sorted(glob.glob(os.path.join(base, "Blender Foundation", "Blender *", "blender.exe")))
        if found:
            return found[-1]
    return shutil.which("blender")


# Makes the config by starting Blender once with the new folders and saving its preferences. Returns the
# environment to use afterwards.
def make_config(folder, signed_out=False, disabled=False, missing=False, blender=None):
    folder = private_root(folder)
    for name in ("config", "scripts", "extensions", "download"):
        shutil.rmtree(os.path.join(folder, name), ignore_errors=True)
        os.makedirs(os.path.join(folder, name))
    if not missing:
        shutil.copytree(FAKE_ADDON, os.path.join(folder, "scripts", "addons", "blenderkit"),
                        ignore=shutil.ignore_patterns("__pycache__"))
    exe = find_blender(blender)
    if not exe:
        raise RuntimeError("Blender was not found. Pass --blender or set PLYNTH_BLENDER.")
    options = {"signed_out": signed_out, "disabled": disabled or missing, "download": os.path.join(folder, "download")}
    command = [exe, "-b", "--python", os.path.abspath(__file__), "--", "--apply", json.dumps(options)]
    env = child_env(folder)
    done = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, timeout=180)
    if done.returncode != 0 or "FAKE CONFIG SAVED" not in done.stdout:
        raise RuntimeError("Making the fake config failed:\n" + done.stdout[-2000:])
    return env


# Runs inside Blender: enables the fake add-on, sets its preferences and saves the user preferences.
def apply(options):
    import addon_utils
    import bpy
    if not options["disabled"]:
        addon_utils.enable("blenderkit", default_set=True, persistent=True)
        preferences = bpy.context.preferences.addons["blenderkit"].preferences
        preferences.global_dir = options["download"]
        if not options["signed_out"]:
            preferences.api_key = FAKE_KEY
    bpy.ops.wm.save_userpref()
    print("FAKE CONFIG SAVED")


# Reads the command line, either as a plain script or inside Blender.
def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if argv and argv[0] == "--apply":
        apply(json.loads(argv[1]))
        return
    parser = argparse.ArgumentParser(prog="make_fake_blenderkit_config.py")
    parser.add_argument("folder")
    parser.add_argument("--signed-out", action="store_true")
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--missing", action="store_true")
    parser.add_argument("--blender")
    options = parser.parse_args(argv)
    make_config(options.folder, options.signed_out, options.disabled, options.missing, options.blender)
    print("Fake BlenderKit config in %s" % os.path.abspath(options.folder))


if __name__ == "__main__":
    main()
