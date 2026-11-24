import bpy


class PLYNTH_OT_test_addon(bpy.types.Operator):
    """Does nothing. It exists so the tests can see the add-on is on."""

    bl_idname = "plynth.test_addon"
    bl_label = "Plynth test add-on"

    # Finishes at once without changing anything.
    def execute(self, context):
        return {"FINISHED"}


# Adds the operator when the add-on is switched on.
def register():
    bpy.utils.register_class(PLYNTH_OT_test_addon)


# Removes the operator when the add-on is switched off.
def unregister():
    bpy.utils.unregister_class(PLYNTH_OT_test_addon)
