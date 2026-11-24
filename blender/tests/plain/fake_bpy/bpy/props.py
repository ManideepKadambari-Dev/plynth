"""Properties of the stand-in bpy. A property keeps a default and an update function, which runs on every
set, also of an equal value, as Blender runs it (S-R 1 checks that in real Blender).
"""


class _Property:
    def __init__(self, default=None, update=None, items=None, **_others):
        self.default = default
        self.update = update
        self.items = items


# Each property kind takes the same keywords.
def StringProperty(**options):
    return _Property(**{"default": "", **options})


def BoolProperty(**options):
    return _Property(**{"default": False, **options})


def IntProperty(**options):
    return _Property(**{"default": 0, **options})


def FloatProperty(**options):
    return _Property(**{"default": 0.0, **options})


def EnumProperty(**options):
    return _Property(**options)


def PointerProperty(**options):
    return _Property(**options)


def CollectionProperty(**options):
    return _Property(**options)
