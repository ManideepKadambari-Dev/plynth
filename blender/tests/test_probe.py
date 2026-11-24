import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import helpers
from plynth.tasks import probe
from test_userprefs import FAKE_KEY, MARKER_VARIABLE, fingerprint, private_env, variant, write_text


# Returns the content of every file under a folder joined together, for key searches.
def all_bytes(folder):
    found = b""
    for place, _, files in os.walk(folder):
        for name in files:
            with open(os.path.join(place, name), "rb") as handle:
                found += handle.read()
    return found


class ProbeTests(helpers.TempFolderTest):
    # Runs the probe on a fixture and returns its result.
    def probe(self, fixture):
        return self.run_task("probe", {"user_folders": fixture.user_folders})

    # Returns the output of a probe that must have worked.
    def output(self, fixture):
        result = self.probe(fixture)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result["output"]

    def modules(self, output):
        return {entry["module"]: entry for entry in output["addons"]}

    # The probe result is valid and reports the running Blender.
    def test_probe_result_is_valid(self):
        import bpy
        result = self.probe(variant("signed_in"))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        blender = result["output"]["blender"]
        self.assertEqual(blender["version"], ".".join(str(part) for part in bpy.app.version))
        self.assertEqual(result["blender_version"], blender["version"])
        self.assertGreater(len(blender["python"]), 0)

    # Without user folders the job is refused. There is no default that could reach a real folder.
    def test_user_folders_are_required(self):
        fixture = variant("signed_in")
        bad = [
            {},
            {"user_folders": "x"},
            {"user_folders": {key: value for key, value in fixture.user_folders.items() if key != "scripts"}},
            {"user_folders": dict(fixture.user_folders, config="relative/config")},
            {"user_folders": dict(fixture.user_folders, extensions="")},
            {"user_folders": dict(fixture.user_folders, blenderkit_data=None)},
        ]
        for params in bad:
            with self.subTest(params=params):
                result = self.run_task("probe", params)
                self.assertFalse(result["ok"])
                self.assertEqual(result["error"]["code"], "bad_job")
                self.assertIsNone(result["output"])

    # Enabled add-ons found on disk are listed with name and version, legacy and extension alike.
    def test_signed_in_state(self):
        output = self.output(variant("signed_in"))
        found = self.modules(output)
        self.assertEqual(found["fixture_legacy"]["name"], "Fixture Legacy")
        self.assertEqual(found["fixture_legacy"]["version"], "1.2.3")
        extension = found["bl_ext.user_default.fixture_ext"]
        self.assertEqual((extension["name"], extension["version"]), ("Fixture Extension", "2.0.1"))
        self.assertEqual(found["blenderkit"]["name"], "BlenderKit Fake")
        self.assertIn("io_scene_fbx", found)
        self.assertNotIn("fixture_disabled", found)
        self.assertEqual(output["blenderkit"], {"installed": True, "enabled": True, "signed_in": True})

    # Signed out, disabled and not installed are three different answers.
    def test_other_kit_states(self):
        self.assertEqual(self.output(variant("signed_out"))["blenderkit"],
                         {"installed": True, "enabled": True, "signed_in": False})
        self.assertEqual(self.output(variant("kit_disabled"))["blenderkit"],
                         {"installed": True, "enabled": False, "signed_in": False})
        missing = self.output(variant("missing"))
        self.assertEqual(missing["blenderkit"], {"installed": False, "enabled": False, "signed_in": False})
        found = self.modules(missing)
        self.assertNotIn("fixture_legacy", found)
        self.assertNotIn("bl_ext.user_default.fixture_ext", found)
        self.assertIn("io_scene_fbx", found)

    # A custom repository, a disabled repository and an extra script folder.
    def test_repositories_and_script_folders(self):
        self.assertEqual(self.modules(self.output(variant("custom_repo")))["bl_ext.custom_repo.fixture_ext2"]["name"],
                         "Fixture Custom")
        self.assertNotIn("bl_ext.user_default.fixture_ext", self.modules(self.output(variant("disabled_repo"))))
        self.assertEqual(self.modules(self.output(variant("extra_scripts")))["fixture_extra"]["name"], "Fixture Extra")

    # No saved settings means Blender's defaults and not signed in.
    def test_no_saved_settings(self):
        fixture = variant("signed_in")
        folders = dict(fixture.user_folders, config=self.path("empty_config"))
        result = self.run_task("probe", {"user_folders": folders})
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        output = result["output"]
        self.assertIn("io_scene_fbx", self.modules(output))
        self.assertFalse(output["blenderkit"]["signed_in"])
        self.assertFalse(output["blenderkit"]["enabled"])

    # A damaged settings file gives the fixed error and no output.
    def test_unreadable_settings(self):
        config = self.path("config")
        write_text(os.path.join(config, "userpref.blend"), "not a blend file " + FAKE_KEY)
        folders = dict(variant("signed_in").user_folders, config=config)
        result = self.run_task("probe", {"user_folders": folders})
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "preferences_unreadable")
        self.assertEqual(result["error"]["message"], "The saved Blender settings could not be read.")
        self.assertIsNone(result["output"])
        self.assertNotIn(FAKE_KEY, json.dumps(result) + self.printed)

    # The JSON sign-in is used when the settings hold no key. A damaged file is left exactly as it is.
    def test_json_sign_in(self):
        fixture = variant("json_kit")
        target = os.path.join(fixture.global_dir, "config", "preferences.json")
        self.addCleanup(shutil.rmtree, os.path.join(fixture.global_dir, "config"), True)
        cases = [
            ("valid", json.dumps({"api_key": FAKE_KEY, "global_dir": "x"}), True),
            ("empty key", json.dumps({"api_key": ""}), False),
            ("no key", json.dumps({"thumb_size": 96}), False),
            ("not an object", "[1, 2]", False),
            ("damaged", '{"api_key": "' + FAKE_KEY + '", ', False),
            ("empty file", "", False),
        ]
        self.assertFalse(self.output(fixture)["blenderkit"]["signed_in"])
        for label, text, expected in cases:
            with self.subTest(label):
                write_text(target, text)
                before = fingerprint(fixture.root)
                with open(target, "rb") as handle:
                    original = handle.read()
                result = self.probe(fixture)
                self.assertTrue(result["ok"], result["error"])
                self.assertEqual(result["output"]["blenderkit"]["signed_in"], expected)
                self.assertNotIn(FAKE_KEY, json.dumps(result) + self.printed)
                with open(target, "rb") as handle:
                    self.assertEqual(handle.read(), original)
                self.assertEqual(fingerprint(fixture.root), before)

    # An add-on whose import writes a marker is listed but never imported.
    def test_no_add_on_is_imported(self):
        fixture = variant("marker")
        os.environ[MARKER_VARIABLE] = fixture.marker
        self.addCleanup(os.environ.pop, MARKER_VARIABLE, None)
        output = self.output(fixture)
        self.assertEqual(self.modules(output)["fixture_marker"]["name"], "Fixture Marker")
        self.assertFalse(os.path.exists(fixture.marker))
        for name in ("fixture_marker", "fixture_legacy", "blenderkit", "bl_ext.user_default.fixture_ext"):
            self.assertNotIn(name, sys.modules)

    # The probe changes nothing in the user's folders, and the key is in no result, output or work file.
    def test_nothing_written_and_no_key(self):
        for name in ("signed_in", "signed_out", "missing", "custom_repo", "json_kit"):
            fixture = variant(name)
            before = fingerprint(fixture.root)
            result = self.probe(fixture)
            self.assertTrue(result["ok"], result["error"])
            self.assertEqual(fingerprint(fixture.root), before)
            self.assertNotIn(FAKE_KEY, json.dumps(result) + self.printed)
            self.assertNotIn(FAKE_KEY.encode(), all_bytes(self.path("work")))

    # The whole worker, started as Plynth starts it with a private folder, leaves the fixture alone and
    # keeps the key out of its output, its work folder and its private folder.
    def test_worker_process(self):
        import bpy
        fixture = variant("signed_in")
        private = tempfile.mkdtemp(prefix="plynth_private_")
        self.addCleanup(shutil.rmtree, private, True)
        job_file, result_file = self.write_job("probe", {"user_folders": fixture.user_folders})
        env = private_env(private)
        env["BLENDER_USER_RESOURCES"] = private
        env[MARKER_VARIABLE] = fixture.marker
        before = fingerprint(fixture.root)
        done = subprocess.run(
            [bpy.app.binary_path, "-t", "6", "--offline-mode", "--background", "--factory-startup", "-noaudio",
             "--python-exit-code", "3", "--python", os.path.join(helpers.BLENDER_FOLDER, "plynth_worker.py"),
             "--", "--job", job_file],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        self.assertEqual(done.returncode, 0, done.stdout.decode("utf-8", "replace")[-1500:])
        result = self.read_result(result_file)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(self.modules(result["output"])["fixture_legacy"]["version"], "1.2.3")
        self.assertTrue(result["output"]["blenderkit"]["signed_in"])
        self.assertEqual(fingerprint(fixture.root), before)
        self.assertNotIn(FAKE_KEY.encode(), done.stdout)
        self.assertNotIn(FAKE_KEY.encode(), all_bytes(private))
        self.assertNotIn(FAKE_KEY.encode(), all_bytes(self.path("work")))
        self.assertFalse(os.path.exists(fixture.marker))

    # Module names are matched by their last dotted part.
    def test_short_name(self):
        self.assertEqual(probe.short_name("bl_ext.user_default.blenderkit"), "blenderkit")
        self.assertEqual(probe.short_name("archimesh"), "archimesh")


if __name__ == "__main__":
    unittest.main()
