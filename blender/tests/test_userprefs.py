import atexit
import builtins
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

import helpers  # noqa: F401  (puts the worker folder on the path)
from plynth import userprefs

FAKE_KEY = "FAKEKEY-q7Zr4mXw90pLtVb2"
MARKER_VARIABLE = "PLYNTH_FIXTURE_MARKER"
# Where each variable of a child points inside its folder. Blender ignores one whose folder is missing.
PRIVATE_FOLDERS = {
    "BLENDER_USER_RESOURCES": "resources",
    "BLENDER_USER_CONFIG": "config",
    "BLENDER_USER_SCRIPTS": "scripts",
    "BLENDER_USER_EXTENSIONS": "extensions",
    "BLENDER_USER_DATAFILES": "resources/datafiles",
    "APPDATA": "appdata",
    "LOCALAPPDATA": "local",
    "USERPROFILE": "home",
    "HOME": "home",
    "XDG_DATA_HOME": "xdg",
    "PLYNTH_BLENDER_PRIVATE": "plynth_private",
    "PLYNTH_CONFIG_DIR": "plynth_config",
}

LEGACY_SOURCE = (
    'bl_info = {"name": "Fixture Legacy", "version": (1, 2, 3), "blender": (4, 2, 0), "category": "Development"}\n'
    "def register(): pass\n"
    "def unregister(): pass\n"
)
EXTRA_SOURCE = LEGACY_SOURCE.replace("Fixture Legacy", "Fixture Extra")
DISABLED_SOURCE = LEGACY_SOURCE.replace("Fixture Legacy", "Fixture Disabled")
MARKER_SOURCE = (
    "import os\n"
    'bl_info = {"name": "Fixture Marker", "version": (0, 1, 0), "blender": (4, 2, 0), "category": "Development"}\n'
    'path = os.environ.get("%s")\n'
    "if path:\n"
    '    with open(path, "w") as handle:\n'
    '        handle.write("imported")\n'
    "def register(): pass\n"
    "def unregister(): pass\n"
) % MARKER_VARIABLE
KIT_SOURCE = (
    "import bpy\n"
    'bl_info = {"name": "BlenderKit Fake", "version": (3, 0, 0), "blender": (4, 2, 0), "category": "Development"}\n'
    "class Prefs(bpy.types.AddonPreferences):\n"
    "    bl_idname = __package__\n"
    '    api_key: bpy.props.StringProperty(name="key", default="")\n'
    '    global_dir: bpy.props.StringProperty(name="dir", default="")\n'
    "def register(): bpy.utils.register_class(Prefs)\n"
    "def unregister(): bpy.utils.unregister_class(Prefs)\n"
)
MANIFEST = (
    'schema_version = "1.0.0"\n'
    'id = "%(id)s"\n'
    'version = "%(version)s"\n'
    'name = "%(name)s"\n'
    'tagline = "Fixture"\n'
    'maintainer = "Test <t@example.com>"\n'
    'type = "add-on"\n'
    'blender_version_min = "4.2.0"\n'
    'license = ["SPDX:GPL-3.0-or-later"]\n'
)
EXTENSION_SOURCE = "def register(): pass\ndef unregister(): pass\n"

CHILD_SCRIPT = '''
import json, os, sys
import addon_utils
import bpy

spec = json.loads(os.environ["PLYNTH_FIXTURE"])
prefs = bpy.context.preferences
for repo in spec["repos"]:
    prefs.extensions.repos.new(name=repo["name"], module=repo["module"], custom_directory=repo["dir"])
for folder in spec["script_dirs"]:
    entry = prefs.filepaths.script_directories.new()
    entry.name = "Extra"
    entry.directory = folder
    sys.path.append(os.path.join(folder, "addons"))
for module in spec["enable"]:
    if addon_utils.enable(module, default_set=True, persistent=True) is None:
        raise SystemExit("could not enable " + module)
if spec["key"]:
    prefs.addons["blenderkit"].preferences.api_key = spec["key"]
if spec["global_dir"]:
    prefs.addons["blenderkit"].preferences.global_dir = spec["global_dir"]
for module in spec["disable_repos"]:
    for repo in prefs.extensions.repos:
        if repo.module == module:
            repo.enabled = False
bpy.ops.wm.save_userpref()
'''

_scratch = tempfile.mkdtemp(prefix="plynth_prefs_")
atexit.register(shutil.rmtree, _scratch, True)
_fixtures = {}


# Writes a text file, making its folder first.
def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


# Lists every file and folder under a root with its size and write time, without reading any content.
def fingerprint(root):
    found = []
    for folder, folders, files in os.walk(root):
        folders.sort()
        for name in sorted(files) + folders:
            path = os.path.join(folder, name)
            info = os.stat(path)
            found.append((os.path.relpath(path, root), info.st_size, info.st_mtime_ns))
    return found


# Makes a made-up user's folders under root and returns an environment that points every user variable at them.
def private_env(root):
    env = {name: value for name, value in os.environ.items() if not name.upper().startswith("BLENDER_USER_")}
    for name, part in PRIVATE_FOLDERS.items():
        env[name] = os.path.join(root, part).replace("\\", "/")
        os.makedirs(env[name], exist_ok=True)
    return env


class Fixture:
    # A fake user area saved by a child Blender: config, scripts, extensions, a custom repository and the rest.
    def __init__(self, name):
        self.root = os.path.join(_scratch, name).replace("\\", "/")
        self.config = self.path("config")
        self.scripts = self.path("scripts")
        self.extensions = self.path("extensions")
        self.custom_repo = self.path("custom_repo")
        self.extra_scripts = self.path("extra_scripts")
        self.global_dir = self.path("bkdata")
        self.marker = self.path("marker.txt")

    def path(self, *parts):
        return os.path.join(self.root, *parts).replace("\\", "/")

    @property
    def user_folders(self):
        return {"config": self.config, "scripts": self.scripts, "extensions": self.extensions,
                "blenderkit_data": self.global_dir}

    def write_sources(self):
        addons = self.path("scripts", "addons")
        write_text(os.path.join(addons, "fixture_legacy.py"), LEGACY_SOURCE)
        write_text(os.path.join(addons, "fixture_disabled.py"), DISABLED_SOURCE)
        write_text(os.path.join(addons, "fixture_marker.py"), MARKER_SOURCE)
        write_text(os.path.join(addons, "blenderkit", "__init__.py"), KIT_SOURCE)
        write_text(os.path.join(self.extra_scripts, "addons", "fixture_extra.py"), EXTRA_SOURCE)
        for folder, extension_id, name, version in (
                (self.path("extensions", "user_default"), "fixture_ext", "Fixture Extension", "2.0.1"),
                (self.custom_repo, "fixture_ext2", "Fixture Custom", "0.9.0")):
            base = os.path.join(folder, extension_id)
            write_text(os.path.join(base, "blender_manifest.toml"),
                       MANIFEST % {"id": extension_id, "version": version, "name": name})
            write_text(os.path.join(base, "__init__.py"), EXTENSION_SOURCE)

    def remove_sources(self):
        for folder in (self.path("scripts", "addons"), self.path("extensions", "user_default"),
                       self.custom_repo, self.extra_scripts):
            shutil.rmtree(folder, True)

    # Saves the settings with a child Blender that sees only this fixture's folders.
    def save(self, enable=(), key=None, global_dir="", repos=(), script_dirs=(), disable_repos=()):
        import bpy
        self.write_sources()
        os.makedirs(self.global_dir, exist_ok=True)
        spec = {"enable": list(enable), "key": key, "global_dir": global_dir, "repos": list(repos),
                "script_dirs": list(script_dirs), "disable_repos": list(disable_repos)}
        script = os.path.join(_scratch, "make_fixture.py")
        write_text(script, CHILD_SCRIPT)
        env = private_env(self.root)
        env.update({"PLYNTH_FIXTURE": json.dumps(spec), MARKER_VARIABLE: self.marker})
        done = subprocess.run(
            [bpy.app.binary_path, "-t", "6", "--offline-mode", "--background", "--factory-startup", "-noaudio",
             "--python-exit-code", "3", "--python", script],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        text = done.stdout.decode("utf-8", "replace")
        if done.returncode != 0 or not os.path.isfile(os.path.join(self.config, "userpref.blend")):
            raise RuntimeError("The fixture could not be saved:\n" + text[-2000:])
        if FAKE_KEY in text:
            raise RuntimeError("The made-up key reached the child output.")
        if os.path.exists(self.marker):
            os.remove(self.marker)
        return self


# The variants the tests share. Each is saved once, by one child Blender.
def variant(name):
    if name in _fixtures:
        return _fixtures[name]
    made = Fixture(name)
    kit = "blenderkit"
    legacy = "fixture_legacy"
    ext = "bl_ext.user_default.fixture_ext"
    if name == "signed_in":
        made.save(enable=[legacy, ext, kit], key=FAKE_KEY, global_dir=made.global_dir)
    elif name == "signed_out":
        made.save(enable=[legacy, kit])
    elif name == "kit_disabled":
        made.save(enable=[legacy])
    elif name == "missing":
        made.save(enable=[legacy, ext, kit], key=FAKE_KEY)
        made.remove_sources()
    elif name == "custom_repo":
        made.save(enable=["bl_ext.custom_repo.fixture_ext2"],
                  repos=[{"name": "Custom Repo", "module": "custom_repo", "dir": made.custom_repo}])
    elif name == "disabled_repo":
        made.save(enable=[ext], disable_repos=["user_default"])
    elif name == "extra_scripts":
        made.save(enable=["fixture_extra"], script_dirs=[made.extra_scripts])
    elif name == "marker":
        made.save(enable=["fixture_marker"])
    elif name == "json_kit":
        made.save(enable=[kit], global_dir=made.global_dir)
    else:
        raise KeyError(name)
    _fixtures[name] = made
    return made


# Rewrites a version 1 blend file in the older layout with the shorter block header.
def to_old_layout(data):
    out = bytearray(b"BLENDER-v405")
    pos = 17
    while True:
        code, sdna, old, length, count = struct.unpack_from("<4siQqq", data, pos)
        out += struct.pack("<4siQii", code, length, old, sdna, count)
        if code == b"ENDB":
            return bytes(out)
        out += data[pos + 32:pos + 32 + length]
        pos += 32 + length


class ReaderTests(unittest.TestCase):
    # Writes bytes as the userpref.blend of a fresh config folder and returns the folder.
    def config_with(self, data):
        folder = tempfile.mkdtemp(prefix="plynth_cfg_", dir=_scratch)
        if data is not None:
            with open(os.path.join(folder, "userpref.blend"), "wb") as handle:
                handle.write(data)
        return folder

    def original_bytes(self, name="signed_in"):
        with open(os.path.join(variant(name).config, "userpref.blend"), "rb") as handle:
            return handle.read()

    # Enabled add-ons, the BlenderKit key flag and the data folder are decoded from a real saved file.
    def test_signed_in_file(self):
        prefs = userprefs.read_preferences(variant("signed_in").config)
        self.assertTrue(prefs.exists)
        for module in ("fixture_legacy", "blenderkit", "bl_ext.user_default.fixture_ext", "io_scene_fbx"):
            self.assertIn(module, prefs.addons)
        self.assertNotIn("fixture_disabled", prefs.addons)
        self.assertTrue(prefs.blenderkit_enabled)
        self.assertTrue(prefs.blenderkit_has_key)
        self.assertEqual(prefs.blenderkit_global_dir.replace("\\", "/"), variant("signed_in").global_dir)

    # No key saved means no sign-in, and a disabled BlenderKit is not read at all.
    def test_signed_out_and_disabled(self):
        prefs = userprefs.read_preferences(variant("signed_out").config)
        self.assertTrue(prefs.blenderkit_enabled)
        self.assertFalse(prefs.blenderkit_has_key)
        prefs = userprefs.read_preferences(variant("kit_disabled").config)
        self.assertFalse(prefs.blenderkit_enabled)
        self.assertFalse(prefs.blenderkit_has_key)
        self.assertEqual(prefs.blenderkit_global_dir, "")

    # Repositories keep their module, enabled flag, system flag and custom folder.
    def test_repositories(self):
        prefs = userprefs.read_preferences(variant("custom_repo").config)
        repos = {repo.module: repo for repo in prefs.repositories}
        self.assertTrue(repos["system"].system)
        self.assertFalse(repos["user_default"].system)
        self.assertEqual(repos["custom_repo"].custom_folder.replace("\\", "/"), variant("custom_repo").custom_repo)
        self.assertTrue(repos["custom_repo"].enabled)
        disabled = userprefs.read_preferences(variant("disabled_repo").config)
        self.assertFalse({repo.module: repo for repo in disabled.repositories}["user_default"].enabled)

    # Extra script folders are listed.
    def test_script_folders(self):
        prefs = userprefs.read_preferences(variant("extra_scripts").config)
        self.assertEqual([folder.replace("\\", "/") for folder in prefs.script_folders],
                         [variant("extra_scripts").extra_scripts])

    # No file, or no folder, means Blender's defaults.
    def test_no_file(self):
        for folder in (self.config_with(None), os.path.join(_scratch, "does_not_exist")):
            prefs = userprefs.read_preferences(folder)
            self.assertFalse(prefs.exists)
            self.assertEqual(prefs.addons, [])
            self.assertFalse(prefs.blenderkit_has_key)

    # The same settings in a zstd file are read the same way, also with a trailing skippable frame.
    def test_zstd_file(self):
        import zstandard
        raw = self.original_bytes()
        packed = zstandard.ZstdCompressor().compress(raw)
        skippable = b"\x5e\x2a\x4d\x18" + struct.pack("<I", 8) + b"\0" * 8
        for data in (packed, packed + skippable):
            prefs = userprefs.read_preferences(self.config_with(data))
            self.assertTrue(prefs.blenderkit_has_key)
            self.assertIn("fixture_legacy", prefs.addons)

    # The older block header layout is read too.
    def test_old_layout(self):
        prefs = userprefs.read_preferences(self.config_with(to_old_layout(self.original_bytes())))
        self.assertTrue(prefs.blenderkit_has_key)
        self.assertIn("bl_ext.user_default.fixture_ext", prefs.addons)

    # Damaged files give one fixed error that holds nothing from the file.
    def test_unreadable_files(self):
        import zstandard
        raw = self.original_bytes()
        packed = zstandard.ZstdCompressor().compress(raw)
        cases = {
            "empty": b"",
            "text": b"this is not a blend file at all",
            "header_only": raw[:17],
            "truncated": raw[:len(raw) // 2],
            "truncated_blocks": raw[:200],
            "zstd_truncated": packed[:len(packed) // 2],
            "zstd_magic_only": b"\x28\xb5\x2f\xfd",
            "wrong_pointer_size": b"BLENDER_v405" + raw[12:],
            "zeros": b"\0" * 4096,
        }
        for label, data in cases.items():
            with self.subTest(label):
                with self.assertRaises(userprefs.PreferencesError) as caught:
                    userprefs.read_preferences(self.config_with(data))
                self.assertEqual(str(caught.exception), userprefs.UNREADABLE_MESSAGE)
                self.assertIsNone(caught.exception.__cause__)
                self.assertTrue(caught.exception.__suppress_context__)

    # The folder holds a directory where the file should be: unreadable, not a crash.
    def test_directory_instead_of_file(self):
        folder = self.config_with(None)
        os.makedirs(os.path.join(folder, "userpref.blend"))
        with self.assertRaises(userprefs.PreferencesError):
            userprefs.read_preferences(folder)

    # The reader opens the file for reading only, changes nothing and leaves nothing behind.
    def test_reads_only(self):
        fixture = variant("signed_in")
        before = fingerprint(fixture.root)
        modes = []
        real_open = builtins.open

        def spy(file, mode="r", *args, **kwargs):
            modes.append(mode)
            return real_open(file, mode, *args, **kwargs)

        builtins.open = spy
        try:
            userprefs.read_preferences(fixture.config)
        finally:
            builtins.open = real_open
        self.assertEqual(modes, ["rb"])
        self.assertEqual(fingerprint(fixture.root), before)

    # The key is only a yes or no: it is in no attribute of the result, and its bytes are never decoded.
    def test_key_is_not_decoded(self):
        prefs = userprefs.read_preferences(variant("signed_in").config)
        self.assertNotIn(FAKE_KEY, repr(vars(prefs)))
        raw = bytearray(self.original_bytes())
        spot = raw.find(FAKE_KEY.encode())
        self.assertGreater(spot, 0)
        raw[spot:spot + len(FAKE_KEY)] = b"\xff" * len(FAKE_KEY)
        damaged = userprefs.read_preferences(self.config_with(bytes(raw)))
        self.assertTrue(damaged.blenderkit_has_key)

    # A list that loops back on itself is refused instead of followed forever.
    def test_looping_list_is_refused(self):
        raw = bytearray(self.original_bytes())
        blend = userprefs.BlendFile(bytes(raw))
        user_start = blend.first_user_block[0]
        first = blend.list_first(user_start, "UserDef", "addons")
        start = blend.block(first)[0]
        raw[start:start + 8] = struct.pack("<Q", first)
        with self.assertRaises(userprefs.PreferencesError):
            userprefs.read_preferences(self.config_with(bytes(raw)))


if __name__ == "__main__":
    unittest.main()
