"""S-R 1 to 5, 3b, 8 and 9: how a background Blender 5.2 behaves where the fetch design rests on it. Real Blender,
scratch folders, offline, six threads, no real add-on. Each answer is printed for the record; the tests fail
only where a rule of BlenderKit.h or the fetch guard needs one answer (S-R 3b, 5, 8 and 9). Every start but one
uses factory startup; the S-R 5 check is the one start without it, in a scratch user only. Run alone on the
owner PC, one Blender at a time:
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
GUARD_FILE = os.path.join(GUARD, "startup", "plynth_fetch_guard.py")
FAKE_ADDONS = os.path.dirname(os.path.abspath(fake_config.FAKE_ADDON))


# The answers a run printed, by their name.
def answers(text):
    return {match.group(1): match.group(2) for match in re.finditer(r"^SR\d+ (\w+): (\S+)", text, re.MULTILINE)}


# The S-R 3b answers a run printed, by their name.
def answers_3b(text):
    return {match.group(1): match.group(2).strip() for match in re.finditer(r"^SR3B (\w+): (.+)$", text, re.MULTILINE)}


# Copies one file of the scratch folder to a place, making its folder.
def copy_scratch(name, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.copyfile(os.path.join(SCRATCH, name), target)


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

    # S-R 3 is answered for the record. Its rule, one hook for every operator, is dropped; S-R 3b replaces it.
    def test_sr3_answered(self):
        self.assertIn("execute_ran_directly", self.seen, self.done.stdout[-2000:])

    # S-R 4 decides the fake append: answers there, the source file kept.
    def test_sr4_answered(self):
        self.assertIn("undo_push", self.seen, self.done.stdout[-2000:])
        self.assertEqual(self.seen.get("source_kept"), "yes")

    # S-R 8: the reports of the add-on need a window.
    def test_sr8_window(self):
        self.assertEqual(self.seen.get("window"), "present", self.done.stdout[-2000:])

    # S-R 9: step 6 compares tempfile.gettempdir() with the fetch folder, so it must follow TEMP.
    def test_sr9_tempdir(self):
        self.assertEqual(self.seen.get("gettempdir_follows_temp"), "yes", self.done.stdout[-2000:])


class OperatorCloses(unittest.TestCase):
    # S-R 3b: one scratch run with factory startup. The work folder is the fetch folder of the guard copies the
    # script loads, and the test add-on sr3b_probe_addon sits in the scratch user.
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix="plynth_sr3b_")
        cls.addClassCleanup(shutil.rmtree, cls.root, True)
        env = fake_config.child_env(os.path.join(cls.root, "user"))
        work = os.path.join(cls.root, "work")
        os.makedirs(os.path.join(work, "tmp"))
        copy_scratch("sr3b_probe_addon.py",
                     os.path.join(env["BLENDER_USER_SCRIPTS"], "addons", "sr3b_probe_addon", "__init__.py"))
        env.update({"TEMP": os.path.join(work, "tmp"), "TMP": os.path.join(work, "tmp"), "PLYNTH_FETCH_PRIVATE": work,
                    "SR3B_PROBE_MARKER": os.path.join(work, "probe.marker"),
                    "FAKE_BK_EVENTS": os.path.join(work, "events.jsonl"), "PYTHONDONTWRITEBYTECODE": "1"})
        cls.done = run_blender(env, ["--factory-startup", "--python", os.path.join(SCRATCH, "sr3b_operator_closes.py"),
                                     "--", work, FAKE_ADDONS, GUARD_FILE])
        cls.seen = answers_3b(cls.done.stdout)
        print(cls.done.stdout[-4000:])

    # The answer of a name, failing with the output when it is missing.
    def answer(self, name):
        self.assertIn(name, self.seen, self.done.stdout[-4000:])
        return self.seen[name]

    # Asserts that a call was refused with an error, not run and not quietly cancelled.
    def assert_refused(self, name):
        value = self.answer(name)
        self.assertNotEqual(value, "ran", name)
        self.assertFalse(value.startswith("returned_"), name + ": " + value)

    # Case 1: an unregistered test operator runs neither through bpy.ops nor through a callable taken before.
    def test_case1_unregistered_operator(self):
        self.assertEqual(self.answer("1_control"), "marker")
        self.assertEqual(self.answer("1_type_before"), "yes")
        self.assertEqual(self.answer("1_type_gone"), "yes")
        self.assert_refused("1_through_ops")
        self.assert_refused("1_through_early")
        self.assertEqual(self.answer("1_marker_after"), "no")

    # Case 2: the same for a class of the fake add-on and for preferences.addon_enable, after the seal, also
    # through addon_utils and through callables taken before.
    def test_case2_addon_class_and_addon_enable(self):
        self.assertEqual(self.answer("2_enable_control"), "marker")
        self.assertEqual(self.answer("2_choice_control"), "marker")
        for name in ("2_enable_through_ops", "2_enable_through_early", "2_addon_utils_enable", "2_addon_utils_disable",
                     "2_choice_through_ops", "2_choice_through_early", "2_install_through_early"):
            with self.subTest(name=name):
                self.assert_refused(name)
        self.assertEqual(self.answer("2_probe_marker_after"), "no")
        self.assertEqual(self.answer("2_choice_marker_after"), "no")

    # Case 3: the wrapper of the search, set in its module, raises before the operator branches on the kind.
    def test_case3_lookup_wrapper(self):
        self.assertEqual(self.answer("3_control"), "marker")
        self.assert_refused("3_through_ops")
        self.assert_refused("3_through_early")
        self.assertEqual(self.answer("3_marker_after"), "no")

    # Case 4: the seal of the guard passes and leaves no named class, while with one class left registered it
    # fails with seal_failed. dir(bpy.types) must show the Python operator types, the guard finds the
    # extension operators that way.
    def test_case4_seal_proofs(self):
        self.assertEqual(self.answer("0_dir_lists_python_operator"), "yes")
        self.assertEqual(self.answer("0_getattr_is_the_class"), "yes")
        self.assertEqual(self.answer("0_extension_types_in_dir"), self.answer("0_extension_classes_found"))
        self.assertEqual(self.answer("4_registered_before"), "yes")
        self.assertEqual(self.answer("4_left_one"), "seal_failed")
        self.assertEqual(self.answer("4_left_one_still_registered"), "yes")
        self.assertEqual(self.answer("4_restored"), "yes")
        self.assertEqual(self.answer("4_seal"), "passes")
        self.assertEqual(self.answer("4_types_left"), "none")
        self.assertEqual(self.answer("4_classes_left"), "none")
        self.assertEqual(self.answer("4_choice_class_left"), "no")
        self.assertEqual(self.answer("4_enable_replaced"), "yes")
        self.assertEqual(self.answer("4_seal_again"), "already_sealed")

    # Case 5: the tested list holds only 5.2, this Blender passes, and a list or a version outside it is refused
    # by require and by seal before anything is unregistered.
    def test_case5_version(self):
        self.assertEqual(self.answer("5_list"), "5.2")
        self.assertEqual(self.answer("5_running"), "passes")
        for name in ("5_other_list_require", "5_other_list_seal", "5_patched_require", "5_patched_seal"):
            with self.subTest(name=name):
                self.assertEqual(self.answer(name), "untested_blender")
        self.assertEqual(self.answer("5_addon_enable_kept"), "yes")
        self.assertEqual(self.answer("done"), "yes")


class StartupOrder(unittest.TestCase):
    # S-R 5: the startup script of the guard in BLENDER_SYSTEM_SCRIPTS runs before an enabled add-on and an
    # enabled extension register, so both already meet its refusals, and Blender still loads its own scripts.
    # The scratch user gets all twelve folders of child_env and a userpref saved there with factory startup;
    # the check is the one start without factory startup, offline, six threads.
    def test_sr5_order(self):
        root = tempfile.mkdtemp(prefix="plynth_sr5_")
        self.addCleanup(shutil.rmtree, root, True)
        env = fake_config.child_env(os.path.join(root, "user"))
        for name in fake_config.PRIVATE_FOLDERS:
            with self.subTest(folder=name):
                self.assertTrue(env[name].startswith(os.path.join(root, "user")), env[name])
        private = os.path.join(root, "f")
        os.makedirs(os.path.join(private, "tmp"))
        fetch_rig.label_low(private)
        outside = os.path.join(root, "outside")
        os.makedirs(outside)
        addon = os.path.join(env["BLENDER_USER_SCRIPTS"], "addons", "sr5_addon")
        extension = os.path.join(env["BLENDER_USER_EXTENSIONS"], "user_default", "sr5_ext")
        copy_scratch("sr5_order_addon.py", os.path.join(addon, "__init__.py"))
        copy_scratch("sr5_order_addon.py", os.path.join(extension, "__init__.py"))
        copy_scratch("sr5_manifest.toml", os.path.join(extension, "blender_manifest.toml"))

        setup = dict(env, SR5_ORDER=os.path.join(root, "setup_order.txt"), SR5_OUTSIDE=os.path.join(outside, "setup"),
                     TEMP=os.path.join(root, "setup_tmp"), TMP=os.path.join(root, "setup_tmp"))
        os.makedirs(setup["TEMP"])
        saved = run_blender(setup, ["--factory-startup", "--python", os.path.join(SCRATCH, "sr5_save_prefs.py")])
        self.assertIn("SR5 SAVED", saved.stdout, saved.stdout[-3000:])
        self.assertIn("SR5 setup_sr5_addon: enabled", saved.stdout, saved.stdout[-3000:])
        self.assertIn("SR5 setup_sr5_ext: enabled", saved.stdout, saved.stdout[-3000:])
        self.assertTrue(os.path.isfile(os.path.join(env["BLENDER_USER_CONFIG"], "userpref.blend")),
                        saved.stdout[-3000:])

        order = os.path.join(private, "order.txt")
        child = dict(env, SR5_ORDER=order, SR5_OUTSIDE=os.path.join(outside, "check"), BLENDER_SYSTEM_SCRIPTS=GUARD,
                     PLYNTH_FETCH_PRIVATE=private, TEMP=os.path.join(private, "tmp"), TMP=os.path.join(private, "tmp"))
        done = run_blender(child, ["--disable-autoexec", "--python", os.path.join(SCRATCH, "sr5_check.py")])
        output = done.stdout[-4000:]
        print(output)
        refusals = done.stdout.count("Refused by the fetch guard")
        print("SR5 refusals printed during the start: %d" % refusals)
        cache = os.path.join(env["BLENDER_USER_EXTENSIONS"], ".cache")
        print("SR5 extensions cache folder: %s" % ("present" if os.path.isdir(cache) else "absent"))
        self.assertIn("SR5 builtin: yes", done.stdout, output)
        self.assertIn("SR5 guard_loaded: yes", done.stdout, output)
        self.assertIn("SR5 guard_root: set", done.stdout, output)
        self.assertIn("SR5 guard_reason: ok", done.stdout, output)
        self.assertTrue(os.path.isfile(order), output)
        with open(order, "r", encoding="utf-8") as handle:
            lines = sorted(handle.read().split(chr(10)))
        self.assertEqual([line for line in lines if line],
                         ["addon guard=yes outside=refused", "extension guard=yes outside=refused"], output)
        self.assertEqual(sorted(os.listdir(outside)), ["setup_addon", "setup_extension"], output)


if __name__ == "__main__":
    unittest.main()
