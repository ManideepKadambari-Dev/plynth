"""S-R 3b, run by test_blender_background_rules in a scratch background Blender with factory startup: whether
each way the fetch closes an operator holds in Blender 5.2, and whether the seal of the fetch guard
(blender/guard/startup/plynth_fetch_guard.py) proves itself. Prints one "SR3B <name>: <answer>" line for each
answer. Nothing here reaches another computer.
    blender -b -t 6 --offline-mode --factory-startup --python sr3b_operator_closes.py --
        <work> <fake add-ons> <guard file>
PLYNTH_FETCH_PRIVATE names the work folder, where every marker is written, so the guard allows them.
SR3B_PROBE_MARKER is written by the test add-on sr3b_probe_addon when it registers. FAKE_BK_EVENTS names a file
in the work folder, so the fake add-on writes its marker there.
"""
import importlib.util
import json
import os
import sys
import types

import addon_utils
import bpy

work, fake_addons, guard_file = sys.argv[sys.argv.index("--") + 1:][:3]
sys.dont_write_bytecode = True
ITEM = "0c1d4a2e-1111-4111-8111-aaaaaaaaaaaa"
PROBE_ADDON = "sr3b_probe_addon"
CHOICE = "scene.blenderkit_addon_choice"
loaded = []


# Prints one answer.
def say(name, answer):
    print("SR3B %s: %s" % (name, answer), flush=True)


# Whether a marker file is there.
def marked(path):
    return "yes" if os.path.exists(path) else "no"


# Removes a marker file when it is there.
def clear(path):
    if os.path.exists(path):
        os.remove(path)


# Calls something that should be refused. Returns "ran" when it went through, or the type name of its error.
def outcome(call):
    try:
        result = call()
    except Exception as error:
        return type(error).__name__
    return "ran" if not isinstance(result, set) or "FINISHED" in result else "returned_" + "_".join(sorted(result))


# The registered Python class of an operator, found among the subclasses of bpy.types.Operator, or None.
def python_class(idname):
    seen = set()
    stack = list(bpy.types.Operator.__subclasses__())
    while stack:
        cls = stack.pop()
        if cls in seen:
            continue
        seen.add(cls)
        stack.extend(cls.__subclasses__())
        if getattr(cls, "bl_idname", "") == idname and getattr(cls, "is_registered", False):
            return cls
    return None


# The registered idnames that start with a prefix, found among the subclasses of bpy.types.Operator.
def python_idnames(prefix):
    found = set()
    stack = list(bpy.types.Operator.__subclasses__())
    while stack:
        cls = stack.pop()
        stack.extend(cls.__subclasses__())
        idname = getattr(cls, "bl_idname", "")
        if idname.startswith(prefix) and getattr(cls, "is_registered", False):
            found.add(idname)
    return sorted(found)


# Loads a fresh copy of the guard under its own module name and registers it. Each copy has its own state, so
# one Blender can check several seals. changes are module values set before register.
def load_guard(name, **changes):
    if hasattr(sys, "_plynth_fetch_guard"):
        delattr(sys, "_plynth_fetch_guard")
    spec = importlib.util.spec_from_file_location(name, guard_file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for key, value in changes.items():
        setattr(module, key, value)
    module.register()
    loaded.append(module)
    return module


# The reason of a guard refusal, "passes" when the call went through, or the type name of another error.
def guard_outcome(call):
    try:
        call()
    except Exception as error:
        return str(getattr(error, "reason", "") or type(error).__name__)
    return "passes"


class SR3BMarker(bpy.types.Operator):
    bl_idname = "plynth.sr3b_marker"
    bl_label = "SR3B marker"

    # Writes the marker of case 1.
    def execute(self, context):
        open(os.path.join(work, "case1.marker"), "w").close()
        return {"FINISHED"}


class SR3BLookup(bpy.types.Operator):
    bl_idname = "plynth.sr3b_lookup"
    bl_label = "SR3B lookup"

    asset_base_id: bpy.props.StringProperty(default="")  # type: ignore[valid-type]

    # Looks the item up through its module at call time, as the add-on does, and branches on the kind.
    def execute(self, context):
        import sr3b_search
        results = sr3b_search.get_search_simple({"asset_base_id": self.asset_base_id}, page_size=1, max_results=1)
        if results and results[0].get("assetType") == "addon":
            open(os.path.join(work, "case3.marker"), "w").close()
        return {"FINISHED"}


# What Blender itself shows: whether dir(bpy.types), hasattr and getattr see a registered Python operator.
def case0():
    say("0_dir_lists_python_operator", "yes" if "PREFERENCES_OT_addon_enable" in dir(bpy.types) else "no")
    say("0_hasattr_python_operator", "yes" if hasattr(bpy.types, "PREFERENCES_OT_addon_enable") else "no")
    found = getattr(bpy.types, "PREFERENCES_OT_addon_enable", None)
    same = found is not None and found is python_class("preferences.addon_enable")
    say("0_getattr_is_the_class", "yes" if same else "no")
    listed = sorted(name for name in dir(bpy.types) if name.startswith("EXTENSIONS_OT_package_"))
    say("0_extension_types_in_dir", str(len(listed)))
    say("0_extension_classes_found", str(len(python_idnames("extensions.package_"))))


# Case 1: a test operator writes a marker; unregistered, neither bpy.ops nor a callable taken before runs it.
def case1():
    marker = os.path.join(work, "case1.marker")
    bpy.utils.register_class(SR3BMarker)
    early = bpy.ops.plynth.sr3b_marker
    say("1_type_before", "yes" if hasattr(bpy.types, "PLYNTH_OT_sr3b_marker") else "no")
    early()
    say("1_control", "marker" if os.path.exists(marker) else "none")
    clear(marker)
    bpy.utils.unregister_class(SR3BMarker)
    say("1_type_gone", "yes" if not hasattr(bpy.types, "PLYNTH_OT_sr3b_marker") else "no")
    say("1_through_ops", outcome(lambda: bpy.ops.plynth.sr3b_marker()))
    say("1_through_early", outcome(early))
    say("1_marker_after", marked(marker))


# Case 3: an operator that calls the search through its module and branches on the kind. With the wrapper in
# that module it raises before the branch.
def case3():
    marker = os.path.join(work, "case3.marker")
    search = types.ModuleType("sr3b_search")

    # Answers an add-on for any id, as a hostile or mistaken server could.
    def get_search_simple(parameters, filepath=None, page_size=100, max_results=100000000, api_key=""):
        return [{"assetBaseId": parameters.get("asset_base_id", ""), "assetType": "addon"}]

    search.get_search_simple = get_search_simple
    sys.modules["sr3b_search"] = search
    bpy.utils.register_class(SR3BLookup)
    early = bpy.ops.plynth.sr3b_lookup
    early(asset_base_id=ITEM)
    say("3_control", "marker" if os.path.exists(marker) else "none")
    clear(marker)
    real = search.get_search_simple

    # Calls the search and raises unless the first result is the asked model.
    def checked(*args, **kwargs):
        results = real(*args, **kwargs)
        first = results[0] if results else {}
        if first.get("assetBaseId") != ITEM or first.get("assetType") != "model":
            raise PermissionError("the search answered another item")
        return results

    search.get_search_simple = checked
    say("3_through_ops", outcome(lambda: bpy.ops.plynth.sr3b_lookup(asset_base_id=ITEM)))
    say("3_through_early", outcome(lambda: early(asset_base_id=ITEM)))
    say("3_marker_after", marked(marker))
    bpy.utils.unregister_class(SR3BLookup)


# Case 5: the tested list of the guard, and a Blender outside it refused by require and seal. The version is
# patched only in a copy of the guard.
def case5():
    if not os.path.isfile(guard_file):
        say("5_list", "missing")
        return False
    other_list = os.path.join(work, "tested_other.json")
    with open(other_list, "w", encoding="utf-8") as handle:
        json.dump({"blender": ["5.3"]}, handle)
    tested = load_guard("sr3b_guard_tested")
    say("5_list", ",".join(tested._read_versions()) or "empty")
    say("5_running", guard_outcome(lambda: tested.require(work)))
    listed = load_guard("sr3b_guard_list", VERSIONS_FILE=other_list)
    say("5_other_list_require", guard_outcome(lambda: listed.require(work)))
    say("5_other_list_seal", guard_outcome(lambda: listed.seal()))
    patched = load_guard("sr3b_guard_version", _blender_version=lambda: "5.3")
    say("5_patched_require", guard_outcome(lambda: patched.require(work)))
    say("5_patched_seal", guard_outcome(lambda: patched.seal()))
    say("5_addon_enable_kept", "yes" if python_class("preferences.addon_enable") is not None else "no")
    return True


# Cases 2 and 4: callables taken before, the controls, a seal with one class left registered, then the seal
# that passes, and every way tried again after it.
def case2_and_4():
    probe_marker = os.environ["SR3B_PROBE_MARKER"]
    choice_marker = os.path.join(work, "addon_choice.marker")
    sys.path.insert(0, fake_addons)
    from blenderkit import download as fake_download
    bpy.utils.register_class(fake_download.BlenderkitAddonChoiceOperator)
    guard = loaded[0]
    named = [guard.operator_type_name(idname) for idname in tuple(guard.SEALED_OPERATORS) + (CHOICE,)]
    named += sorted(name for name in dir(bpy.types) if name.startswith(guard.EXTENSION_PREFIX) and name not in named)
    recorded = {name: getattr(bpy.types, name, None) for name in named}
    missing = ",".join(name for name, cls in recorded.items() if cls is None)
    say("4_registered_before", "missing_" + missing if missing else "yes")
    early_enable = bpy.ops.preferences.addon_enable
    early_choice = bpy.ops.scene.blenderkit_addon_choice
    early_install = bpy.ops.extensions.package_install_files
    outcome(lambda: early_enable(module=PROBE_ADDON))
    say("2_enable_control", "marker" if os.path.exists(probe_marker) else "none")
    addon_utils.disable(PROBE_ADDON, default_set=True)
    clear(probe_marker)
    outcome(lambda: early_choice(asset_base_id=ITEM))
    say("2_choice_control", "marker" if os.path.exists(choice_marker) else "none")
    clear(choice_marker)

    left = load_guard("sr3b_guard_left_one")
    real_unregister = bpy.utils.unregister_class

    # Unregisters every class but preferences.addon_remove, as a Blender where one class cannot be taken out.
    def keep_one(cls):
        if "addon_remove" in getattr(cls, "bl_idname", "") + getattr(cls, "__name__", ""):
            return
        real_unregister(cls)

    bpy.utils.unregister_class = keep_one
    try:
        say("4_left_one", guard_outcome(lambda: left.seal(operators=(CHOICE,))))
    finally:
        bpy.utils.unregister_class = real_unregister
    say("4_left_one_still_registered", "yes" if hasattr(bpy.types, "PREFERENCES_OT_addon_remove") else "no")
    failed = []
    for name, cls in recorded.items():
        if cls is not None and not hasattr(bpy.types, name):
            try:
                bpy.utils.register_class(cls)
            except Exception:
                failed.append(name)
    back = not failed and all(hasattr(bpy.types, name) for name in named)
    say("4_restored", "yes" if back else "no_" + ",".join(failed))

    sealing = load_guard("sr3b_guard_seal")
    say("4_seal", guard_outcome(lambda: sealing.seal(operators=(CHOICE,))))
    say("4_types_left", ",".join(name for name in named if hasattr(bpy.types, name)) or "none")
    left_classes = python_idnames("extensions.package_") + [idname for idname in sealing.SEALED_OPERATORS
                                                             if python_class(idname) is not None]
    say("4_classes_left", ",".join(left_classes) or "none")
    say("4_choice_class_left", "yes" if python_class(CHOICE) is not None else "no")
    say("4_enable_replaced", "yes" if sys.modules["addon_utils"].enable is sealing._refused else "no")
    say("4_seal_again", guard_outcome(lambda: sealing.seal()))

    say("2_enable_through_ops", outcome(lambda: bpy.ops.preferences.addon_enable(module=PROBE_ADDON)))
    say("2_enable_through_early", outcome(lambda: early_enable(module=PROBE_ADDON)))
    say("2_addon_utils_enable", outcome(lambda: addon_utils.enable(PROBE_ADDON, default_set=True)))
    say("2_addon_utils_disable", outcome(lambda: addon_utils.disable(PROBE_ADDON, default_set=True)))
    say("2_probe_marker_after", marked(probe_marker))
    say("2_choice_through_ops", outcome(lambda: bpy.ops.scene.blenderkit_addon_choice(asset_base_id=ITEM)))
    say("2_choice_through_early", outcome(lambda: early_choice(asset_base_id=ITEM)))
    say("2_choice_marker_after", marked(choice_marker))
    say("2_install_through_early", outcome(lambda: early_install(filepath=os.path.join(work, "none.zip"))))


case0()
case1()
case3()
if case5():
    case2_and_4()
say("done", "yes")
