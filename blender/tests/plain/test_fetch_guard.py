"""The fetch guard (blender/guard/startup/plynth_fetch_guard.py, StoreFetch.h) checked without Blender: the
stand-in Blender loads it from BLENDER_SYSTEM_SCRIPTS as Blender would and runs guard_probe.py, which tries
each kind of event outside and inside the fetch folder. The seal of the guard and the low integrity start
are checked with seal_probe.py and save_probe.py.
"""
import json
import os
import shutil
import subprocess
import sys
import unittest
import winreg

import fetch_rig
from fetch_rig import Rig

PROBE = os.path.join(fetch_rig.HERE, "guard_probe.py")
SEAL_PROBE = os.path.join(fetch_rig.HERE, "seal_probe.py")
SAVE_PROBE = os.path.join(fetch_rig.HERE, "save_probe.py")
FILE_EVENTS = ("create", "change", "rename", "link", "folder", "delete")
SEALED = ("preferences.addon_enable", "preferences.addon_disable", "preferences.addon_install",
          "preferences.addon_remove", "extensions.package_install_files", "extensions.package_install",
          "extensions.package_uninstall", "extensions.package_upgrade_all", "scene.blenderkit_addon_choice")


class GuardEvents(unittest.TestCase):
    # Runs the probe once under the guard.
    @classmethod
    def setUpClass(cls):
        cls.case = cls("test_outside_is_refused")
        cls.rig = Rig(cls.case)
        cls.outside = os.path.join(cls.rig.root, "outside")
        os.makedirs(cls.outside)
        cls.rig.write(os.path.join(cls.outside, "kept.txt"), "kept")
        cls.inside = os.path.join(cls.rig.private, "work")
        os.makedirs(cls.inside)
        helper = os.path.join(cls.rig.private, "client", "bin", "helper.exe")
        os.makedirs(os.path.dirname(helper))
        shutil.copyfile(fetch_rig.helper_program(), helper)
        cls.sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)"])
        arguments = ["--disable-autoexec", "--python-exit-code", "3", "--python", PROBE, "--", cls.outside, cls.inside,
                     helper, str(cls.sleeper.pid)]
        cls.done = fetch_rig.run_child(cls.rig.environment(), arguments, cls.rig.private, 120)
        cls.seen = {}
        for line in cls.done.stdout.splitlines():
            if line.startswith("{"):
                entry = json.loads(line)
                cls.seen[entry["name"]] = entry

    # Ends the process the probe tried to kill, takes out a registry key it may have made, and cleans up.
    @classmethod
    def tearDownClass(cls):
        cls.sleeper.kill()
        cls.sleeper.wait()
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, "Software\\PlynthGuardProbe%d" % cls.sleeper.pid)
        except OSError:
            pass
        cls.case.doCleanups()

    # The outcome of one attempt.
    def outcome(self, name):
        self.assertIn(name, self.seen, self.done.stdout[-3000:])
        return self.seen[name]

    # File events outside the fetch folder raise PermissionError and change nothing.
    def test_outside_is_refused(self):
        for kind in FILE_EVENTS:
            with self.subTest(kind=kind):
                self.assertEqual(self.outcome("outside_" + kind)["error"], "PermissionError")
        self.assertEqual(sorted(os.listdir(self.outside)), ["kept.txt"])
        with open(os.path.join(self.outside, "kept.txt"), "r", encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "kept")

    # The same inside the fetch folder is allowed.
    def test_inside_is_allowed(self):
        for kind in FILE_EVENTS:
            with self.subTest(kind=kind):
                self.assertTrue(self.outcome("inside_" + kind)["ok"], self.outcome("inside_" + kind))

    # Reading outside, opening NUL and reading the registry are allowed.
    def test_reads_are_allowed(self):
        for name in ("outside_read", "nul_open", "registry_read"):
            with self.subTest(name=name):
                self.assertTrue(self.outcome(name)["ok"], self.outcome(name))

    # A registry write, a program from outside, ending another program and an install, whose files the
    # extension operator writes outside with Python, are refused. A program inside the fetch folder may start.
    # Saving preferences is a C operator that no Python check sees: PreferencesAtLowIntegrity covers it.
    def test_other_events(self):
        for name in ("registry_write", "program_outside", "kill", "install"):
            with self.subTest(name=name):
                self.assertFalse(self.outcome(name)["ok"], self.outcome(name))
        self.assertIsNone(self.sleeper.poll())
        inside = self.outcome("program_inside")
        self.assertTrue(inside["ok"] or inside["error"] == "TimeoutExpired", inside)

    # The guard cannot be taken out again.
    def test_cannot_be_removed(self):
        self.assertFalse(self.outcome("after_unregister_create")["ok"])
        self.assertFalse(os.path.exists(os.path.join(self.outside, "after.txt")))


class GuardSeal(unittest.TestCase):
    # Runs the seal probe once in a scratch user with the fake add-on enabled. extra holds variables for the
    # stand-in. Returns the outcomes by name, the rig and the end of the output.
    def probe(self, extra=None):
        rig = Rig(self)
        rig.prepare()
        rig.extra_env.update(extra or {})
        done = fetch_rig.run_child(rig.environment(), ["--disable-autoexec", "--python", SEAL_PROBE],
                                   rig.private, 120)
        seen = {}
        for line in done.stdout.splitlines():
            if line.startswith("{"):
                entry = json.loads(line)
                seen[entry["name"]] = entry
        self.assertIn("seal_again", seen, done.stdout[-3000:])
        return seen, rig, done.stdout[-3000:]

    # The seal in a tested Blender: it passes, every named class is gone, each one fails by its name and through
    # a callable taken before, addon_utils refuses, nothing ran, and a second seal is refused.
    def test_seal_closes_each_way(self):
        seen, rig, output = self.probe()
        self.assertTrue(seen["guard_loaded"]["ok"], output)
        self.assertTrue(seen["require"]["ok"], output)
        self.assertTrue(seen["seal"]["ok"], output)
        for idname in SEALED:
            for way in ("type_left_", "by_name_", "early_"):
                with self.subTest(way=way, operator=idname):
                    self.assertFalse(seen[way + idname]["ok"], output)
        for name in ("addon_utils_enable", "addon_utils_disable"):
            with self.subTest(name=name):
                self.assertEqual(seen[name]["error"], "PermissionError", output)
        self.assertEqual(seen["seal_again"]["reason"], "already_sealed", output)
        self.assertEqual(rig.events("bundled_ran"), [], output)
        self.assertEqual(rig.events("addon_choice_ran"), [], output)
        self.assertFalse(os.path.exists(rig.marker("addon_choice")))
        self.assertEqual([entry for entry in rig.events("addon_enable") if entry["module"] != "blenderkit"], [])
        self.assertEqual(rig.events("addon_disable"), [])

    # A Blender outside the tested list: require and seal both refuse with untested_blender, and the operators
    # stay as they were.
    def test_untested_version_is_refused(self):
        seen, _rig, output = self.probe({"FAKE_BLENDER_VERSION": "5.3.0"})
        self.assertEqual(seen["require"]["reason"], "untested_blender", output)
        self.assertEqual(seen["seal"]["reason"], "untested_blender", output)
        self.assertTrue(seen["type_left_preferences.addon_enable"]["ok"], output)

    # A class that cannot be taken out: the seal fails closed with seal_failed.
    def test_class_that_stays_fails_the_seal(self):
        for name in ("PREFERENCES_OT_addon_remove", "SCENE_OT_blenderkit_addon_choice",
                     "EXTENSIONS_OT_package_upgrade_all"):
            with self.subTest(kept=name):
                seen, _rig, output = self.probe({"FAKE_BPY_KEEP_CLASS": name})
                self.assertEqual(seen["seal"]["reason"], "seal_failed", output)


class PreferencesAtLowIntegrity(unittest.TestCase):
    # S-R 7 and the guard row "cannot save preferences": wm.save_userpref is a C operator in Blender 5.2, out of
    # reach of any Python check, so only Windows stops it, because the fetch runs Blender at low integrity. The
    # save of the stand-in writes for real: at normal integrity the file changes, at low integrity it does not,
    # with the guard or without, while the fetch folder stays writable.
    def test_save_userpref(self):
        for label, low, guard, changed in (("normal", False, False, True), ("low", True, False, False),
                                           ("low_with_guard", True, True, False)):
            with self.subTest(run=label):
                rig = Rig(self)
                saved = os.path.join(rig.blender_user, "config", "userpref.json")
                rig.write(saved, json.dumps({"addons": {}}))
                with open(saved, "rb") as handle:
                    before = handle.read()
                env = rig.environment()
                if not guard:
                    env["BLENDER_SYSTEM_SCRIPTS"] = os.path.join(rig.root, "no_guard")
                done = fetch_rig.run_child(env, ["--disable-autoexec", "--python", SAVE_PROBE, "--", rig.private],
                                           rig.private, 120, low=low)
                seen = {}
                for line in done.stdout.splitlines():
                    if line.startswith("{"):
                        entry = json.loads(line)
                        seen[entry["name"]] = entry
                self.assertIn("save_userpref", seen, done.stdout[-3000:])
                self.assertTrue(seen["inside"]["ok"], done.stdout[-3000:])
                with open(saved, "rb") as handle:
                    self.assertEqual(handle.read() != before, changed, done.stdout[-3000:])


if __name__ == "__main__":
    unittest.main()
