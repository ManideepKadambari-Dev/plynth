"""The fetch guard (blender/guard/startup/plynth_fetch_guard.py, StoreFetch.h) checked without Blender: the
stand-in Blender loads it from BLENDER_SYSTEM_SCRIPTS as Blender would and runs guard_probe.py, which tries
each kind of event outside and inside the fetch folder.
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
FILE_EVENTS = ("create", "change", "rename", "link", "folder", "delete")


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

    # A registry write, a program from outside, ending another program, saving preferences and installing are
    # refused. A program inside the fetch folder may start.
    def test_other_events(self):
        for name in ("registry_write", "program_outside", "kill", "save_userpref", "install"):
            with self.subTest(name=name):
                self.assertFalse(self.outcome(name)["ok"], self.outcome(name))
        self.assertIsNone(self.sleeper.poll())
        inside = self.outcome("program_inside")
        self.assertTrue(inside["ok"] or inside["error"] == "TimeoutExpired", inside)

    # The guard cannot be taken out again.
    def test_cannot_be_removed(self):
        self.assertFalse(self.outcome("after_unregister_create")["ok"])
        self.assertFalse(os.path.exists(os.path.join(self.outside, "after.txt")))


if __name__ == "__main__":
    unittest.main()
