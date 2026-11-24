"""Checks what each child Blender of the suite would get, without starting one.

Every test works in its own temporary folder with a made-up user. Starts and deletes are mocked, so this file
also runs alone with a plain Python: python -I test_child_isolation.py
"""
import ast
import atexit
import collections
import contextlib
import importlib
import importlib.util
import io
import os
import re
import runpy
import shutil
import sys
import tempfile
import types
import unittest
from unittest import mock

TESTS = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(TESTS, "tools")
CMAKE_FILE = os.path.join(os.path.dirname(TESTS), "CMakeLists.txt")
EXTENSIONS_FILE = os.path.join(os.path.dirname(TESTS), "plynth", "extensions.py")
# The real fake helper program the build names, read before any test clears the environment.
HELPER_VARIABLE = "PLYNTH_FAKE_STORE_HELPER"
REAL_HELPER = os.environ.get(HELPER_VARIABLE, "")

BLENDER_FOLDERS = ("BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                   "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES")
PRIVATE_VARIABLES = BLENDER_FOLDERS + ("APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME",
                                       "PLYNTH_BLENDER_PRIVATE", "PLYNTH_CONFIG_DIR")
# A parent that has all the folders set, and one that has none of them.
PARENTS = ("set", "unset")

# The file and function of each start that is checked here, and the call it makes.
SUITE_STARTS = {("test_probe.py", "ProbeTests.test_worker_process"): "subprocess.run",
                ("test_userprefs.py", "Fixture.save"): "subprocess.run"}
STORE_STARTS = {("tools/make_fake_blenderkit_config.py", "make_config"): "subprocess.run"}
EDGE_STARTS = {("blenderkit_edge_helpers.py", "run_blender"): "subprocess.run",
               ("blenderkit_edge_helpers.py", "make_user"): "run_blender",
               ("blenderkit_edge_helpers.py", "EdgeTest.start"): "run_blender",
               ("test_blenderkit_autoexec.py", "AutoexecTests.setUpClass"): "run_blender",
               # The same scenario user and run_blender as setUpClass, which EdgeChildTests checks.
               ("test_blenderkit_autoexec.py", "AutoexecTests.open_with_driver"): "run_blender"}
# The plain fetch tests start the stand-in Blender, at normal or low integrity, and fake helpers.
PLAIN_STARTS = {("plain/fetch_rig.py", "run_child"): "subprocess.run",
                ("plain/fetch_rig.py", "_low_api"): "CreateProcessAsUserW",
                ("plain/fetch_rig.py", "_run_low"): "CreateProcessAsUserW",
                ("plain/test_fetch_rules.py", "FetchCase.start_user_helper"): "subprocess.Popen",
                ("plain/test_fetch_guard.py", "GuardEvents.setUpClass"): "subprocess.Popen",
                # Two attempts the guard must refuse, run only inside the stand-in under the guard.
                ("plain/guard_probe.py", "main"): "subprocess.run"}
# The scratch runs of S-R 1 to 9: real Blender children, and the fake helper that S-R 7 starts.
SCRATCH_STARTS = {("scratch/sr7_low_integrity.py", "main"): "subprocess.Popen",
                  ("test_blender_background_rules.py", "BackgroundRules.setUpClass"): "run_blender",
                  ("test_blender_background_rules.py", "OperatorCloses.setUpClass"): "run_blender",
                  ("test_blender_background_rules.py", "StartupOrder.test_sr5_order"): "run_blender"}
# The fake add-on starts its fake helper as the real one does. The fetch gives it only fetch folders, which
# test_fetch_rules checks (test_preferences_copy_and_child_folders_are_private).
ADDON_STARTS = {("data/fake_blenderkit/blenderkit/client_lib.py", "start_blenderkit_client"): "subprocess.Popen"}
ADDON_HANDED_ON = ("data/fake_blenderkit/blenderkit/client_lib.py", "start_blenderkit_client", "subprocess handed on")
# Places with more than one start of the same kind.
REPEATED = {("plain/guard_probe.py", "main", "subprocess.run"): 2,
            ("test_blender_background_rules.py", "StartupOrder.test_sr5_order", "run_blender"): 2}
PLAIN = os.path.join(TESTS, "plain")
HAS_PLAIN = os.path.isfile(os.path.join(PLAIN, "fetch_rig.py"))
RESTART_STARTS = {("run_tests.py", "run_with_private_extensions"): "subprocess.call"}
GATE_STARTS = {("test_addon_gate.py", "<top>"): "import _winapi",
               ("test_addon_gate.py", "ChildCase.blender"): "subprocess.run"}
# A helper that starts whatever it is given, so each place that uses it counts as a start too.
START_WRAPPERS = ("run_blender",)
STORE_PARTS = ("tools/make_fake_blenderkit_config.py", "test_blenderkit_fetch.py",
               "data/fake_blenderkit/blenderkit/__init__.py")
HAS_STORE = any(os.path.isfile(os.path.join(TESTS, part)) for part in STORE_PARTS)
HAS_EDGE = os.path.isfile(os.path.join(TESTS, "blenderkit_edge_helpers.py"))
HAS_RESTART = os.path.isfile(EXTENSIONS_FILE)
HAS_GATE = HAS_RESTART and os.path.isfile(os.path.join(TESTS, "test_addon_gate.py"))
NO_STORE = "this branch has no fake BlenderKit helper"

NOT_A_START = {"PIPE", "STDOUT", "DEVNULL", "TimeoutExpired", "CalledProcessError", "CompletedProcess",
               "SubprocessError", "list2cmdline"}
START_MODULES = {"subprocess", "multiprocessing", "pty", "_winapi", "_posixsubprocess", "nt", "posix"}
START_ANYWHERE = {"CreateProcess", "CreateProcessW", "CreateProcessA", "CreateProcessAsUserW",
                  "CreateProcessAsUserA", "CreateProcessWithTokenW", "ShellExecuteW", "ShellExecuteA",
                  "ShellExecuteExW", "WinExec", "create_subprocess_exec", "create_subprocess_shell",
                  "subprocess_exec", "subprocess_shell", "ProcessPoolExecutor", "path_open"}
BEFORE_SCRIPT = ("--python", "-P", "--python-expr", "--python-text", "--")
CMAKE_WORD = re.compile(r'"((?:\x5c.|[^"\x5c])*)"|(#[^\n]*)|([()])|((?:\x5c.|[^\s()"#\x5c])+)', re.DOTALL)


class ChildStopped(BaseException):
    """Ends the code under test at the moment a child would start."""


class NotAllowed(BaseException):
    """Ends the code under test at a delete or start that must not happen. No except Exception catches it."""


# Says whether a path is inside a folder, by name alone.
def inside(path, folder):
    own = os.path.join(os.path.normcase(os.path.abspath(folder)), "")
    given = os.path.join(os.path.normcase(os.path.abspath(path)), "")
    return given.startswith(own) and given != own


# Makes an empty file at a path, making its folder.
def empty_file(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").close()


# Says whether a name of the os module starts a program.
def is_os_start(name):
    return name in ("system", "popen", "startfile", "fork", "forkpty") or name.startswith(
        ("spawn", "exec", "posix_spawn"))


# Lists what is wrong with the options a child Blender would get.
def option_problems(args):
    places = [args.index(option) for option in BEFORE_SCRIPT if option in args]
    early = args[:min(places)] if places else args
    problems = []
    if not any(option in args for option in BEFORE_SCRIPT[:-1]):
        problems.append("no script")
    if "--offline-mode" not in early:
        problems.append("no --offline-mode before --python")
    if "--online-mode" in early:
        problems.append("--online-mode is given")
    counts = [early[index + 1:index + 2] for index, part in enumerate(early) if part in ("-t", "--threads")]
    if not counts or any(count != ["6"] for count in counts):
        problems.append("no -t 6 before --python")
    if "-b" not in early and "--background" not in early:
        problems.append("not in the background")
    return problems


class StartFinder(ast.NodeVisitor):
    """Finds every place in a source text that can start a process, with the function it is in."""

    # Begins with nothing found.
    def __init__(self):
        self.scope = []
        self.found = []

    # Notes one place.
    def add(self, node, what):
        self.found.append((".".join(self.scope) or "<top>", node.lineno, what))

    # Remembers the name of the class or function being read.
    def named(self, node):
        self.scope.append(node.name)
        self.generic_visit(node)
        self.scope.pop()

    visit_ClassDef = visit_FunctionDef = visit_AsyncFunctionDef = named

    # Notes process modules that come in directly or under another name.
    def visit_Import(self, node):
        for alias in node.names:
            top = alias.name.split(".")[0]
            if top in START_MODULES - {"subprocess"} or (top in ("subprocess", "os") and alias.asname):
                self.add(node, "import " + alias.name)

    # Notes process calls that are imported by name.
    def visit_ImportFrom(self, node):
        top = (node.module or "").split(".")[0]
        for alias in node.names:
            name = alias.name
            if ((top in START_MODULES and name not in NOT_A_START) or name in START_ANYWHERE
                    or (name in START_WRAPPERS and alias.asname)
                    or (top == "os" and (name == "*" or is_os_start(name)))):
                self.add(node, "from %s import %s" % (top, name))

    # Notes process calls named with a dot, also when they are only handed on.
    def visit_Attribute(self, node):
        owner = node.value.id if isinstance(node.value, ast.Name) else ""
        if owner == "subprocess":
            if node.attr not in NOT_A_START:
                self.add(node, "subprocess." + node.attr)
            return
        if (owner == "os" and is_os_start(node.attr)) or node.attr in START_ANYWHERE.union(START_WRAPPERS):
            self.add(node, node.attr)
        self.generic_visit(node)

    # Notes the process module handed on, and each use of a helper that starts a child.
    def visit_Name(self, node):
        if node.id == "subprocess":
            self.add(node, "subprocess handed on")
        if node.id in START_WRAPPERS:
            self.add(node, node.id)

    # Notes process modules and calls that are looked up by a text name.
    def visit_Call(self, node):
        called = getattr(node.func, "id", getattr(node.func, "attr", ""))
        texts = [arg.value if isinstance(arg, ast.Constant) and isinstance(arg.value, str) else ""
                 for arg in node.args]
        if called in ("import_module", "__import__") and texts and texts[0].split(".")[0] in START_MODULES:
            self.add(node, "import " + texts[0])
        if called == "getattr" and len(texts) > 1 and getattr(node.args[0], "id", "") == "os":
            if is_os_start(texts[1]):
                self.add(node, "os." + texts[1])
        if called in ("start", "fetch") and any(word.arg == "env" for word in node.keywords):
            self.add(node, called + " with its own env")
        self.generic_visit(node)

    # Notes a process module taken out of a table by its name.
    def visit_Subscript(self, node):
        if isinstance(node.slice, ast.Constant) and node.slice.value in ("subprocess", "_winapi"):
            self.add(node, "looked up " + node.slice.value)
        self.generic_visit(node)


# Returns the places in a source text that can start a process.
def find_starts(source):
    finder = StartFinder()
    finder.visit(ast.parse(source))
    return finder.found


class Sandbox(unittest.TestCase):
    """A made-up user in one temporary folder, where nothing can start and little can be deleted."""

    # Puts the guards in place before any tested code is loaded.
    def setUp(self):
        self.folder = os.path.realpath(tempfile.mkdtemp(prefix="plynth_isolation_"))
        self.addCleanup(shutil.rmtree, self.folder, True)
        self.scratch = os.path.join(self.folder, "scratch")
        self.user = os.path.join(self.folder, "user")
        os.makedirs(self.scratch)
        os.makedirs(self.user)
        self.blender = os.path.join(self.folder, "no_such_program", "blender.exe")
        self.count = 0
        self.starts = []
        self.deletes = []
        self.answer = None
        self.deletable = None
        self.deleting = False
        self.user_values = {name: os.path.join(self.user, name.lower()) for name in PRIVATE_VARIABLES}
        self.user_values["BLENDER_USER_SOMETHING_ELSE"] = os.path.join(self.user, "something_else")
        plain = {"TEMP": self.scratch, "TMP": self.scratch, "PATH": self.scratch}
        if os.environ.get("SYSTEMROOT"):
            plain["SYSTEMROOT"] = os.environ["SYSTEMROOT"]
        self.parents = {"set": dict(plain, **self.user_values), "unset": plain}
        self.patch(mock.patch.dict(os.environ, self.parents["set"], clear=True))
        self.patch(mock.patch.object(tempfile, "tempdir", self.scratch))
        # Loading test_userprefs would otherwise book a delete for when the program ends.
        self.patch(mock.patch.object(atexit, "register", lambda function, *args, **kwargs: function))
        self.guard_starts()
        self.guard_deletes()
        self.prepare_imports()

    # Switches a patch on until the test ends.
    def patch(self, patcher):
        patcher.start()
        self.addCleanup(patcher.stop)

    # Returns a new path inside the scratch folder.
    def new_path(self, *parts):
        self.count += 1
        return os.path.join(self.scratch, "case%d" % self.count, *parts)

    # Switches the environment to one of the parents.
    def use_parent(self, parent):
        os.environ.clear()
        os.environ.update(self.parents[parent])

    # Writes down what a child would get and starts nothing.
    def record_start(self, *args, **kwargs):
        command = args[0] if args else kwargs.get("args")
        listed = isinstance(command, (list, tuple)) and not kwargs.get("shell")
        env = kwargs.get("env")
        env = None if env is None else dict(env)
        made = {name for name, value in (env or {}).items()
                if isinstance(value, str) and inside(value, self.scratch) and os.path.isdir(value)}
        self.starts.append(types.SimpleNamespace(
            args=[str(part) for part in command] if listed else None, env=env, made=made))
        if self.answer is None:
            raise ChildStopped()
        return types.SimpleNamespace(returncode=0, stdout=self.answer, stderr="")

    # Mocks every way of starting a process.
    def guard_starts(self):
        # Stops any start that the tested code should not use.
        def refuse(*args, **kwargs):
            raise NotAllowed("another kind of process start was tried")

        self.patch(mock.patch("subprocess.run", self.record_start))
        for name in ("Popen", "call", "check_call", "check_output", "getoutput", "getstatusoutput"):
            self.patch(mock.patch("subprocess." + name, refuse))
        for name in dir(os):
            if is_os_start(name):
                self.patch(mock.patch.object(os, name, refuse))
        if os.name == "nt":
            self.patch(mock.patch("_winapi.CreateProcess", refuse))

    # Says whether the test in progress allows this path to be deleted.
    def may_delete(self, path):
        if self.deletable is None:
            return False
        path = os.path.abspath(os.fsdecode(path))
        return inside(path, self.folder) and inside(path, self.deletable)

    # Mocks every deleting or moving call. Only what a test allows inside its own folder is really changed.
    def guard_deletes(self):
        real_rmtree = shutil.rmtree

        # Writes down a folder delete and does it only where the test allows it.
        def remove_tree(path, *args, **kwargs):
            self.deletes.append(os.fsdecode(path))
            if not self.may_delete(path):
                raise NotAllowed("would delete %s" % os.fsdecode(path))
            self.deleting = True
            try:
                return real_rmtree(path, *args, **kwargs)
            finally:
                self.deleting = False

        # Wraps a call that removes or moves one thing.
        def guarded(real, paths):
            # Lets the call through only where the test allows it.
            def call(*args, **kwargs):
                if not self.deleting:
                    self.deletes.append(os.fsdecode(args[0]))
                    if not all(self.may_delete(path) for path in args[:paths]):
                        raise NotAllowed("would remove or move %s" % os.fsdecode(args[0]))
                return real(*args, **kwargs)
            return call

        self.patch(mock.patch.object(shutil, "rmtree", remove_tree))
        for module, name, paths in ((os, "remove", 1), (os, "unlink", 1), (os, "rmdir", 1), (os, "rename", 2),
                                    (os, "replace", 2), (shutil, "move", 2)):
            self.patch(mock.patch.object(module, name, guarded(getattr(module, name), paths)))

    # Lists the loaded modules that come from the tests folder, and the ones that are replaced here.
    def local_modules(self, replaced):
        found = {}
        for name, module in list(sys.modules.items()):
            file = getattr(module, "__dict__", {}).get("__file__")
            if name in replaced or (name not in (__name__, "__main__")
                                    and isinstance(file, str) and inside(file, TESTS)):
                found[name] = module
        return found

    # Gives the tested files a fake bpy and worker package, and fresh copies of each other.
    def prepare_imports(self):
        fakes = {name: types.ModuleType(name) for name in ("bpy", "plynth", "plynth.runner", "plynth.tasks",
                                                           "plynth.tasks.probe", "plynth.userprefs",
                                                           "plynth.extensions")}
        fakes["bpy"].app = types.SimpleNamespace(binary_path=self.blender, version=(5, 2, 0))
        fakes["plynth"].__path__ = []
        fakes["plynth.tasks"].__path__ = []
        fakes["plynth"].userprefs = fakes["plynth.userprefs"]
        fakes["plynth.tasks"].probe = fakes["plynth.tasks.probe"]
        fakes["plynth.runner"].run_job_file = lambda job_file: None
        saved = self.local_modules(fakes)

        # Puts the modules of the suite back.
        def restore():
            for name in self.local_modules(fakes):
                del sys.modules[name]
            sys.modules.update(saved)

        self.addCleanup(restore)
        self.patch(mock.patch.object(sys, "path", [TESTS, TOOLS] + list(sys.path)))
        self.patch(mock.patch.object(sys, "dont_write_bytecode", True))
        for name in saved:
            del sys.modules[name]
        sys.modules.update(fakes)

    # Loads one file of the tests folder.
    def load(self, name):
        return importlib.import_module(name)

    # Runs code up to the start of its child and returns what that child would get.
    def child_of(self, call):
        before = len(self.starts)
        self.answer = None
        try:
            call()
        except ChildStopped:
            pass
        self.assertEqual(len(self.starts), before + 1, "expected one child start")
        return self.starts[-1]

    # Lists what is wrong with the environment of one child.
    def folder_problems(self, start):
        if start.env is None:
            return ["no env is given, so the child inherits everything"]
        problems = [name + " is not set" for name in PRIVATE_VARIABLES if name not in start.env]
        for name, value in start.env.items():
            value = str(value)
            if name.upper() in PRIVATE_VARIABLES or name.upper().startswith("BLENDER_USER_"):
                if not os.path.isabs(value) or not inside(value, self.scratch):
                    problems.append(name + " is not a scratch folder")
                elif name not in start.made:
                    problems.append(name + " names a folder that was not made")
            elif os.path.isabs(value) and inside(value, self.user):
                problems.append(name + " holds a folder of the user")
        return sorted(problems)

    # Fails for each child that does not get only scratch folders that exist.
    def check_folders(self, drive):
        self.deletable = self.scratch
        for parent in PARENTS:
            self.use_parent(parent)
            for number, start in enumerate(drive()):
                with self.subTest(parent=parent, child=number):
                    self.assertEqual(self.folder_problems(start), [])

    # Fails for each child that is online, has no thread limit or gets its options too late.
    def check_options(self, drive):
        self.deletable = self.scratch
        for number, start in enumerate(drive()):
            with self.subTest(child=number):
                self.assertIsNotNone(start.args, "the child needs a list of arguments and no shell")
                self.assertEqual(start.args[0], self.blender)
                self.assertEqual(option_problems(start.args), [])

    # Returns the child of the worker process test.
    def worker_children(self):
        prefs_tests = self.load("test_userprefs")
        probe_tests = self.load("test_probe")
        self.count += 1
        fixture = prefs_tests.Fixture("signed_in_%d" % self.count)
        os.makedirs(fixture.root)
        case = probe_tests.ProbeTests("test_worker_process")
        with mock.patch.object(probe_tests, "variant", lambda name: fixture):
            case.setUp()
            try:
                return [self.child_of(case.test_worker_process)]
            finally:
                case.doCleanups()

    # Returns the children that save preference fixtures, with and without the optional settings.
    def fixture_children(self):
        module = self.load("test_userprefs")
        found = []
        for options in ({}, {"enable": ["blenderkit"], "key": module.FAKE_KEY, "global_dir": self.new_path("data")}):
            self.count += 1
            fixture = module.Fixture("saved_%d" % self.count)
            found.append(self.child_of(lambda: fixture.save(**options)))
        return found


class SuiteChildTests(Sandbox):
    # The worker child sees only scratch folders that exist.
    def test_worker_child_folders(self):
        self.check_folders(self.worker_children)

    # The worker child is offline and limited to six threads.
    def test_worker_child_options(self):
        self.check_options(self.worker_children)

    # The fixture child sees only scratch folders that exist.
    def test_fixture_child_folders(self):
        self.check_folders(self.fixture_children)

    # The fixture child is offline and limited to six threads.
    def test_fixture_child_options(self):
        self.check_options(self.fixture_children)

    # Both of these children still start from factory settings.
    def test_factory_startup_stays(self):
        self.deletable = self.scratch
        for start in self.worker_children() + self.fixture_children():
            self.assertIn("--factory-startup", start.args[:start.args.index("--python")])


class StoreSandbox(Sandbox):
    """The same sandbox, for the fake BlenderKit helper and its test."""

    # Skips where there is no such helper.
    def setUp(self):
        if not HAS_STORE:
            self.skipTest(NO_STORE)
        super().setUp()

    # Loads the helper as a new process would see it.
    def fresh_tool(self):
        sys.modules.pop("make_fake_blenderkit_config", None)
        return self.load("make_fake_blenderkit_config")

    # Returns the children that save each kind of fake config.
    def config_children(self):
        tool = self.fresh_tool()
        found = []
        for options in ({}, {"signed_out": True}, {"disabled": True}, {"missing": True}):
            folder = self.new_path("config")
            found.append(self.child_of(lambda: tool.make_config(folder, **options)))
        return found


class StoreChildTests(StoreSandbox):
    # The child that saves a fake config sees only scratch folders that exist.
    def test_config_child_folders(self):
        self.check_folders(self.config_children)

    # The child that saves a fake config is offline and limited to six threads.
    def test_config_child_options(self):
        self.check_options(self.config_children)


class FakeConfigFolderTests(StoreSandbox):
    # Makes a folder that someone else filled, inside the temporary folder.
    def foreign(self, files=(), folders=()):
        root = self.new_path("someone")
        os.makedirs(root)
        for part in folders:
            os.makedirs(os.path.join(root, part), exist_ok=True)
        for part in files:
            os.makedirs(os.path.dirname(os.path.join(root, part)), exist_ok=True)
            with open(os.path.join(root, part), "wb") as handle:
                handle.write(b"keep " + part.encode("utf-8"))
        return root

    # Lists everything under a path of the test, with the bytes of each file.
    def contents(self, root):
        if os.path.isfile(root):
            with open(root, "rb") as handle:
                return [handle.read()]
        if not os.path.exists(root):
            return None
        found = []
        for place, folders, files in os.walk(root):
            found += [(os.path.relpath(os.path.join(place, name), root), None) for name in folders]
            for name in files:
                with open(os.path.join(place, name), "rb") as handle:
                    found.append((os.path.relpath(os.path.join(place, name), root), handle.read()))
        return sorted(found, key=lambda entry: entry[0])

    # Fails unless the call refuses with no delete, no start and no change.
    def check_refused(self, call, root, errors=ValueError):
        self.deletable = None
        self.answer = None
        del self.deletes[:]
        del self.starts[:]
        before = self.contents(root)
        problems = []
        try:
            call()
            problems.append("was accepted")
        except NotAllowed as error:
            problems.append(str(error))
        except ChildStopped:
            problems.append("would start Blender")
        except errors:
            pass
        if self.deletes and not problems:
            problems.append("tried to delete")
        if self.starts and not problems:
            problems.append("tried to start")
        if self.contents(root) != before:
            problems.append("changed the folder")
        self.assertEqual(problems, [])

    # Makes a fake config with the helper itself.
    def made_config(self, *parts):
        folder = self.new_path(*parts)
        self.deletable = self.scratch
        self.answer = "FAKE CONFIG SAVED"
        self.fresh_tool().make_config(folder)
        return folder

    # A folder with anything in it that the helper did not make is refused.
    def test_filled_folder_is_refused(self):
        tool = self.fresh_tool()
        cases = [((os.path.join(name, "keep.txt"),), ()) for name in ("config", "scripts", "extensions", "download")]
        cases += [((), ("config",)), (("notes.txt",), ()), ((".hidden",), ()), ((), ("other",))]
        for number, (files, folders) in enumerate(cases):
            for action in (tool.make_config, tool.child_env):
                with self.subTest(case=number, action=action.__name__):
                    root = self.foreign(files, folders)
                    self.check_refused(lambda: action(root), root)

    # A user folder that looks like a fake config is refused, also when APPDATA points at scratch.
    def test_lookalike_is_refused_with_scratch_environment(self):
        tool = self.fresh_tool()
        folders = ("resources", "config", "scripts", "extensions", "datafiles", "download", "appdata", "local",
                   "home", "xdg", "plynth_private", "plynth_config")
        files = (os.path.join("config", "userpref.blend"), os.path.join("config", "preferences.json"),
                 os.path.join("scripts", "addons", "blenderkit", "__init__.py"),
                 os.path.join("extensions", "user_default", "keep.py"), os.path.join("download", "keep.blend"))
        for parent in PARENTS:
            for action in (tool.make_config, tool.child_env):
                with self.subTest(parent=parent, action=action.__name__):
                    self.use_parent(parent)
                    root = self.foreign(files, folders)
                    self.check_refused(lambda: action(root), root)

    # Being around or inside a made config does not make a folder the helper's own.
    def test_around_made_config_is_refused(self):
        for part in ("parent", "child"):
            with self.subTest(part=part):
                made = self.made_config("holder", "made")
                root = os.path.dirname(made) if part == "parent" else os.path.join(made, "scripts")
                tool = self.fresh_tool()
                self.check_refused(lambda: tool.make_config(root), root)

    # An empty name or a dot means the working folder, which is refused when it is filled.
    def test_working_folder_is_refused(self):
        tool = self.fresh_tool()
        for given in ("", "."):
            with self.subTest(given=given):
                root = self.foreign((os.path.join("config", "keep.txt"),))
                old = os.getcwd()
                os.chdir(root)
                try:
                    self.check_refused(lambda: tool.make_config(given), root)
                finally:
                    os.chdir(old)

    # A folder that cannot be listed is not taken to be empty.
    def test_unreadable_folder_is_refused(self):
        tool = self.fresh_tool()
        root = self.foreign((os.path.join("config", "keep.txt"),))
        real = {name: getattr(os, name) for name in ("listdir", "scandir")}

        # Makes a listing that fails for that one folder.
        def failing(name):
            # Fails as a locked folder does.
            def listing(path=".", *args, **kwargs):
                if isinstance(path, (str, os.PathLike)) and os.path.abspath(path) == os.path.abspath(root):
                    raise PermissionError("locked")
                return real[name](path, *args, **kwargs)
            return listing

        # Runs the helper while the folder cannot be listed.
        def call():
            with mock.patch.object(os, "listdir", failing("listdir")):
                with mock.patch.object(os, "scandir", failing("scandir")):
                    tool.make_config(root)

        self.check_refused(call, root, (ValueError, OSError))

    # A file given in place of a folder is refused before any delete.
    def test_file_is_refused(self):
        tool = self.fresh_tool()
        root = os.path.join(self.foreign(("plain.txt",)), "plain.txt")
        self.check_refused(lambda: tool.make_config(root), root, (ValueError, OSError))

    # The command line refuses a filled folder too.
    def test_command_line_is_refused(self):
        tool = self.fresh_tool()
        root = self.foreign((os.path.join("config", "keep.txt"),))
        script = os.path.join(TOOLS, "make_fake_blenderkit_config.py")
        with mock.patch.object(sys, "argv", [script, root]):
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.check_refused(tool.main, root, (ValueError, SystemExit))

    # The known user folders stay refused when empty or missing, also one that the helper made earlier.
    def test_known_user_folders_stay_refused(self):
        places = (("APPDATA", "Blender Foundation"), ("APPDATA", "Plynth"), ("LOCALAPPDATA", "Blender Foundation"),
                  ("LOCALAPPDATA", "Plynth"), ("USERPROFILE", "blenderkit_data"),
                  ("XDG_DATA_HOME", "blenderkit_data"), ("BLENDER_USER_CONFIG", ""))
        tool = self.fresh_tool()
        for name, part in places:
            for exists in (False, True):
                with self.subTest(name=name, part=part, exists=exists):
                    root = os.path.join(self.user_values[name], part, "exists" if exists else "missing")
                    if exists:
                        os.makedirs(root)
                    self.check_refused(lambda: tool.make_config(root), root)
        made = self.made_config("pretend", "Blender Foundation")
        os.environ["APPDATA"] = os.path.dirname(made)
        tool = self.fresh_tool()
        self.check_refused(lambda: tool.make_config(made), made)

    # A missing folder and an empty one are accepted.
    def test_empty_folder_is_accepted(self):
        for exists in (False, True):
            with self.subTest(exists=exists):
                folder = self.new_path("empty one")
                if exists:
                    os.makedirs(folder)
                self.deletable = folder
                self.answer = "FAKE CONFIG SAVED"
                self.fresh_tool().make_config(folder)
                self.assertTrue(os.path.isfile(os.path.join(folder, "scripts", "addons", "blenderkit", "__init__.py")))

    # A folder the helper made is accepted later, also after a failed run, and only its own parts are renewed.
    def test_made_folder_is_accepted_again(self):
        folder = self.new_path("made")
        self.deletable = folder
        self.child_of(lambda: self.fresh_tool().make_config(folder))
        stale = os.path.join(folder, "config", "stale.txt")
        kept = os.path.join(folder, "notes.txt")
        for path in (stale, kept):
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("text")
        self.answer = "FAKE CONFIG SAVED"
        self.fresh_tool().make_config(folder)
        self.assertFalse(os.path.exists(stale))
        self.assertTrue(os.path.isfile(kept))
        self.fresh_tool().child_env(folder)

        other = self.new_path("env_first")
        self.deletable = other
        self.fresh_tool().child_env(other)
        self.fresh_tool().make_config(other)


class EdgeChildTests(StoreSandbox):
    # Skips where there are no scenario children.
    def setUp(self):
        if not HAS_EDGE:
            self.skipTest("this branch has no scenario children")
        super().setUp()

    # Returns the children that make and change a scenario user, and the one that fetches.
    def edge_children(self):
        helper = self.load("blenderkit_edge_helpers")
        changes = os.path.isfile(os.path.join(TESTS, "test_blenderkit_autoexec.py"))
        autoexec = self.load("test_blenderkit_autoexec") if changes else None
        tests = autoexec.AutoexecTests if changes else helper.EdgeTest
        # The source and driver .blend files are made in this process with bpy, which is fake here. They start no
        # child, so empty files stand in for them.
        self.patch(mock.patch.object(helper, "make_source", empty_file))
        if changes:
            self.patch(mock.patch.object(autoexec, "make_driver_blend", lambda path, marker: empty_file(path)))

        before = len(self.starts)
        # The scenario user gets a copy of the real helper, so the patched environment needs the build's name
        # for it back. It is a build file, not a user folder.
        if REAL_HELPER:
            os.environ[HELPER_VARIABLE] = REAL_HELPER
        # The fetch runs on a thread of the helper, so every child here gets an answer and none is stopped.
        self.answer = "FAKE CONFIG SAVED AUTO RUN SAVED"
        tests.setUpClass()
        case = tests("test_disable_autoexec_wins" if changes else "runTest")
        case.setUp()
        # The fetch starts Blender at low integrity. That start is replaced by a record until the helper's
        # thread is done, so nothing starts.
        low = lambda command, env, cwd, timeout: self.record_start(command, env=env)
        with mock.patch.object(helper.fetch_rig, "_run_low", low):
            try:
                case.start(helper.MODEL, {})
            finally:
                case.doCleanups()
        self.assertEqual(len(self.starts), before + (3 if changes else 2))
        return self.starts[before:]

    # The scenario children see only scratch folders that exist.
    def test_edge_child_folders(self):
        self.check_folders(self.edge_children)

    # The scenario children are offline and limited to six threads.
    def test_edge_child_options(self):
        self.check_options(self.edge_children)


class PlainAndScratchChildTests(Sandbox):
    """The starts of the plain fetch tests and of the scratch runs, checked like every other child."""

    # Skips where there are no plain fetch tests, and lets the plain helpers load.
    def setUp(self):
        if not HAS_PLAIN:
            self.skipTest("this branch has no plain fetch tests")
        super().setUp()
        sys.path.insert(0, PLAIN)

    # Puts an empty stand-in for the fake helper program where the build would name it.
    def fake_helper(self):
        path = self.new_path("fake_store_helper.exe")
        os.makedirs(os.path.dirname(path))
        open(path, "wb").close()
        os.environ["PLYNTH_FAKE_STORE_HELPER"] = path
        return path

    # Returns the stand-in children of a fetch and of a low integrity start. The low start itself is replaced
    # by a record, so nothing starts.
    def stand_in_children(self):
        self.fake_helper()
        rig = self.load("fetch_rig")
        rules = self.load("test_fetch_rules")
        case = rules.FetchCase("assert_ok")
        case.setUp()
        found = []
        low = lambda command, env, cwd, timeout: self.record_start(command, env=env)
        try:
            with mock.patch.object(rig, "_run_low", low):
                # A fetch starts the stand-in at low integrity, as core does.
                found.append(self.child_of(lambda: case.rig.fetch(rig.MODEL)))
                found.append(self.child_of(lambda: rig.run_child(case.rig.environment(), ["--python", "probe.py"],
                                                                 case.rig.private, 60, low=True)))
            found.append(self.child_of(lambda: rig.run_child(case.rig.environment(), ["--python", "probe.py"],
                                                             case.rig.private, 60)))
        finally:
            case.doCleanups()
        return found

    # Returns the fake helper started as the user's own and the program the guard test leaves running.
    def helper_children(self):
        self.fake_helper()
        rules = self.load("test_fetch_rules")
        guard = self.load("test_fetch_guard")
        found = []
        with mock.patch("subprocess.Popen", self.record_start):
            case = rules.FetchCase("assert_ok")
            case.setUp()
            try:
                found.append(self.child_of(case.start_user_helper))
            finally:
                case.doCleanups()
            try:
                found.append(self.child_of(guard.GuardEvents.setUpClass))
            finally:
                guard.GuardEvents.case.doCleanups()
        return found

    # Returns the fake helper that S-R 7 starts from inside its Blender.
    def sr7_children(self):
        folder = self.new_path("labelled")
        os.makedirs(folder)
        sys.modules["bpy"].app.background = True
        script = os.path.join(TESTS, "scratch", "sr7_low_integrity.py")
        arguments = ["blender", "--", folder, self.new_path("outside.txt"), self.fake_helper()]
        with mock.patch("subprocess.Popen", self.record_start), mock.patch.object(sys, "argv", arguments):
            return [self.child_of(lambda: runpy.run_path(script, run_name="__main__"))]

    # Returns the real Blender children of the S-R runs: S-R 1 to 4, 8 and 9, S-R 3b, and both starts of S-R 5.
    def scratch_children(self):
        module = self.load("test_blender_background_rules")
        found = []
        for tests in (module.BackgroundRules, module.OperatorCloses):
            try:
                found.append(self.child_of(tests.setUpClass))
            finally:
                tests.doClassCleanups()
        record = self.record_start

        # Answers the setup start of S-R 5 as Blender would, so the test goes on to its check start.
        def answer(*args, **kwargs):
            command = args[0] if args else kwargs.get("args")
            if any(str(part).endswith("sr5_save_prefs.py") for part in command):
                open(os.path.join(kwargs["env"]["BLENDER_USER_CONFIG"], "userpref.blend"), "wb").close()
                self.answer = chr(10).join(("SR5 SAVED", "SR5 setup_sr5_addon: enabled", "SR5 setup_sr5_ext: enabled"))
            else:
                self.answer = None
            return record(*args, **kwargs)

        case = module.StartupOrder("test_sr5_order")
        before = len(self.starts)
        with mock.patch("subprocess.run", answer):
            try:
                case.test_sr5_order()
            except ChildStopped:
                pass
            finally:
                case.doCleanups()
        self.answer = None
        self.assertEqual(len(self.starts), before + 2, "expected the setup and the check start of S-R 5")
        return found + self.starts[before:]

    # Fails for each stand-in child that is not the stand-in, is online, has no thread limit or gets its
    # options too late.
    def check_stand_in_options(self, drive):
        self.deletable = self.scratch
        stand_in = os.path.join(PLAIN, "stand_in_blender.py")
        for number, start in enumerate(drive()):
            with self.subTest(child=number):
                self.assertIsNotNone(start.args, "the child needs a list of arguments and no shell")
                self.assertEqual(start.args[0], sys.executable)
                self.assertIn(stand_in, start.args)
                self.assertEqual(option_problems(start.args[start.args.index(stand_in) + 1:]), [])

    # The stand-in children, also the low integrity one, see only scratch folders that exist.
    def test_stand_in_child_folders(self):
        self.check_folders(self.stand_in_children)

    # The stand-in children are offline and limited to six threads, as a Blender child.
    def test_stand_in_child_options(self):
        self.check_stand_in_options(self.stand_in_children)

    # The fake helper and the program of the guard test see only scratch folders that exist.
    def test_helper_child_folders(self):
        self.check_folders(self.helper_children)

    # The fake helper of S-R 7 sees only scratch folders that exist.
    def test_sr7_child_folders(self):
        self.check_folders(self.sr7_children)

    # The Blender children of the S-R runs see only scratch folders that exist.
    def test_scratch_child_folders(self):
        self.check_folders(self.scratch_children)

    # The Blender children of the S-R runs are offline and limited to six threads.
    def test_scratch_child_options(self):
        self.check_options(self.scratch_children)


class RestartChildTests(Sandbox):
    # Skips where the suite does not start itself again.
    def setUp(self):
        if not HAS_RESTART:
            self.skipTest("this branch does not start the suite again")
        super().setUp()

    # Loads the runner and the real extensions module on the fake bpy.
    def load_restart(self):
        running = os.path.join(self.folder, "running")
        sys.modules["bpy"].utils = types.SimpleNamespace(resource_path=lambda kind: running,
                                                         user_resource=lambda kind: running)
        spec = importlib.util.spec_from_file_location("plynth.extensions", EXTENSIONS_FILE)
        extensions = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(extensions)
        sys.modules["plynth.extensions"] = sys.modules["plynth"].extensions = extensions
        return self.load("run_tests"), extensions

    # Returns the child that runs the suite again.
    def restart_children(self):
        runner, extensions = self.load_restart()

        # Writes down the restart, and whether its extensions folder was prepared.
        def record_call(*args, **kwargs):
            self.answer = ""
            self.record_start(*args, **kwargs)
            folder = (kwargs.get("env") or {}).get(extensions.ENV_NAME, "")
            self.prepared = inside(folder, self.scratch) and os.path.isfile(os.path.join(folder, extensions.MARKER))
            return 0

        before = len(self.starts)
        with mock.patch("subprocess.call", record_call):
            with mock.patch.object(sys, "argv", ["blender", "--", "test_probe.py"]):
                self.assertEqual(runner.run_with_private_extensions(), 0)
        self.assertEqual(len(self.starts), before + 1)
        return self.starts[before:]

    # Returns the children of the add-on gate tests: one as it is, one on another extensions folder.
    def gate_children(self):
        self.load_restart()
        gate = self.load("test_addon_gate")
        case = gate.ChildCase("runTest")
        case.setUp()
        try:
            script = case.path("script.py")
            other = case.path("job", "extensions")
            os.makedirs(other)
            return [self.child_of(lambda: case.blender(script)),
                    self.child_of(lambda: case.blender(script, ("--", "--job", "job.json"),
                                                       BLENDER_USER_EXTENSIONS=other))]
        finally:
            case.doCleanups()

    # The restarted suite sees only scratch folders that exist.
    def test_restart_child_folders(self):
        self.check_folders(self.restart_children)

    # The restarted suite is offline and limited to six threads.
    def test_restart_child_options(self):
        self.check_options(self.restart_children)

    # The restarted suite keeps factory settings, its pattern and a prepared folder.
    def test_restart_stays_prepared(self):
        self.deletable = self.scratch
        start = self.restart_children()[0]
        self.assertIn("--factory-startup", start.args[:start.args.index("--python")])
        self.assertEqual(start.args[-2:], ["--", "test_probe.py"])
        self.assertTrue(self.prepared)

    # A restarted suite that is not prepared stops and starts nothing.
    def test_restart_happens_once(self):
        runner, _extensions = self.load_restart()
        os.environ[runner.CHILD_ENV_NAME] = "1"
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as stopped:
                runner.main()
        self.assertEqual((stopped.exception.code, self.starts), (1, []))

    # A Blender user folder is known by its name, also when APPDATA points at scratch.
    def test_user_folder_is_known_without_appdata(self):
        _runner, extensions = self.load_restart()
        for parent in PARENTS:
            for part in ("Blender Foundation", "BLENDER FOUNDATION"):
                with self.subTest(parent=parent, part=part):
                    self.use_parent(parent)
                    own = self.new_path("someone", part, "Blender", "5.2", "extensions")
                    os.makedirs(own)
                    with self.assertRaises(ValueError):
                        extensions.prepare(self.new_path("target"), own)
                    with self.assertRaises(ValueError):
                        extensions.prepare(own)
                    with open(os.path.join(own, extensions.MARKER), "w", encoding="utf-8"):
                        pass
                    os.environ[extensions.ENV_NAME] = own
                    self.assertFalse(extensions.is_private())

    # The children of the add-on gate tests see only scratch folders that exist.
    def test_gate_child_folders(self):
        if not HAS_GATE:
            self.skipTest("this branch has no add-on gate tests")
        self.check_folders(self.gate_children)

    # The children of the add-on gate tests are offline and limited to six threads.
    def test_gate_child_options(self):
        if not HAS_GATE:
            self.skipTest("this branch has no add-on gate tests")
        self.check_options(self.gate_children)


class StartListTests(unittest.TestCase):
    # Every start in the tests folder is one that this file checks. A new one fails until it gets a check here.
    def test_every_start_is_checked(self):
        wanted = dict(SUITE_STARTS)
        if HAS_STORE:
            wanted.update(STORE_STARTS)
        if HAS_EDGE:
            wanted.update((key, what) for key, what in EDGE_STARTS.items()
                          if os.path.isfile(os.path.join(TESTS, key[0])))
        for group in (PLAIN_STARTS, SCRATCH_STARTS, ADDON_STARTS):
            wanted.update((key, what) for key, what in group.items() if os.path.isfile(os.path.join(TESTS, key[0])))
        if HAS_RESTART:
            wanted.update(RESTART_STARTS)
        if os.path.isfile(os.path.join(TESTS, "test_addon_gate.py")):
            wanted.update(GATE_STARTS)
        expected = collections.Counter(key + (what,) for key, what in wanted.items())
        if os.path.isfile(os.path.join(TESTS, ADDON_HANDED_ON[0])):
            expected[ADDON_HANDED_ON] += 1
        for key, count in REPEATED.items():
            if key in expected:
                expected[key] = count
        found = collections.Counter()
        seen = []
        for place, folders, files in os.walk(TESTS):
            folders[:] = [name for name in folders if name != "__pycache__"]
            for name in files:
                if not name.endswith((".py", ".pyw")):
                    continue
                path = os.path.join(place, name)
                short = os.path.relpath(path, TESTS).replace(os.sep, "/")
                with open(path, encoding="utf-8-sig") as handle:
                    source = handle.read()
                for scope, line, what in find_starts(source):
                    found[(short, scope, what)] += 1
                    seen.append("%s:%d %s in %s" % (short, line, what, scope))
                if short != os.path.basename(__file__):
                    self.assertNotIn("--online-mode", source, short)
        self.assertEqual(found, expected, "\n".join(seen))

    # The search sees the usual ways of starting a process.
    def test_search_sees_new_starts(self):
        samples = ("import subprocess\nsubprocess.run(['x'])", "import subprocess\nsubprocess.Popen(['x'])",
                   "import subprocess\ngo = subprocess.call", "import subprocess\nother = subprocess",
                   "import subprocess as sp", "from subprocess import check_output", "import os as machine",
                   "import os\nos.system('x')", "import os\nos.startfile('x')", "import os\nos.spawnv(0, 'x', [])",
                   "from os import popen", "import os\ngetattr(os, 'system')('x')", "import multiprocessing",
                   "import importlib\nimportlib.import_module('subprocess')", "import sys\nsys.modules['subprocess']",
                   "import asyncio\nasyncio.create_subprocess_exec('x')", "bpy.ops.wm.path_open(filepath='x')",
                   "run_blender({}, [])", "pool.submit(run_blender, {}, [])", "self.start(item, env={})",
                   "from helper import run_blender as go", "advapi.CreateProcessAsUserW(token)",
                   "advapi.CreateProcessAsUserA(token)", "advapi.CreateProcessWithTokenW(token)",
                   "from ctypes.windll.advapi32 import CreateProcessAsUserW")
        for source in samples:
            with self.subTest(source=source):
                self.assertNotEqual(find_starts(source), [])
        self.assertEqual(find_starts("import subprocess\nkind = subprocess.PIPE"), [])


class SuiteRegistrationTests(unittest.TestCase):
    # Reads the CMake file as plain text. It understands set, list(APPEND) and set_tests_properties.
    def setUp(self):
        with open(CMAKE_FILE, encoding="utf-8-sig") as handle:
            text = handle.read()
        self.variables = {"PLYNTH_BLENDER": "<blender>", "CMAKE_BINARY_DIR": "<build>",
                          "CMAKE_CURRENT_BINARY_DIR": "<build>/blender", "CMAKE_CURRENT_SOURCE_DIR": "<source>",
                          "CMAKE_COMMAND": "<cmake>"}
        fixed = set(self.variables)
        self.tests = {}
        self.properties = collections.defaultdict(dict)
        self.commands = []
        for name, words in self.read_commands(text):
            args = self.expand(words)
            self.commands.append(name)
            if name == "set" and args and args[0] not in fixed and "CACHE" not in args:
                self.variables[args[0]] = ";".join(args[1:])
            elif name == "list" and len(args) > 1 and args[0] == "APPEND" and args[1] not in fixed:
                old = self.variables.get(args[1], "")
                self.variables[args[1]] = ";".join(([old] if old else []) + args[2:])
            elif name == "add_test" and "NAME" in args and "COMMAND" in args:
                self.tests[args[args.index("NAME") + 1]] = args[args.index("COMMAND") + 1:]
            elif name == "set_tests_properties" and "PROPERTIES" in args:
                split = args.index("PROPERTIES")
                pairs = args[split + 1:]
                for test in args[:split]:
                    self.properties[test].update(zip(pairs[::2], pairs[1::2]))
        self.blender_tests = {name: args for name, args in self.tests.items() if "<blender>" in args}

    # Splits the text into commands and their words, keeping whether each word was quoted.
    def read_commands(self, text):
        found = []
        last = name = None
        words = []
        depth = 0
        for match in CMAKE_WORD.finditer(text):
            quoted, comment, bracket, word = match.groups()
            if comment is not None:
                continue
            if bracket == "(":
                if depth == 0:
                    name, words = (last or "").lower(), []
                depth += 1
            elif bracket == ")":
                depth -= 1
                if depth == 0:
                    found.append((name, words))
            elif depth:
                words.append((quoted.replace("\x5c\n", ""), True) if quoted is not None else (word, False))
            else:
                last = word
        return found

    # Puts in the values of the variables that are known.
    def expand(self, words):
        result = []
        for word, quoted in words:
            value = re.sub(r"\$\{(\w+)\}", lambda match: self.variables.get(match.group(1), match.group(0)), word)
            result += [value] if quoted else [part for part in value.split(";") if part]
        return result

    # Returns the folders that the setup tests of one test make each time it runs.
    def folders_made_for(self, test):
        needed = set(self.properties[test].get("FIXTURES_REQUIRED", "").split(";")) - {""}
        made = []
        for name, args in self.tests.items():
            given = set(self.properties[name].get("FIXTURES_SETUP", "").split(";"))
            if needed & given and args[:3] == ["<cmake>", "-E", "make_directory"]:
                made += args[3:]
        return made

    # The Blender of the suite is offline and limited to six threads.
    def test_suite_options(self):
        self.assertIn("blender_python", self.blender_tests)
        for name, args in self.blender_tests.items():
            with self.subTest(test=name):
                self.assertEqual(option_problems(args), [])

    # The Blender of the suite gets every private folder from CTest, and a setup test makes each one.
    def test_suite_folders(self):
        self.assertIn("blender_python", self.blender_tests)
        for name in self.blender_tests:
            with self.subTest(test=name):
                env = {}
                for entry in self.properties[name].get("ENVIRONMENT", "").split(";"):
                    key, equals, value = entry.partition("=")
                    if equals:
                        env[key] = value
                made = self.folders_made_for(name)
                problems = [key + " is not set" for key in PRIVATE_VARIABLES if key not in env]
                for key, value in env.items():
                    if key not in PRIVATE_VARIABLES and not key.startswith("BLENDER_USER_"):
                        continue
                    if not value.startswith("<build>/") or "$" in value or ".." in value.split("/"):
                        problems.append(key + " is not inside the build folder")
                    elif not any(path == value or path.startswith(value + "/") for path in made):
                        problems.append(key + " names a folder that no setup test makes")
                self.assertEqual(sorted(problems), [])

    # The suite keeps factory settings, and configuring starts no program.
    def test_suite_startup_stays(self):
        self.assertIn("blender_python", self.blender_tests)
        for name, args in self.blender_tests.items():
            with self.subTest(test=name):
                self.assertIn("--factory-startup", args[:args.index("--python")])
        self.assertNotIn("execute_process", self.commands)


if __name__ == "__main__":
    unittest.main()
