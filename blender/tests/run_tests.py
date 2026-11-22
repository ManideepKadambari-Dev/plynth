import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))


# Reads the optional file pattern after the "--" in the command line, for example "test_probe*.py".
def pattern_from(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    return args[0] if args else "test_*.py"


# Starts this same run again in a Blender that uses a private extensions folder, and returns its exit code.
# The user's shared extensions folder is only read, and its library file count must not go down. A rising count is another Blender restoring libraries.
def run_with_private_extensions():
    import bpy
    from plynth import extensions
    folder = tempfile.mkdtemp(prefix="plynth_ext_")
    try:
        extensions.prepare(folder)
        before = extensions.shared_library_count()
        command = [bpy.app.binary_path, "--background", "--factory-startup", "-noaudio", "--python-exit-code", "3",
                   "--python", os.path.abspath(__file__)] + (sys.argv[sys.argv.index("--"):] if "--" in sys.argv else [])
        code = subprocess.call(command, env=extensions.environment(folder), cwd=os.getcwd())
        after = extensions.shared_library_count()
        print("Shared extension library files: %d before, %d after." % (before, after))
        if after < before:
            print("The shared extension libraries lost files.")
            return code or 1
        return code
    finally:
        shutil.rmtree(folder, ignore_errors=True)


# Runs the tests of this folder inside Blender and exits 1 when any fails.
def main():
    from plynth import extensions
    if not extensions.is_private() and os.environ.get("PLYNTH_TESTS_SHARED") != "1":
        sys.stdout.flush()
        sys.exit(run_with_private_extensions())
    suite = unittest.defaultTestLoader.discover(HERE, pattern=pattern_from(sys.argv), top_level_dir=HERE)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.stdout.flush()
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
