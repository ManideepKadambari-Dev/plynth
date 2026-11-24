"""The test add-on of S-R 3b, copied into a scratch user by test_blender_background_rules. It writes the file
SR3B_PROBE_MARKER names when it registers, so a test sees whether an enable went through.
"""
import os

bl_info = {"name": "SR3B probe", "author": "Plynth tests", "version": (1, 0, 0), "blender": (4, 2, 0),
           "category": "Development"}


# Writes the marker.
def register():
    with open(os.environ["SR3B_PROBE_MARKER"], "w", encoding="utf-8") as handle:
        handle.write("registered")


# Nothing to take out.
def unregister():
    pass
