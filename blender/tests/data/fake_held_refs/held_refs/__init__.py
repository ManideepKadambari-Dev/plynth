"""A second fake add-on for the fetch tests that run without Blender. When it is imported it takes
addon_utils.enable, addon_utils.disable and the logout of the fake BlenderKit add-on by name, as an add-on may
hold functions from before the seal. When it registers it wraps the search of the fake BlenderKit add-on, so the
operator calls the held functions after the seal, just before its lookup. Each call is noted as held_call_ran
or held_call_refused in the events of the fake BlenderKit add-on.
"""
from addon_utils import disable, enable
from blenderkit import global_vars, search
from blenderkit.bkit_oauth import logout

bl_info = {"name": "Held references", "author": "Plynth tests", "version": (1, 0, 0), "blender": (4, 2, 0),
           "category": "Development"}
_original = []


# Calls each held function and notes whether it went through.
def call_held():
    calls = (("enable", lambda: enable("other_addon", default_set=True)),
             ("disable", lambda: disable("blenderkit", default_set=True)),
             ("logout", lambda: logout()))
    for name, call in calls:
        try:
            call()
            global_vars.note("held_call_ran", name=name)
        except Exception:
            global_vars.note("held_call_refused", name=name)


# The search of the fake BlenderKit add-on, after the held calls.
def get_search_simple(*args, **kwargs):
    call_held()
    return _original[0](*args, **kwargs)


# Puts the wrapper on the search.
def register():
    _original.append(search.get_search_simple)
    search.get_search_simple = get_search_simple


# Takes the wrapper off again.
def unregister():
    if _original:
        search.get_search_simple = _original.pop()
