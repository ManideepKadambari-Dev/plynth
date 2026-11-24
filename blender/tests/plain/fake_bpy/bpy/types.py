"""Base classes of the stand-in bpy. As in Blender, a registered operator class also appears here under its
type name, for example PREFERENCES_OT_addon_enable, and leaves again when it is unregistered.
"""


class Bag:
    # Any attributes given.
    def __init__(self, **fields):
        self.__dict__.update(fields)


class ID:
    # A data block with a name.
    def __init__(self, name):
        self.name = name
        self.data = None


class AddonPreferences:
    bl_idname = ""


class PropertyGroup:
    pass


class Operator:
    bl_idname = ""
    is_registered = False

    def report(self, kinds, text):
        pass


class Panel:
    pass


class Menu:
    pass
