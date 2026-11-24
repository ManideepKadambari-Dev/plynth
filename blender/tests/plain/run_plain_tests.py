"""Runs the fetch tests that need no Blender, in plain Python: python -I run_plain_tests.py [pattern].
The build sets PLYNTH_FAKE_STORE_HELPER to its fake_store_helper program. Exits 1 when any test fails.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


# Runs the tests of this folder.
def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else "test_*.py"
    suite = unittest.defaultTestLoader.discover(HERE, pattern=pattern, top_level_dir=HERE)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
