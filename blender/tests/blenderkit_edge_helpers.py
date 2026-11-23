"""Fetches in real child Blenders with the fake add-on, the fake helper and the fetch guard, for the few
BlenderKit tests that need real Blender. They run on the owner's PC only, one file at a time:
    blender -b --factory-startup --python tests/run_tests.py -- <test file>
Every child gets child_env, six threads and no online access. run_blender is the one place that starts a
child. The rig of the plain tests (tests/plain/fetch_rig.py) gives the items, the low label and the helper.
"""
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile

import bpy

import helpers

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "plain"))
import fetch_rig  # noqa: E402
import make_fake_blenderkit_config as fake_config  # noqa: E402

WORKER = os.path.join(helpers.BLENDER_FOLDER, "plynth_worker.py")
GUARD = os.path.join(helpers.BLENDER_FOLDER, "guard")
KEY = fake_config.FAKE_KEY
MODEL = fetch_rig.MODEL
MATERIAL = fetch_rig.MATERIAL


# Runs Blender in a scratch environment with the fixed safe options before any script.
def run_blender(env, arguments, timeout=200):
    command = [bpy.app.binary_path, "-b", "-t", "6", "--offline-mode"] + arguments
    return subprocess.run(command, env=env, cwd=env["USERPROFILE"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors="replace", timeout=timeout)


# Makes a scratch user with the fake add-on enabled and signed in, its helper program and updater folder in
# place. Returns the environment.
def make_user(root):
    env = fake_config.child_env(root)
    download = os.path.join(root, "download")
    os.makedirs(download, exist_ok=True)
    addon = os.path.join(root, "scripts", "addons", "blenderkit")
    shutil.copytree(fake_config.FAKE_ADDON, addon, ignore=shutil.ignore_patterns("__pycache__"))
    os.makedirs(os.path.join(addon, "client"))
    shutil.copyfile(fetch_rig.helper_program(), os.path.join(addon, "client", "bk_client.exe"))
    os.makedirs(os.path.join(addon, "blenderkit_updater"))
    options = {"signed_out": False, "disabled": False, "download": download}
    done = run_blender(env, ["--python", os.path.abspath(fake_config.__file__), "--", "--apply", json.dumps(options)])
    if done.returncode != 0 or "FAKE CONFIG SAVED" not in done.stdout:
        raise RuntimeError("Making the scratch user failed:\n" + done.stdout[-2000:])
    return env


# Saves a small real .blend with one object and the material "Oak floor", made in this Blender.
def make_source(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mesh = bpy.data.meshes.new("Item A")
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (1, 1, 0)], [], [(0, 1, 2)])
    bpy.context.scene.collection.objects.link(bpy.data.objects.new("Item A", mesh))
    bpy.data.materials.new("Oak floor").use_fake_user = True
    bpy.ops.wm.save_as_mainfile(filepath=path, copy=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)


class EdgeTest(helpers.TempFolderTest):
    # Makes one scratch user and one source .blend for the class.
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix="plynth_edge_")
        cls.addClassCleanup(shutil.rmtree, cls.root, True)
        cls.env = make_user(os.path.join(cls.root, "user"))
        cls.home = cls.env["USERPROFILE"]
        cls.source = os.path.join(cls.root, "source", "item.blend")
        os.makedirs(os.path.dirname(cls.source))
        make_source(cls.source)

    # Makes room for two fetches at once.
    def setUp(self):
        super().setUp()
        self.runner = concurrent.futures.ThreadPoolExecutor(max_workers=2)
        self.addCleanup(self.runner.shutdown)

    # Starts a fetch of an item. scenario may hold "helper" and "addon" parts for the two fakes. Returns what
    # finish needs.
    def start(self, item, scenario, wait=60, extra=("--disable-autoexec",)):
        folder = tempfile.mkdtemp(prefix="run", dir=self.folder)
        private = os.path.join(folder, "f")
        os.makedirs(os.path.join(private, "tmp"))
        fetch_rig.label_low(private)
        result = os.path.join(private, "result.json").replace("\\", "/")
        output = os.path.join(private, "out")
        job = {"format": "plynth-blender-job", "version": 1, "task": "blenderkit_fetch", "job_id": "edge-1",
               "result_file": result,
               "params": {"asset_base_id": item.base_id, "kind": item.kind, "resolution": "2k",
                          "output_folder": output.replace("\\", "/"), "wait_seconds": wait,
                          "private_folder": private.replace("\\", "/")}}
        helper = dict({"search": [item.record()], "download": {"source": self.source}, "linger_s": 2},
                      **scenario.get("helper", {}))
        addon = dict({"standard_ports": [str(fetch_rig.free_port()) for _ in range(8)]}, **scenario.get("addon", {}))
        files = {"job.json": job, "helper.json": helper, "addon.json": addon}
        for name, data in files.items():
            with open(os.path.join(private if name == "job.json" else folder, name), "w", encoding="utf-8") as handle:
                json.dump(data, handle)
        child = {name: value for name, value in self.env.items() if name.upper() not in fetch_rig.REMOVED}
        child.update({"BLENDER_SYSTEM_SCRIPTS": GUARD, "PLYNTH_FETCH_PRIVATE": private, "PYTHONDONTWRITEBYTECODE": "1",
                      "TEMP": os.path.join(private, "tmp"), "TMP": os.path.join(private, "tmp"),
                      "FAKE_STORE_SCENARIO": os.path.join(folder, "helper.json"),
                      "FAKE_STORE_LOG": os.path.join(folder, "helper.log"),
                      "FAKE_BK_SCENARIO": os.path.join(folder, "addon.json"),
                      "FAKE_BK_EVENTS": os.path.join(private, "events.jsonl")})
        arguments = list(extra) + ["--python-exit-code", "3", "--python", WORKER, "--", "--job",
                                   os.path.join(private, "job.json")]
        return self.runner.submit(run_blender, child, arguments), result, output

    # Waits for a started fetch. Returns (result, output folder).
    def finish(self, started):
        waiting, result, output = started
        done = waiting.result()
        self.assertTrue(os.path.exists(result), "the fetch wrote no result:\n" + done.stdout[-3000:])
        read = self.read_result(result)
        self.assert_result_valid(read)
        return read, output

    # Runs a fetch to its end.
    def fetch(self, item, scenario, **options):
        return self.finish(self.start(item, scenario, **options))

    # Path and content of every file below the scratch home.
    def home_files(self):
        found = {}
        for current, _folders, names in os.walk(self.home):
            for name in names:
                path = os.path.join(current, name)
                with open(path, "rb") as handle:
                    found[os.path.relpath(path, self.home)] = handle.read()
        return found
