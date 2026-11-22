import json
import os
import re
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest import mock

import sys

import bpy

import helpers
from schema_check import load_schema, validate

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_fake_blenderkit_config as fake_config  # noqa: E402

WORKER = os.path.join(helpers.BLENDER_FOLDER, "plynth_worker.py")
CHAIR_SOURCE = os.path.join(helpers.REPO_FOLDER, "samples", "models", "chair")
PLYNTH_FOLDER = os.path.join(helpers.BLENDER_FOLDER, "plynth")
MODEL_ID = "11111111-1111-4111-8111-111111111111"
MATERIAL_ID = "22222222-2222-4222-8222-222222222222"
HDR_ID = "33333333-3333-4333-8333-333333333333"
ERROR_ID = "44444444-4444-4444-8444-444444444444"
NEVER_ID = "55555555-5555-4555-8555-555555555555"
SLOW = bool(os.environ.get("PLYNTH_SLOW_TESTS"))


# Makes the files the fake add-on hands out as downloads: a chair with textures and a material.
def make_sources(folder):
    os.makedirs(os.path.join(folder, "textures"))
    shutil.copyfile(os.path.join(CHAIR_SOURCE, "chair_wood.png"), os.path.join(folder, "textures", "chair_wood.png"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mesh = bpy.data.meshes.new("Seat")
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], [], [(0, 1, 2, 3)])
    bpy.context.scene.collection.objects.link(bpy.data.objects.new("Seat", mesh))
    bpy.data.materials.new("Wood").use_fake_user = True
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "chair.blend"), copy=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.data.materials.new("Fake oak").use_fake_user = True
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "material.blend"), copy=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)


class BlenderKitFetchTests(helpers.TempFolderTest):
    configs = {}
    root = None

    # Makes the fake add-on configs once: signed in, signed out, installed but disabled, and not installed.
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix="plynth_bk_")
        make_sources(os.path.join(cls.root, "source"))
        cls.configs = {
            "ok": fake_config.make_config(os.path.join(cls.root, "ok")),
            "signed_out": fake_config.make_config(os.path.join(cls.root, "signed_out"), signed_out=True),
            "disabled": fake_config.make_config(os.path.join(cls.root, "disabled"), disabled=True),
            "missing": fake_config.make_config(os.path.join(cls.root, "missing"), missing=True),
        }

    # Removes the configs.
    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, True)

    # Returns the download folder of one of the configs.
    def download_folder(self, config="ok"):
        return os.path.join(self.root, config, "download")

    # Runs the fetch task in a child Blender with a config and returns the result, the output lines and the seconds.
    def fetch(self, base_id, kind="model", config="ok", wait=60, resolution="2k"):
        params = {"asset_base_id": base_id, "kind": kind, "resolution": resolution,
                  "output_folder": self.path("work", "fetched"), "wait_seconds": wait}
        job_file, result_file = self.write_job("blenderkit_fetch", params)
        env = dict(self.configs[config])
        env["FAKE_BLENDERKIT_SOURCE"] = os.path.join(self.root, "source")
        env["FAKE_BLENDERKIT_LOG"] = self.path("requests.log")
        command = [bpy.app.binary_path, "-b", "-t", "6", "--offline-mode"]
        command += ["--python", WORKER, "--", "--job", job_file]
        started = time.time()
        done = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              timeout=300, cwd=self.folder)
        seconds = time.time() - started
        self.output = done.stdout
        self.assertTrue(os.path.exists(result_file), "no result file:\n" + done.stdout[-1500:])
        result = self.read_result(result_file)
        self.assert_result_valid(result)
        self.assertEqual(validate(json.load(open(job_file)), load_schema("blender-job")), [])
        return result, seconds

    # Returns the progress step names printed by the worker, in order.
    def steps(self):
        found = []
        for line in self.output.splitlines():
            if line.startswith("PLYNTH:"):
                found.append(json.loads(line[len("PLYNTH:"):])["progress"]["step"])
        return found

    # Returns the requests the fake add-on logged.
    def requests(self):
        with open(self.path("requests.log"), encoding="utf-8") as handle:
            return [json.loads(line) for line in handle if line.strip()]

    # A model arrives as a .blend with its textures, with the name, author and license the add-on knew.
    def test_model(self):
        result, seconds = self.fetch(MODEL_ID)
        print("fake fetch, background: %.1f s" % seconds)
        self.assertTrue(result["ok"], result["error"])
        output = result["output"]
        self.assertTrue(output["file"].endswith(".blend"))
        self.assertTrue(os.path.isfile(self.path("work", "fetched", output["file"])))
        self.assertTrue(os.path.isfile(self.path("work", "fetched", "textures", "chair_wood.png")))
        self.assertEqual((output["kind"], output["format"], output["name"], output["author"], output["license"]),
                         ("model", "blend", "Fake chair", "Fake Author", "royalty_free"))
        self.assertIsNone(output["material"])
        self.assertIn(MODEL_ID, output["reference"])
        request = self.requests()[0]
        self.assertEqual((request["resolution"], request["use_resolution_operator"]), ("2048", True))

    # A material arrives with the name of the material inside the file.
    def test_material(self):
        result, _seconds = self.fetch(MATERIAL_ID, kind="material", resolution="original")
        self.assertTrue(result["ok"], result["error"])
        output = result["output"]
        self.assertEqual((output["kind"], output["material"], output["license"]), ("material", "Fake oak", "cc0"))
        self.assertEqual(self.requests()[0]["resolution"], "ORIGINAL")

    # An item of the other kind, and kinds that cannot be imported, are refused with a sentence.
    def test_wrong_kind(self):
        for base_id, kind, words in ((MATERIAL_ID, "model", "is a material"), (MODEL_ID, "material", "is a model"),
                                     (HDR_ID, "model", "hdr"), (HDR_ID, "material", "hdr")):
            result, _seconds = self.fetch(base_id, kind=kind)
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "wrong_asset_kind", result["error"])
            self.assertIn(words, result["error"]["message"])
        self.assertFalse(os.path.exists(self.path("work", "fetched", "Fake_oak.blend")))

    # Signed out, add-on disabled and add-on missing give their own codes.
    def test_setup_errors(self):
        result, _seconds = self.fetch(MODEL_ID, config="signed_out")
        self.assertEqual(result["error"]["code"], "not_signed_in")
        self.assertIn("Sign in to BlenderKit inside Blender first", result["error"]["message"])
        for config in ("disabled", "missing"):
            result, _seconds = self.fetch(MODEL_ID, config=config)
            self.assertEqual(result["error"]["code"], "addon_missing", config)
        self.assertEqual(self.steps(), ["check_addon"])

    # A download that ends without a file is download_failed and names the add-on version.
    def test_download_failed(self):
        result, _seconds = self.fetch(ERROR_ID)
        self.assertEqual(result["error"]["code"], "download_failed")
        self.assertIn("9.9.9", result["error"]["message"])
        self.assertIn("server returned an error", result["error"]["message"])

    # A download that never arrives is download_timeout. Slow, so only with PLYNTH_SLOW_TESTS.
    @unittest.skipUnless(SLOW, "set PLYNTH_SLOW_TESTS to run")
    def test_download_timeout(self):
        result, _seconds = self.fetch(NEVER_ID, wait=60)
        self.assertEqual(result["error"]["code"], "download_timeout")
        self.assertIn("9.9.9", result["error"]["message"])

    # The key is not in the result, the printed log or any file the job wrote.
    def test_key_never_written(self):
        result, _seconds = self.fetch(MODEL_ID)
        self.assertTrue(result["ok"])
        self.assertNotIn(fake_config.FAKE_KEY, self.output)
        for folder, _folders, files in os.walk(self.folder):
            for name in files:
                with open(os.path.join(folder, name), "rb") as handle:
                    self.assertNotIn(fake_config.FAKE_KEY.encode(), handle.read(), name)

    # Files that were in the add-on's download folder before are still there, unchanged.
    def test_download_folder_untouched(self):
        old = os.path.join(self.download_folder(), "models", "Old_item", "Old.blend")
        os.makedirs(os.path.dirname(old), exist_ok=True)
        with open(old, "wb") as handle:
            handle.write(b"OLDFILE")
        os.utime(old, (1_000_000_000, 1_000_000_000))
        result, _seconds = self.fetch(MODEL_ID)
        self.assertTrue(result["ok"])
        self.assertTrue(os.path.isfile(old))
        self.assertEqual(os.path.getmtime(old), 1_000_000_000)
        self.assertNotIn("Old", result["output"]["file"])
        self.assertFalse(os.path.exists(self.path("work", "fetched", "Old.blend")))

    # Progress lines come in order.
    def test_progress_steps(self):
        result, _seconds = self.fetch(MODEL_ID)
        self.assertTrue(result["ok"])
        self.assertEqual(self.steps(), ["check_addon", "download", "copy"])

    # Writes the fake add-on's saved settings file into the signed out config, runs the fetch and removes the file.
    def fetch_with_saved_settings(self, content):
        path = os.path.join(self.root, "signed_out", "config", "preferences.json")
        with open(path, "wb") as handle:
            handle.write(content)
        try:
            result, _seconds = self.fetch(MODEL_ID, config="signed_out")
            remains = os.path.isfile(path)
            with open(path, "rb") as handle:
                kept = handle.read() if remains else None
        finally:
            if os.path.isfile(path):
                os.remove(path)
        return result, kept

    # A saved settings file that does not parse is not given to the add-on's loader, which would delete it. It
    # stays as it was and the error says it was left alone.
    def test_damaged_saved_settings_stay(self):
        for content in (b'{"api_key": "', b"", bytes([255, 254]) + b" not json", b"[1, 2]"):
            result, kept = self.fetch_with_saved_settings(content)
            self.assertEqual(result["error"]["code"], "not_signed_in", content)
            self.assertIn("left as it is", result["error"]["message"], content)
            self.assertEqual(kept, content)

    # A valid saved settings file is handed to the loader and signs in. The key is not in the output.
    def test_valid_saved_settings_sign_in(self):
        content = json.dumps({"api_key": fake_config.FAKE_KEY}).encode("ascii")
        result, kept = self.fetch_with_saved_settings(content)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(kept, content)
        self.assertNotIn(fake_config.FAKE_KEY, self.output)


class FakeConfigTests(helpers.TempFolderTest):
    # A child gets every private variable, each naming a folder that exists inside the scratch folder.
    def test_child_env_is_private(self):
        env = fake_config.child_env(self.path("fake"))
        root = os.path.join(os.path.abspath(self.path("fake")), "")
        names = ("BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS", "BLENDER_USER_EXTENSIONS",
                 "BLENDER_USER_DATAFILES", "APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME",
                 "PLYNTH_BLENDER_PRIVATE", "PLYNTH_CONFIG_DIR")
        for name in names:
            self.assertTrue(env[name].startswith(root), name)
            self.assertTrue(os.path.isdir(env[name]), name)
        self.assertEqual({name for name in env if name.startswith("BLENDER_USER_")}, set(names[:5]))

    # A folder in or around a user's own folders is refused before anything is made.
    def test_user_folders_are_refused(self):
        user = {name: self.path(name.lower()) for name in ("APPDATA", "LOCALAPPDATA", "USERPROFILE")}
        with mock.patch.dict(os.environ, user):
            for folder in (os.path.join(user["APPDATA"], "Blender Foundation", "Blender", "5.2"), user["APPDATA"],
                           os.path.join(user["USERPROFILE"], "blenderkit_data")):
                with self.assertRaises(ValueError, msg=folder):
                    fake_config.child_env(folder)
                self.assertFalse(os.path.exists(folder), folder)


class SourceRuleTests(unittest.TestCase):
    # Returns the text of every Python file under the worker package.
    def sources(self):
        for folder, _folders, files in os.walk(PLYNTH_FOLDER):
            for name in files:
                if name.endswith(".py"):
                    path = os.path.join(folder, name)
                    with open(path, encoding="utf-8") as handle:
                        yield path, handle.read()

    # The worker package never names the path tracing engine.
    def test_no_path_tracing_engine(self):
        for path, text in self.sources():
            self.assertNotIn("cycles", text.lower(), path)

    # The worker package opens no network connection of its own.
    def test_no_network_code(self):
        pattern = re.compile(r"^\s*(import|from)\s+(urllib|requests|http|socket|ssl|ftplib)\b", re.MULTILINE)
        for path, text in self.sources():
            self.assertIsNone(pattern.search(text), path)

    # The key is only ever asked about, never copied out: api_key appears only in the stored check.
    def test_key_only_checked(self):
        with open(os.path.join(PLYNTH_FOLDER, "stores", "blenderkit.py"), encoding="utf-8") as handle:
            lines = [line for line in handle if "api_key" in line]
        self.assertEqual(len(lines), 1)
        self.assertIn("bool(", lines[0])


if __name__ == "__main__":
    unittest.main()
