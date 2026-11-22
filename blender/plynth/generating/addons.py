import addon_utils


# Returns the module name of the add-on whose last dotted part, in lower case, is short_name, or None.
def find_module(short_name):
    wanted = short_name.lower()
    for module in addon_utils.modules(refresh=False):
        name = module.__name__
        if name.split(".")[-1].lower() == wanted:
            return name
    return None


# Turns the add-on on in this Blender session without saving it to the preferences. Returns true when it is on.
def enable(short_name):
    name = find_module(short_name)
    if name is None:
        return False
    try:
        if addon_utils.check(name)[1]:
            return True
        return addon_utils.enable(name, default_set=False, persistent=False) is not None
    except Exception:
        return False
