"""S-R 1 to 5, 8 and 9: how a background Blender 5.2 behaves where the fetch's design rests on it. Real Blender,
scratch folders, offline, no real add-on. Each answer is printed for the record; the tests fail only where a
rule of BlenderKit.h needs one answer (S-R 3, 5, 8 and 9). Run alone on the owner's PC:
    blender -b --factory-startup --python tests/run_tests.py -- test_blender_background_rules.py
"""
import os
import re
import shutil
import sys
import tempfile
import unittest

from blenderkit_edge_helpers import GUARD, fetch_rig, run_blender

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_fake_blenderkit_config as fake_config  # noqa: E402

SCRATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scratch")
ADDON = "def register():\n    import os\n    open(os.environ['SR5_ORDER'], 'a').write('%s\\n')\n\ndef unregister():\n    pass\n"
MANIFEST = ('schema_version = "1.0.0"\nid = "sr5_ext"\nversion = "1.0.0"\nname = "SR5"\nmaintainer = "Plynth tests"\n'
            'type = "add-on"\nblender_version_min = "4.2.0"\n')
ENABLE = ("import addon_utils, bpy; addon_utils.enable('sr5_addon', default_set=True); "
          "addon_utils.enable('bl_ext.user_default.sr5_ext', default_set=True); bpy.ops.wm.save_userpref(); "
          "print('SR5 SAVED')")
CHECK = "import bpy; print('SR5 builtin: %s' % ('yes' if hasattr(bpy.types, 'VIEW3D_HT_header') else 'no'))"


# The answers a run printed, by their name.
def answers(text):
    return {match.group(1): match.group(2) for match in re.finditer(r"^SR\d+ (\w+): (\S+)", text, re.MULTILINE)}


class BackgroundRules(unittest.TestCase):
    # One scratch run for S-R 1 to 4, 8 and 9.
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix="plynth_sr_")
        cls.addClassCleanup(shutil.rmtree, cls.root, True)
        env = fake_config.child_env(os.path.join(cls.root, "user"))
        env["TEMP"] = env["TMP"] = os.path.join(cls.root, "tmpx")
        os.makedirs(env["TEMP"])
        work = os.path.join(cls.root, "work")
        os.makedirs(work)
        cls.done = run_blender(env, ["--factory-startup", "--python", os.path.join(SCRATCH, "sr_rules.py"), "--", work])
        cls.seen = answers(cls.done.stdout)
        print(cls.done.stdout[-3000:])

    # S-R 1 and 2 only decide what the fakes do: an answer must be there.
    def test_sr1_sr2_answered(self):
        self.assertIn("update_on_equal_value", self.seen, self.done.stdout[-2000:])
        self.assertIn("resolution_seen", self.seen, self.done.stdout[-2000:])

    # S-R 3: a hook on bpy.ops._op_call refuses an operator and its execute never runs.
    def test_sr3_hook_refuses(self):
        self.assertEqual(self.seen.get("hook_refused"), "yes", self.done.stdout[-2000:])
        self.assertEqual(self.seen.get("execute_ran_through_hook"), "no")

    # S-R 4 decides the fake's append: answers there, the source file kept.
    def test_sr4_answered(self):
        self.assertIn("undo_push", self.seen, self.done.stdout[-2000:])
        self.assertEqual(self.seen.get("source_kept"), "yes")

    # S-R 8: the add-on's reports need a window.
    def test_sr8_window(self):
        self.assertEqual(self.seen.get("window"), "present", self.done.stdout[-2000:])

    # S-R 9: step 6 compares tempfile.gettempdir() with the fetch folder, so it must follow TEMP.
    def test_sr9_tempdir(self):
        self.assertEqual(self.seen.get("gettempdir_follows_temp"), "yes", self.done.stdout[-2000:])


class StartupOrder(unittest.TestCase):
    # S-R 5: a startup script of BLENDER_SYSTEM_SCRIPTS runs before an enabled add-on and an extension register,
    # Blender's own scripts still load, also with the fetch guard first.
    def test_sr5_order(self):
        root = tempfile.mkdtemp(prefix="plynth_sr5_")
        self.addCleanup(shutil.rmtree, root, True)
        env = fake_config.child_env(os.path.join(root, "user"))
        private = os.path.join(root, "f")
        os.makedirs(os.path.join(private, "tmp"))
        fetch_rig.label_low(private)
        addon = os.path.join(env["BLENDER_USER_SCRIPTS"], "addons", "sr5_addon")
        extension = os.path.join(env["BLENDER_USER_EXTENSIONS"], "user_default", "sr5_ext")
        startup = os.path.join(root, "system", "startup")
        for folder, name, text in ((addon, "__init__.py", ADDON % "addon"), (extension, "__init__.py", ADDON % "extension"),
                                   (extension, "blender_manifest.toml", MANIFEST), (startup, "sr5_first.py", ADDON % "startup")):
            os.makedirs(folder, exist_ok=True)
            with open(os.path.join(folder, name), "w", encoding="utf-8") as handle:
                handle.write(text)
        env["SR5_ORDER"] = os.path.join(private, "setup_order.txt")
        saved = run_blender(env, ["--python-expr", ENABLE])
        self.assertIn("SR5 SAVED", saved.stdout, saved.stdout[-2000:])
        for label, system in (("plain", os.path.join(root, "system")),
                              ("guard", os.pathsep.join((GUARD, os.path.join(root, "system"))))):
            with self.subTest(run=label):
                order = os.path.join(private, label + "_order.txt")
                child = dict(env, SR5_ORDER=order, BLENDER_SYSTEM_SCRIPTS=system, PLYNTH_FETCH_PRIVATE=private,
                             TEMP=os.path.join(private, "tmp"), TMP=os.path.join(private, "tmp"))
                done = run_blender(child, ["--disable-autoexec", "--python-expr", CHECK])
                self.assertIn("SR5 builtin: yes", done.stdout, done.stdout[-2000:])
                with open(order, "r", encoding="utf-8") as handle:
                    self.assertEqual(handle.read().split(), ["startup", "addon", "extension"], done.stdout[-2000:])


if __name__ == "__main__":
    unittest.main()
