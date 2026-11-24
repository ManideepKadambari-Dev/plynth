"""S-R 1 to 4, 8 and 9, run by test_blender_background_rules in a scratch background Blender. Prints one
"SR<n> <name>: <answer>" line for each behavior. Nothing here reaches another computer.
    blender -b -t 6 --offline-mode --factory-startup --python sr_rules.py -- <scratch folder>
"""
import os
import sys
import tempfile

import bpy

folder = sys.argv[sys.argv.index("--") + 1]
seen = []


# Prints one answer.
def say(number, name, answer):
    print("SR%d %s: %s" % (number, name, answer), flush=True)


# S-R 1: does an update function run when a property is set to the value it already has?
def equal_value_update():
    def update(self, context):
        seen.append("update")
    bpy.types.Scene.sr1_flag = bpy.props.BoolProperty(default=False, update=update)
    bpy.context.scene.sr1_flag = False
    say(1, "update_on_equal_value", "yes" if seen else "no")


class SR2Choices(bpy.types.Operator):
    bl_idname = "plynth.sr2_choices"
    bl_label = "SR2"

    # The choices follow max_resolution, as the add-on's do.
    def resolutions(self, context):
        top = self.max_resolution or 1024
        return [(str(size), str(size), "") for size in (512, 1024, 2048, 4096) if size <= top]

    max_resolution: bpy.props.IntProperty(default=0)  # type: ignore[valid-type]
    resolution: bpy.props.EnumProperty(items=resolutions)  # type: ignore[valid-type]

    # Prints the value the operator got.
    def execute(self, context):
        say(2, "resolution_seen", self.resolution)
        return {"FINISHED"}


class SR3Dialog(bpy.types.Operator):
    bl_idname = "plynth.sr3_dialog"
    bl_label = "SR3"

    # Asks for a dialog first, as the add-on's prompts do.
    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    # Writes the marker.
    def execute(self, context):
        open(os.path.join(folder, "sr3_marker.txt"), "w").close()
        return {"FINISHED"}


# S-R 2: both orders of the two properties in one call.
def choices_order():
    for options in ({"max_resolution": 4096, "resolution": "4096"}, {"resolution": "4096", "max_resolution": 4096}):
        try:
            bpy.ops.plynth.sr2_choices(**options)
        except Exception as error:
            say(2, "resolution_seen", type(error).__name__)


# S-R 3: a dialog operator in background. The old hook on bpy.ops._op_call is gone in Blender 5.2, so only
# the answer is printed; S-R 3b checks the ways that replace it.
def dialog_in_background():
    marker = os.path.join(folder, "sr3_marker.txt")
    try:
        bpy.ops.plynth.sr3_dialog("INVOKE_DEFAULT")
    except Exception as error:
        say(3, "dialog_error", type(error).__name__)
    say(3, "execute_ran_directly", "yes" if os.path.exists(marker) else "no")
    say(3, "op_call_exists", "yes" if "_op_call" in vars(bpy.ops) else "no")


# S-R 4: undo_push after an append in background.
def append_then_undo():
    source = os.path.join(folder, "sr4.blend")
    bpy.data.objects.new("SR4", None)
    bpy.data.libraries.write(source, {bpy.data.objects["SR4"]})
    try:
        bpy.ops.wm.append(directory=source + "/Object/", filename="SR4")
        say(4, "append", "ok")
    except Exception as error:
        say(4, "append", type(error).__name__)
    try:
        bpy.ops.ed.undo_push(message="sr4")
        say(4, "undo_push", "ok")
    except Exception as error:
        say(4, "undo_push", type(error).__name__)
    say(4, "source_kept", "yes" if os.path.exists(source) else "no")


bpy.utils.register_class(SR2Choices)
bpy.utils.register_class(SR3Dialog)
equal_value_update()
choices_order()
dialog_in_background()
append_then_undo()
say(8, "window", "none" if bpy.context.window is None else "present")
say(9, "gettempdir_follows_temp",
    "yes" if os.path.normcase(tempfile.gettempdir()) == os.path.normcase(os.environ.get("TEMP", "")) else "no")
