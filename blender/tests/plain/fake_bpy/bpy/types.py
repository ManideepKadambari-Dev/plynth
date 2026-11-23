"""Base classes of the stand-in bpy."""


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

    def report(self, kinds, text):
        pass


class Panel:
    pass


class Menu:
    pass
