import fnmatch
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CHILD_ENV_NAME = "PLYNTH_TESTS_CHILD"
ADDONS_OPTION = "--addons"
ADDON_MODULES = ("test_bridges.py", "test_addon_generators.py")
THREADS = "6"
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))


# Reads the options after the "--" in the command line: an optional file pattern, for example "test_probe*.py",
# and --addons, which runs only the add-on tests and fails when any of them is skipped.
def options_from(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    rest = [arg for arg in args if arg != ADDONS_OPTION]
    return (rest[0] if rest else "test_*.py"), ADDONS_OPTION in args


# Starts this same run again in a Blender that has its own user folders and a prepared extensions folder, and
# returns its exit code. Add-ons are copied only from the folder PLYNTH_TEST_ADDONS names, whose library file
# count must not go down. With --addons that folder must hold every known add-on.
def run_with_private_extensions():
    import bpy
    from plynth import extensions
    _pattern, addons = options_from(sys.argv)
    source = os.environ.get(extensions.SOURCE_ENV_NAME) or None
    if addons and not source:
        print("The add-on tests need %s to name a folder of add-ons, so they were not run."
              % extensions.SOURCE_ENV_NAME)
        return 1
    root = tempfile.mkdtemp(prefix="plynth_ext_")
    try:
        env = extensions.environment(root)
        env[CHILD_ENV_NAME] = "1"
        try:
            copied = extensions.prepare(os.path.join(root, "extensions"), source)
        except ValueError as error:
            print("%s The tests were not run." % error)
            return 1
        missing = extensions.missing_addons(copied) if addons else []
        if missing:
            print("The add-on folder lacks %s, so the add-on tests were not run." % ", ".join(missing))
            return 1
        before = extensions.shared_library_count(source) if source else 0
        command = [bpy.app.binary_path, "--background", "--factory-startup", "-noaudio", "-t", THREADS,
                   "--offline-mode", "--python-exit-code", "3", "--python", os.path.abspath(__file__)]
        command += sys.argv[sys.argv.index("--"):] if "--" in sys.argv else []
        code = subprocess.call(command, env=env, cwd=os.getcwd())
        if not source:
            return code
        after = extensions.shared_library_count(source)
        print("Extension library files in the source: %d before, %d after." % (before, after))
        if after < before:
            print("The source's extension libraries lost files.")
            return code or 1
        return code
    finally:
        shutil.rmtree(root, ignore_errors=True)


# Returns the tests to run: the files that match the pattern, or with --addons only the add-on test files.
def load_suite(pattern, addons):
    loader = unittest.defaultTestLoader
    if not addons:
        return loader.discover(HERE, pattern=pattern, top_level_dir=HERE)
    suite = unittest.TestSuite()
    for name in ADDON_MODULES:
        if fnmatch.fnmatch(name, pattern):
            suite.addTests(loader.discover(HERE, pattern=name, top_level_dir=HERE))
    return suite


# Makes TEMP the one temporary folder of the run. Python reads TMPDIR before TEMP, and a Git Bash shell sets
# TMPDIR, so without this a run from there would put temporary files elsewhere than a ctest run, and every
# child a test starts with its own TEMP would still follow the TMPDIR it inherited.
def use_temp_only():
    if os.environ.get("TEMP"):
        os.environ.pop("TMPDIR", None)
        tempfile.tempdir = None


# Runs the tests of this folder inside Blender and exits 1 when any fails. With --addons a skip or an empty run
# fails too.
def main():
    from plynth import extensions
    use_temp_only()
    if not extensions.is_private():
        if os.environ.get(CHILD_ENV_NAME):
            print("The prepared extensions folder is missing, so the tests were not run.")
            sys.exit(1)
        sys.stdout.flush()
        sys.exit(run_with_private_extensions())
    pattern, addons = options_from(sys.argv)
    result = unittest.TextTestRunner(verbosity=2).run(load_suite(pattern, addons))
    passed = result.wasSuccessful()
    if addons:
        skipped = len(result.skipped)
        print("Add-on tests: %d run, %d skipped" % (result.testsRun - skipped, skipped))
        passed = passed and skipped == 0 and result.testsRun > 0
    sys.stdout.flush()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
