import builtins
import contextlib
import copy
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import unittest
from unittest import mock

import _winapi
import bpy

import helpers
from build_helpers import load_params
from plynth import extensions
from slot_fixtures import upgrade

ADDON = "plynth_test_addon"
ADDON_DATA = os.path.join(helpers.BLENDER_FOLDER, "tests", "data", "addons")
FIVE = ("archimesh", "asset_optimizer", "Bagapie", "bool_tool", "home_builder_5")
USER_VARIABLES = (("BLENDER_USER_RESOURCES", ""), ("BLENDER_USER_CONFIG", "config"),
                  ("BLENDER_USER_SCRIPTS", "scripts"), ("BLENDER_USER_EXTENSIONS", "extensions"),
                  ("BLENDER_USER_DATAFILES", "datafiles"))
RECORD = """import json, os, sys, unittest


class Recorded(unittest.TestCase):
    def test_record(self):
        names = [n for n in os.environ if n.startswith("BLENDER_USER_")]
        with open(os.environ["GATE_RECORD"], "w", encoding="utf-8") as handle:
            json.dump({"argv": sys.argv, "user": {n: os.environ[n] for n in names},
                       "prepared": os.path.isfile(os.path.join(os.environ["BLENDER_USER_EXTENSIONS"],
                                                               ".plynth-prepared"))}, handle)
"""
PASSES = """import unittest


class Passes(unittest.TestCase):
    def test_passes(self):
        pass
"""
SKIPS = PASSES + """
    def test_skips(self):
        self.skipTest("no add-on")
"""
FAILS = PASSES + """
    def test_fails(self):
        self.fail("broken")
"""
SPY = """import builtins, glob, os, runpy


# Prints every file call that names the forbidden folder, then makes it.
def watched(call):
    def wrapper(*args, **kwargs):
        for value in args[:2]:
            if isinstance(value, (str, bytes, os.PathLike)) and "gate-forbidden" in str(value).lower():
                print("TOUCHED %s %s" % (getattr(call, "__name__", call), value), flush=True)
        return call(*args, **kwargs)
    return wrapper


for module, names in ((os, ("stat", "lstat", "listdir", "scandir", "open", "walk", "access")),
                      (os.path, ("exists", "lexists", "isfile", "isdir", "islink", "isjunction", "realpath",
                                 "getsize", "getmtime", "samefile")),
                      (glob, ("glob", "iglob")), (builtins, ("open",))):
    for name in names:
        if hasattr(module, name):
            setattr(module, name, watched(getattr(module, name)))
runpy.run_path(os.environ["GATE_RUNNER"], run_name="__main__")
"""


# Writes a text file, making its folder.
def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


# Returns each entry below a folder with its write time and the hash of its content.
def fingerprint(root):
    found = []
    for folder, folders, files in os.walk(root):
        for name in folders:
            found.append((os.path.relpath(os.path.join(folder, name), root), 0, ""))
        for name in files:
            path = os.path.join(folder, name)
            with open(path, "rb") as handle:
                digest = hashlib.sha256(handle.read()).hexdigest()
            found.append((os.path.relpath(path, root), os.stat(path).st_mtime_ns, digest))
    return sorted(found)


class ChildCase(helpers.TempFolderTest):
    # Makes the folders and variables that every child Blender of a test gets, and uses them here too.
    def setUp(self):
        super().setUp()
        values = {}
        for name, part in (("APPDATA", "appdata"), ("LOCALAPPDATA", "local"), ("USERPROFILE", "profile"),
                           ("HOME", "home"), ("XDG_DATA_HOME", "xdg"), ("TEMP", "temp"), ("TMP", "temp"),
                           ("PLYNTH_CONFIG_DIR", "settings"), ("PLYNTH_BLENDER_PRIVATE", "private")):
            values[name] = self.path(part)
        for name, part in USER_VARIABLES:
            values[name] = self.path("user", part) if part else self.path("user")
        for folder in values.values():
            os.makedirs(folder, exist_ok=True)
        self.env = {name: value for name, value in os.environ.items()
                    if not name.upper().startswith(("BLENDER_USER_", "PLYNTH_", "GATE_"))}
        self.env.update(values)
        patcher = mock.patch.dict(os.environ, values)
        patcher.start()
        self.addCleanup(patcher.stop)

    # Runs real Blender with six threads, offline. Returns its exit code and output.
    def blender(self, script, after=(), **changes):
        env = dict(self.env, **changes)
        command = [bpy.app.binary_path, "-t", "6", "--offline-mode", "--background", "--factory-startup", "-noaudio",
                   "--python-exit-code", "3", "--python", script] + list(after)
        done = subprocess.run(command, env=env, cwd=self.folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=180)
        return done.returncode, done.stdout.decode("utf-8", "replace")


class GateTests(ChildCase):
    # Makes a copy of the test runner with stand-in modules, so no real add-on test runs.
    def setUp(self):
        super().setUp()
        base = self.path("repo", "blender")
        shutil.copytree(os.path.join(helpers.BLENDER_FOLDER, "plynth"), os.path.join(base, "plynth"),
                        ignore=shutil.ignore_patterns("__pycache__"))
        tests = os.path.join(helpers.BLENDER_FOLDER, "tests")
        os.makedirs(os.path.join(base, "tests"))
        for name in os.listdir(tests):
            if name.endswith(".py") and not name.startswith("test_"):
                shutil.copyfile(os.path.join(tests, name), os.path.join(base, "tests", name))
        self.runner = self.path("repo", "blender", "tests", "run_tests.py")
        self.record = self.path("record.json")
        self.env["GATE_RECORD"] = self.record
        self.env["GATE_RUNNER"] = self.runner
        self.modules(RECORD, PASSES)

    # Sets what the two add-on test modules hold.
    def modules(self, bridges, generators):
        write_text(self.path("repo", "blender", "tests", "test_bridges.py"), bridges)
        write_text(self.path("repo", "blender", "tests", "test_addon_generators.py"), generators)

    # Writes five made-up add-ons with the ids the gate asks for, leaving out the one named.
    def five(self, folder=None, without=None):
        folder = folder or self.path("addons")
        os.makedirs(os.path.join(folder, "blender_org"), exist_ok=True)
        for name in FIVE:
            if name != without:
                write_text(os.path.join(folder, "blender_org", name, "blender_manifest.toml"),
                           'schema_version = "1.0.0"\nid = "%s"\nversion = "1.0.0"\n' % name)
                write_text(os.path.join(folder, "blender_org", name, "__init__.py"), "def register():\n    pass\n")
        return folder

    # Runs the gate on a source, or with the variable unset.
    def gate(self, source=None, script=None, arguments=("--", "--addons")):
        changes = {} if source is None else {"PLYNTH_TEST_ADDONS": source}
        return self.blender(script or self.runner, arguments, **changes)

    # With the five and no skip the gate passes, says so, and leaves the source as it was.
    def test_passes(self):
        write_text(self.path("repo", "blender", "tests", "test_other.py"), FAILS)
        source = self.five()
        before = fingerprint(source)
        code, text = self.gate(source)
        self.assertEqual(code, 0, text[-3000:])
        self.assertIn("Add-on tests: 2 run, 0 skipped", text)
        self.assertEqual(fingerprint(source), before)

    # The child gets six threads, offline mode and a prepared folder in the temporary folder.
    def test_child(self):
        code, text = self.gate(self.five())
        self.assertEqual(code, 0, text[-3000:])
        with open(self.record, "r", encoding="utf-8") as handle:
            child = json.load(handle)
        before = child["argv"][:child["argv"].index("--python")]
        self.assertEqual(before[before.index("-t") + 1] if "-t" in before else None, "6", before)
        self.assertIn("--offline-mode", before)
        self.assertIn("--factory-startup", before)
        self.assertTrue(child["prepared"])
        temp = os.path.normcase(os.path.abspath(self.path("temp")))
        for name, _ in USER_VARIABLES:
            self.assertTrue(os.path.normcase(os.path.abspath(child["user"][name])).startswith(temp), name)

    # One skip fails.
    def test_skip(self):
        self.modules(PASSES, SKIPS)
        code, text = self.gate(self.five())
        self.assertNotEqual(code, 0, text[-3000:])
        self.assertIn("1 skipped", text)

    # No tests at all fails.
    def test_no_tests(self):
        self.modules("", "")
        code, text = self.gate(self.five())
        self.assertNotEqual(code, 0, text[-3000:])

    # A failing test fails.
    def test_failure(self):
        self.modules(PASSES, FAILS)
        code, text = self.gate(self.five())
        self.assertNotEqual(code, 0, text[-3000:])

    # An unset, empty or missing source fails.
    def test_no_source(self):
        for source in (None, "", self.path("nowhere")):
            with self.subTest(source=source):
                code, text = self.gate(source)
                self.assertNotEqual(code, 0, text[-3000:])

    # A source without any one of the five fails.
    def test_incomplete(self):
        for name in FIVE:
            with self.subTest(addon=name):
                code, text = self.gate(self.five(self.path("without_" + name.lower()), without=name))
                self.assertNotEqual(code, 0, text[-3000:])

    # An add-on without its manifest counts as not there.
    def test_no_manifest(self):
        source = self.five()
        os.remove(os.path.join(source, "blender_org", "bool_tool", "blender_manifest.toml"))
        code, text = self.gate(source)
        self.assertNotEqual(code, 0, text[-3000:])

    # A user folder, one around it and a prepared folder are refused and no test runs.
    def test_refused(self):
        foundation = self.path("appdata", "Blender Foundation")
        marked = self.path("marked")
        write_text(os.path.join(marked, extensions.MARKER), "")
        for source in (foundation, os.path.join(foundation, "copies"), self.path("appdata"),
                       self.path("user", "extensions"), marked):
            with self.subTest(source=source):
                if os.path.exists(self.record):
                    os.remove(self.record)
                self.five(source)
                copies = [os.path.join(source, "blender_org", name) for name in FIVE]
                before = [fingerprint(copy_folder) for copy_folder in copies]
                code, text = self.gate(source)
                self.assertNotEqual(code, 0, text[-3000:])
                self.assertFalse(os.path.exists(self.record), "the tests ran")
                self.assertEqual([fingerprint(copy_folder) for copy_folder in copies], before)

    # A source with a folder named Blender Foundation in its path is refused by the text, before any file access.
    def test_refused_by_text(self):
        spy = self.path("spy.py")
        write_text(spy, SPY)
        cases = (("gate-forbidden", "Blender Foundation", "Blender", "5.2", "extensions"),
                 ("gate-forbidden-b", "BLENDER FOUNDATION", "BLENDER", "4.5", "extensions"),
                 ("gate-forbidden-c", "Blender Foundation"),
                 ("gate-forbidden-d", "blender foundation", "copies"),
                 ("gate-forbidden-e", "Blender Foundation."))
        for parts in cases:
            for exists in (False, True):
                source = self.path("elsewhere", "there" if exists else "none", *parts)
                if exists:
                    self.five(source)
                for spelled in (source, source.replace("/", "\\")):
                    with self.subTest(source=spelled):
                        code, text = self.gate(spelled, script=spy)
                        self.assertNotEqual(code, 0, text[-3000:])
                        self.assertNotIn("TOUCHED", text)
                        self.assertFalse(os.path.exists(self.record), "the tests ran")

    # Without --addons the suite still passes when a test skips and no source is set.
    def test_everyday_run(self):
        self.modules(PASSES, SKIPS)
        code, text = self.gate(arguments=())
        self.assertEqual(code, 0, text[-3000:])


class TestAddonTests(ChildCase):
    # Copies the project test add-on into this test and returns that source folder.
    def addon_source(self):
        self.assertTrue(os.path.isfile(os.path.join(ADDON_DATA, "blender_org", ADDON, "blender_manifest.toml")),
                        "the test add-on is missing")
        source = self.path("addons")
        shutil.copytree(ADDON_DATA, source, ignore=shutil.ignore_patterns("__pycache__"))
        return source

    # Starts a child on an extensions folder that tries to switch the test add-on on.
    def enable(self, folder):
        script = self.path("enable.py")
        answer = self.path("answer.json")
        write_text(script, "import json, sys\nsys.path.insert(0, %r)\n" % helpers.BLENDER_FOLDER
                   + "import addon_utils\nfrom plynth.generating import addons\n"
                   + "on = addons.enable(%r)\nname = addons.find_module(%r)\n" % (ADDON, ADDON)
                   + "answer = {'on': bool(on), 'loaded': bool(name) and bool(addon_utils.check(name)[1])}\n"
                   + "with open(%r, 'w') as handle:\n    json.dump(answer, handle)\n" % answer)
        code, text = self.blender(script, BLENDER_USER_EXTENSIONS=folder)
        self.assertEqual(code, 0, text[-3000:])
        with open(answer, "r", encoding="utf-8") as handle:
            return json.load(handle)

    # It switches on from a prepared folder and the source is as it was.
    def test_prepared(self):
        source = self.addon_source()
        before = fingerprint(source)
        folder = self.path("job", "extensions")
        self.assertEqual(extensions.prepare(folder, source, (ADDON,)), [ADDON])
        self.assertEqual(self.enable(folder), {"on": True, "loaded": True})
        self.assertEqual(fingerprint(source), before)

    # It refuses in a folder without the marker.
    def test_unprepared(self):
        source = self.addon_source()
        folder = self.path("job", "extensions")
        shutil.copytree(os.path.join(source, "blender_org", ADDON), os.path.join(folder, "user_default", ADDON))
        self.assertEqual(self.enable(folder), {"on": False, "loaded": False})

    # A marker in a Blender user folder is worth nothing.
    def test_marked_user_folder(self):
        source = self.addon_source()
        folder = self.path("appdata", "Blender Foundation", "Blender", "5.2", "extensions")
        shutil.copytree(os.path.join(source, "blender_org", ADDON), os.path.join(folder, "user_default", ADDON))
        write_text(os.path.join(folder, extensions.MARKER), "")
        self.assertEqual(self.enable(folder), {"on": False, "loaded": False})

    # The worker builds a generator that needs it from a prepared folder and leaves it out without one.
    def test_worker_build(self):
        source = self.addon_source()
        prepared = self.path("job", "extensions")
        extensions.prepare(prepared, source, (ADDON,))
        declaration = {"format": "plynth-generator", "version": 1, "name": "Test part",
                       "description": "A part for tests.", "addons": [ADDON],
                       "size": {"width": 1.0, "depth": 1.0, "height": 1.0},
                       "params": [{"id": "amount", "label": "Amount", "type": "number", "default": 1.0}]}
        file = self.path("generators", "needs_test.py")
        write_text(file, '"""plynth-generator\n%s\n"""\n\n\ndef generate(gen):\n' % json.dumps(declaration)
                   + "    v = [(x / 2, y / 2, z) for z in (0, 1) for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1))]\n"
                   + "    f = [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]\n"
                   + "    return [gen.mesh_object('box', v, f)]\n")
        params = upgrade(copy.deepcopy(load_params("square_room")))
        params["slots"] = sorted(params["slots"] + [{"name": "Generated", "material": None, "surface": "Generated"}],
                                 key=lambda row: row["name"])
        params["generators"] = [{"id": "needs_test", "file": file, "addons": [ADDON]}]
        params["floors"][0]["generator_items"] = [
            {"id": "g1", "generator": "needs_test", "params": {"amount": 1.0}, "location": [2.0, -2.0, 0.0],
             "rotation_z": 0.0, "size": {"width": 1.0, "depth": 1.0, "height": 1.0}, "room": None, "wall": None,
             "slots": {}}]
        params["output_folder"] = self.path("build")
        worker = os.path.join(helpers.BLENDER_FOLDER, "plynth_worker.py")
        for folder, made in ((prepared, 1), (self.path("user", "extensions"), 0)):
            with self.subTest(made=made):
                job_file, result_file = self.write_job("build", params, job_id="build-1")
                code, text = self.blender(worker, ("--", "--job", job_file), BLENDER_USER_EXTENSIONS=folder)
                self.assertEqual(code, 0, text[-3000:])
                result = self.read_result(result_file)
                self.assertTrue(result["ok"], result["error"])
                self.assertEqual(result["output"]["counts"]["generators"], made)
                self.assertEqual([(p["code"], p["severity"]) for p in result["output"]["problems"]],
                                 [] if made else [("generator_unavailable", "warning")])


class GateRegistrationTests(helpers.TempFolderTest):
    # Reads the CMake file of the Blender tests.
    def setUp(self):
        super().setUp()
        with open(os.path.join(helpers.BLENDER_FOLDER, "CMakeLists.txt"), "r", encoding="utf-8") as handle:
            self.text = handle.read()

    # Returns the body of one add_test.
    def command(self, name):
        found = re.search(r"add_test\(\s*NAME\s+%s\s(.*?)\)" % name, self.text, re.S)
        self.assertIsNotNone(found, "%s is not registered" % name)
        return found.group(1)

    # Both Blender suites start with six threads, offline, before the script.
    def test_commands(self):
        for name in ("blender_python", "blender_addons"):
            with self.subTest(test=name):
                before = self.command(name).split("--python ")[0]
                self.assertRegex(before, r"(^|\s)-t\s+6(\s|$)")
                self.assertIn("--offline-mode", before)

    # The add-on suite has both labels, the private folders and no way to skip.
    def test_addon_suite(self):
        self.assertIn("--addons", self.command("blender_addons"))
        properties = " ".join(m for m in re.findall(r"set_tests_properties\((.*?)\)", self.text, re.S)
                              if "blender_addons" in m)
        self.assertRegex(properties, r'LABELS\s+"?(blender;addons|addons;blender)"?')
        self.assertIn("FIXTURES_REQUIRED blender_private", properties)
        for variable, _ in USER_VARIABLES:
            self.assertIn(variable, self.text)
        self.assertNotIn("SKIP_RETURN_CODE", self.text)
        self.assertNotIn("SKIP_REGULAR_EXPRESSION", self.text)


# Records every file call that names a path below the folder while the block runs.
@contextlib.contextmanager
def file_calls(folder):
    wanted = os.path.normcase(os.path.abspath(folder))
    seen = []

    # Wraps one file call.
    def watched(name, call):
        def wrapper(*args, **kwargs):
            for value in args[:2]:
                if isinstance(value, (str, os.PathLike)) and wanted in os.path.normcase(str(value)):
                    seen.append((name, str(value)))
            return call(*args, **kwargs)
        return wrapper

    patches = []
    for module, names in ((os, ("stat", "lstat", "listdir", "scandir", "open", "makedirs", "mkdir")),
                          (os.path, ("exists", "isfile", "isdir", "islink", "realpath", "getsize")),
                          (glob, ("glob", "iglob")), (builtins, ("open",))):
        for name in names:
            if hasattr(module, name):
                patches.append(mock.patch.object(module, name, watched(name, getattr(module, name))))
    for patch in patches:
        patch.start()
    try:
        yield seen
    finally:
        for patch in reversed(patches):
            patch.stop()


class FolderRuleTests(ChildCase):
    # Returns each forbidden folder as it is spelled and as it is on disk, filled with a small add-on.
    def named_folders(self):
        cases = []
        for number, (parts, spelled) in enumerate((
                (("Blender Foundation",), ("Blender Foundation",)),
                (("Blender Foundation", "copies"), ("Blender Foundation", "copies")),
                (("BLENDER FOUNDATION",), ("BLENDER FOUNDATION",)),
                (("blender foundation", "copies"), ("blender foundation", "copies")),
                (("Blender Foundation",), ("Blender Foundation.",)),
                (("Blender Foundation", "copies"), ("Blender Foundation.", "copies")),
                (("Blender Foundation",), ("Blender Foundation ",)),
                (("Blender Foundation",), ("Blender Foundation. .",)))):
            base = self.path("elsewhere", str(number))
            self.mark_and_fill(os.path.join(base, *parts))
            cases.append((os.path.join(base, *spelled), os.path.join(base, *parts)))
        return cases

    # Puts a small add-on into a folder, and the marker when asked.
    def mark_and_fill(self, folder, marked=False):
        write_text(os.path.join(folder, "blender_org", ADDON, "blender_manifest.toml"), 'id = "%s"\n' % ADDON)
        if marked:
            write_text(os.path.join(folder, extensions.MARKER), "")

    # check_source refuses the name by the text alone.
    def test_check_source_name(self):
        for given, real in self.named_folders():
            with self.subTest(folder=given):
                before = fingerprint(real)
                with file_calls(os.path.dirname(real)) as seen, self.assertRaises(ValueError):
                    extensions.check_source(given)
                self.assertEqual(seen, [])
                self.assertEqual(fingerprint(real), before)

    # prepare refuses the name for the source and for the target, by the text alone.
    def test_prepare_name(self):
        good = self.path("addons")
        self.mark_and_fill(good)
        for number, (given, real) in enumerate(self.named_folders()):
            target = self.path("job", str(number))
            for folder, source in ((target, given), (given, good)):
                with self.subTest(folder=folder, source=source):
                    before = fingerprint(real)
                    with file_calls(os.path.dirname(real)) as seen, self.assertRaises(ValueError):
                        extensions.prepare(folder, source, (ADDON,))
                    self.assertEqual(seen, [])
                    self.assertEqual(fingerprint(real), before)
                    self.assertFalse(os.path.exists(target))

    # A marker in such a folder is worth nothing to is_private.
    def test_is_private_name(self):
        for given, real in self.named_folders():
            with self.subTest(folder=given):
                write_text(os.path.join(real, extensions.MARKER), "")
                before = fingerprint(real)
                with mock.patch.dict(os.environ, {"BLENDER_USER_EXTENSIONS": given}):
                    with file_calls(os.path.dirname(real)) as seen:
                        self.assertFalse(extensions.is_private())
                self.assertEqual(seen, [])
                self.assertEqual(fingerprint(real), before)

    # A junction to such a folder is refused by all three and the target is untouched.
    def test_junction_to_name(self):
        for tail in ((), ("copies",)):
            with self.subTest(tail=tail):
                label = "below" if tail else "itself"
                target = os.path.join(self.path("linked", label), "Blender Foundation", *tail)
                self.mark_and_fill(target, marked=True)
                good = self.path("addons")
                self.mark_and_fill(good)
                link = self.path("link_" + label)
                _winapi.CreateJunction(os.path.abspath(target), os.path.abspath(link))
                self.addCleanup(os.rmdir, link)
                before = fingerprint(target)
                with self.assertRaises(ValueError):
                    extensions.check_source(link)
                with self.assertRaises(ValueError):
                    extensions.prepare(self.path("job"), link, (ADDON,))
                with self.assertRaises(ValueError):
                    extensions.prepare(os.path.join(link, "job"), good, (ADDON,))
                with mock.patch.dict(os.environ, {"BLENDER_USER_EXTENSIONS": link}):
                    self.assertFalse(extensions.is_private())
                self.assertEqual(fingerprint(target), before)
                self.assertFalse(os.path.exists(self.path("job")))

    # A folder around the fake user folder is neither a source nor a target.
    def test_around_user_folder(self):
        good = self.path("addons")
        self.mark_and_fill(good)
        holder = self.path("holder")
        with mock.patch.dict(os.environ, {"APPDATA": self.path("holder", "roaming")}):
            for made in (False, True):
                with self.subTest(made=made):
                    if made:
                        self.mark_and_fill(holder)
                    before = fingerprint(holder)
                    with self.assertRaises(ValueError):
                        extensions.check_source(holder)
                    with self.assertRaises(ValueError):
                        extensions.prepare(self.path("job"), holder, (ADDON,))
                    with self.assertRaises(ValueError):
                        extensions.prepare(holder, good, (ADDON,))
                    self.assertEqual(os.path.exists(holder), made)
                    self.assertEqual(fingerprint(holder), before)
                    self.assertFalse(os.path.exists(self.path("job")))

    # A name that only starts the same is allowed.
    def test_like_name(self):
        source = self.path("Blender Foundation copies", "addons")
        self.mark_and_fill(source)
        extensions.check_source(source)
        folder = self.path("BlenderFoundation", "job")
        self.assertEqual(extensions.prepare(folder, source, (ADDON,)), [ADDON])
        with mock.patch.dict(os.environ, {"BLENDER_USER_EXTENSIONS": folder}):
            self.assertTrue(extensions.is_private())


if __name__ == "__main__":
    unittest.main()
