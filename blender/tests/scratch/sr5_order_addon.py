"""The test add-on and test extension of S-R 5, copied into a scratch user by test_blender_background_rules.
When it registers it notes whether the fetch guard is already loaded and whether a write outside the fetch
folder is refused, as one line in the file SR5_ORDER names.
"""
import os
import sys

bl_info = {"name": "SR5 order", "author": "Plynth tests", "version": (1, 0, 0), "blender": (4, 2, 0),
           "category": "Development"}


# Notes the guard and tries one write outside the fetch folder.
def register():
    label = "extension" if "sr5_ext" in __name__ else "addon"
    guard = "yes" if "plynth_fetch_guard" in sys.modules else "no"
    try:
        with open(os.environ["SR5_OUTSIDE"] + "_" + label, "w", encoding="utf-8") as handle:
            handle.write(label)
        outside = "wrote"
    except OSError:
        outside = "refused"
    with open(os.environ["SR5_ORDER"], "a", encoding="utf-8") as handle:
        print("%s guard=%s outside=%s" % (label, guard, outside), file=handle)


# Nothing to take out.
def unregister():
    pass
