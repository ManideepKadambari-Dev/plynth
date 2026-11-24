"""S-R 5 check, run in the one start without factory startup, after Blender loaded its own scripts, the guard
and the add-ons of the scratch user. Prints what the test compares.
    blender -b -t 6 --offline-mode --disable-autoexec --python sr5_check.py
"""
import sys

import bpy

state = getattr(sys, "_plynth_fetch_guard", None)
print("SR5 builtin: %s" % ("yes" if hasattr(bpy.types, "VIEW3D_HT_header") else "no"), flush=True)
print("SR5 guard_loaded: %s" % ("yes" if "plynth_fetch_guard" in sys.modules else "no"), flush=True)
print("SR5 guard_root: %s" % ("set" if state is not None and state.root else "none"), flush=True)
print("SR5 guard_reason: %s" % ((state.reason or "ok") if state is not None else "absent"), flush=True)
