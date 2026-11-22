import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CHILD_ENV_NAME = "PLYNTH_TESTS_CHILD"
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))


# Reads the optional file pattern after the "--" in the command line, for example "test_probe*.py".
def pattern_from(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    return args[0] if args else "test_*.py"


# Starts this same run again in a Blender that has its own user folders and a prepared extensions folder, and
# returns its exit code. Add-ons are copied only from the folder PLYNTH_TEST_ADDONS names, whose library file
# count must not go down.
def run_with_private_extensions():
    import bpy
    from plynth import extensions
    source = os.environ.get(extensions.SOURCE_ENV_NAME)
    root = tempfile.mkdtemp(prefix="plynth_ext_")
    try:
        env = extensions.environment(root)
        env[CHILD_ENV_NAME] = "1"
        extensions.prepare(os.path.join(root, "extensions"), source)
        before = extensions.shared_library_count(source) if source else 0
        command = [bpy.app.binary_path, "--background", "--factory-startup", "-noaudio", "--python-exit-code", "3",
                   "--python", os.path.abspath(__file__)] + (sys.argv[sys.argv.index("--"):] if "--" in sys.argv else [])
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


# Runs the tests of this folder inside Blender and exits 1 when any fails.
def main():
    from plynth import extensions
    if not extensions.is_private():
        if os.environ.get(CHILD_ENV_NAME):
            print("The prepared extensions folder is missing, so the tests were not run.")
            sys.exit(1)
        sys.stdout.flush()
        sys.exit(run_with_private_extensions())
    suite = unittest.defaultTestLoader.discover(HERE, pattern=pattern_from(sys.argv), top_level_dir=HERE)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.stdout.flush()
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
