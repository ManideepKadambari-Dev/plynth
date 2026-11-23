"""A scratch user, fetch folder and fake helper for the fetch tests that run without Blender. The test plays
core's part: it makes the fetch folder low integrity labelled, builds the child's environment as core does
(guard first in BLENDER_SYSTEM_SCRIPTS, TEMP inside the fetch folder, no helper variables), writes the job
and starts the stand-in Blender. run_child is the one place that starts a child here.

Keys and tokens are made up. Nothing here reaches another computer: the fake helper listens on 127.0.0.1.
"""
import ctypes
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
BLENDER_FOLDER = os.path.dirname(os.path.dirname(HERE))
WORKER = os.path.join(BLENDER_FOLDER, "plynth_worker.py")
GUARD = os.path.join(BLENDER_FOLDER, "guard")
STAND_IN = os.path.join(HERE, "stand_in_blender.py")
FAKE_ADDON = os.path.join(BLENDER_FOLDER, "tests", "data", "fake_blenderkit", "blenderkit")
KEY = "KEY-MARKER-7f31-made-up-key-0000000000"
REFRESH = "TOKEN-MARKER-92ac-made-up-refresh-00000"
REMOVED = ("BLENDKIT_CLIENT_SETTINGS", "BLENDKIT_SERVER", "BLENDERKIT_SERVER", "BLENDKIT_DEBUG", "BLENDKIT_TOOLS_DIR")
RESOLUTION_PREF = {"0.5k": "512", "1k": "1024", "2k": "2048", "4k": "4096", "8k": "8192", "original": "ORIGINAL"}


# The fake helper program, named by the build. Fails loudly when the build did not name it.
def helper_program():
    path = os.environ.get("PLYNTH_FAKE_STORE_HELPER", "")
    if not os.path.isfile(path):
        raise RuntimeError("PLYNTH_FAKE_STORE_HELPER must name the fake_store_helper program of the build")
    return path


# A free local port.
def free_port():
    with socket.socket() as spare:
        spare.bind(("127.0.0.1", 0))
        return spare.getsockname()[1]


# Labels a folder so that a low integrity program may write in it, as core's makeLowIntegrityFolder does.
def label_low(folder):
    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    descriptor = ctypes.c_void_p()
    if not advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW("S:(ML;OICI;NW;;;LW)", 1,
                                                                        ctypes.byref(descriptor), None):
        raise OSError(ctypes.get_last_error(), "could not make the low label")
    try:
        if not advapi.SetFileSecurityW(folder, 0x10, descriptor):
            raise OSError(ctypes.get_last_error(), "could not label the fetch folder")
    finally:
        ctypes.windll.kernel32.LocalFree(descriptor)


class Item:
    # A made-up store item as the search answers it.
    def __init__(self, letter, kind="model", name=None):
        self.base_id = "%s-1111-4111-8111-%s" % (letter * 8, letter * 12)
        self.version_id = "%s-2222-4222-8222-%s" % (letter * 8, letter * 12)
        self.kind = kind
        self.name = name or "Item %s" % letter.upper()

    # The search record. It holds addresses and fields the worker must never copy whole.
    def record(self, **changes):
        record = {"assetBaseId": self.base_id, "id": self.version_id, "assetType": self.kind, "name": self.name,
                  "displayName": self.name, "author": {"fullName": "Made Up", "id": 7, "email": "SECRET-MAIL"},
                  "license": "royalty_free", "canDownload": True,
                  "files": [{"fileType": "blend", "downloadUrl": "https://www.blendkit.com/api/v1/downloads/x/",
                             "fileUuid": str(uuid.uuid4())}],
                  "thumbnailUrl": "https://www.blendkit.com/thumb/SECRET-ADDRESS"}
        record.update(changes)
        return record


MODEL = Item("a")
OTHER = Item("b")
MATERIAL = Item("c", "material", "Oak floor")
ADDON = Item("d", "addon")


# The JSON lines of a file, or none.
def read_lines(path):
    if not os.path.isfile(path):
        return []
    with open(path, "r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


# Starts the stand-in Blender with Blender's fixed safe options first.
def run_child(env, arguments, cwd, timeout):
    command = [sys.executable, "-I", "-X", "utf8", STAND_IN, "-b", "-t", "6", "--offline-mode"] + arguments
    return subprocess.run(command, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          errors="replace", timeout=timeout)


class Rig:
    # Everything one fetch needs, in a new temporary folder.
    def __init__(self, test, low=True):
        self.root = tempfile.mkdtemp(prefix="pf")
        test.addCleanup(shutil.rmtree, self.root, True)
        self.home = os.path.join(self.root, "home")
        self.private = os.path.join(self.root, "f")
        os.makedirs(self.private)
        if low:
            label_low(self.private)
        self.blender_user = os.path.join(self.home, "blender")
        for sub in ("config", "scripts", "extensions", "datafiles"):
            os.makedirs(os.path.join(self.blender_user, sub))
        self.addon = os.path.join(self.blender_user, "scripts", "addons", "blenderkit")
        shutil.copytree(FAKE_ADDON, self.addon, ignore=shutil.ignore_patterns("__pycache__"))
        os.makedirs(os.path.join(self.addon, "client"))
        shutil.copyfile(helper_program(), os.path.join(self.addon, "client", "bk_client.exe"))
        os.makedirs(os.path.join(self.addon, "blenderkit_updater"))
        self.data = os.path.join(self.home, "blenderkit_data")
        self.user_item = os.path.join(self.data, "models", "item-a_" + MODEL.version_id, "item-a_2K_old.blend")
        self.write(self.user_item, json.dumps({"fake_blend": 1, "objects": ["Users own"]}))
        self.write(os.path.join(self.data, "config", "preferences.json"), json.dumps({"api_key": KEY}))
        self.write(os.path.join(self.data, "client", "bin", "bk_client-settings.json"), "{}")
        self.prefs = {"api_key": KEY, "api_key_refresh": REFRESH, "api_key_timeout": int(time.time()) + 86400,
                      "global_dir": self.data, "resolution": "1024", "directory_behaviour": "BOTH",
                      "unpack_files": False, "create_asset_library": True}
        self.addon_scenario = {"standard_ports": [str(free_port()) for _ in range(8)]}
        self.source = os.path.join(self.root, "src", "item.blend")
        self.write(self.source, json.dumps({"fake_blend": 1, "objects": ["Item A"], "materials": ["Oak floor"]}))
        self.helper_scenario = {"search": [MODEL.record()], "download": {"source": self.source}, "linger_s": 2}
        self.helper_log = os.path.join(self.root, "helper.log")
        self.events_file = os.path.join(self.private, "events.jsonl")
        self.extra_env = {}
        self.done = None

    # Writes a text file, making its folder.
    def write(self, path, text):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)

    # The job of a fetch, as core writes it.
    def job(self, item, resolution="2k", wait=60):
        return {"format": "plynth-blender-job", "version": 1, "task": "blenderkit_fetch", "job_id": "plain-1",
                "result_file": os.path.join(self.private, "result.json").replace("\\", "/"),
                "params": {"asset_base_id": item.base_id, "kind": item.kind, "resolution": resolution,
                           "output_folder": os.path.join(self.private, "out").replace("\\", "/"),
                           "wait_seconds": wait, "private_folder": self.private.replace("\\", "/")}}

    # The child's environment, as core builds it, plus the fakes' variables.
    def environment(self):
        env = {name: value for name, value in os.environ.items() if name.upper() not in REMOVED}
        temp = os.path.join(self.private, "tmp")
        os.makedirs(temp, exist_ok=True)
        user = self.blender_user
        env.update({"BLENDER_SYSTEM_SCRIPTS": GUARD, "PLYNTH_FETCH_PRIVATE": self.private, "TEMP": temp, "TMP": temp,
                    "PYTHONDONTWRITEBYTECODE": "1", "USERPROFILE": self.home, "HOME": self.home,
                    "APPDATA": os.path.join(self.home, "AppData", "Roaming"),
                    "LOCALAPPDATA": os.path.join(self.home, "AppData", "Local"),
                    "BLENDER_USER_RESOURCES": user, "BLENDER_USER_CONFIG": os.path.join(user, "config"),
                    "BLENDER_USER_SCRIPTS": os.path.join(user, "scripts"),
                    "BLENDER_USER_EXTENSIONS": os.path.join(user, "extensions"),
                    "BLENDER_USER_DATAFILES": os.path.join(user, "datafiles"),
                    "FAKE_BK_SCENARIO": os.path.join(self.root, "addon_scenario.json"),
                    "FAKE_BK_EVENTS": self.events_file,
                    "FAKE_STORE_SCENARIO": os.path.join(self.root, "helper_scenario.json"),
                    "FAKE_STORE_LOG": self.helper_log})
        env.pop("XDG_DATA_HOME", None)
        env.update(self.extra_env)
        return env

    # Empties the fetch folder except the helper's kept folder, and the helper's log, as core does between fetches.
    def reset(self):
        for name in os.listdir(self.private):
            if name != "client":
                path = os.path.join(self.private, name)
                shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
        if os.path.exists(self.helper_log):
            os.remove(self.helper_log)

    # Runs one fetch in the stand-in Blender. Returns the result as read, or None when there is none.
    def fetch(self, item, resolution="2k", wait=60, guard=True, timeout=300):
        self.reset()
        self.write(os.path.join(self.blender_user, "config", "userpref.json"),
                   json.dumps({"addons": {"blenderkit": self.prefs}}))
        self.write(os.path.join(self.root, "addon_scenario.json"), json.dumps(self.addon_scenario))
        self.write(os.path.join(self.root, "helper_scenario.json"), json.dumps(self.helper_scenario))
        job = self.job(item, resolution, wait)
        job_file = os.path.join(self.private, "job.json")
        self.write(job_file, json.dumps(job))
        env = self.environment()
        if not guard:
            env["BLENDER_SYSTEM_SCRIPTS"] = os.path.join(self.root, "no_guard")
        self.done = run_child(env, ["--disable-autoexec", "--python-exit-code", "3", "--python", WORKER, "--",
                                    "--job", job_file], self.private, timeout)
        if not os.path.isfile(job["result_file"]):
            return None
        with open(job["result_file"], "r", encoding="utf-8") as handle:
            return json.load(handle)

    # Every event of the fake add-on and the stand-in bpy, in order.
    def events(self, name=None):
        return [entry for entry in read_lines(self.events_file) if name is None or entry["event"] == name]

    # Every line of the fake helper's request log.
    def requests(self, op=None):
        return [entry for entry in read_lines(self.helper_log) if op is None or entry.get("op") == op]

    # Path and content hash of every file below the scratch home, to see that nothing changed.
    def fingerprint(self):
        seen = {}
        for current, _folders, names in os.walk(self.home):
            for name in names:
                path = os.path.join(current, name)
                with open(path, "rb") as handle:
                    seen[os.path.relpath(path, self.home)] = hashlib.sha256(handle.read()).hexdigest()
        return seen

    # The end of the child's output, for a failing check.
    def output(self):
        return (self.done.stdout if self.done is not None else "")[-4000:]
