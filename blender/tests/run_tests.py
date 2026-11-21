import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))


# Reads the optional file pattern after the "--" in the command line, for example "test_probe*.py".
def pattern_from(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    return args[0] if args else "test_*.py"


# Runs the tests of this folder inside Blender and exits 1 when any fails.
def main():
    suite = unittest.defaultTestLoader.discover(HERE, pattern=pattern_from(sys.argv), top_level_dir=HERE)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.stdout.flush()
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
