"""Loaded by Blender from BLENDER_SYSTEM_SCRIPTS before any add-on when Plynth fetches from a store. A Python
audit hook, which cannot be taken out again, refuses every file write outside the fetch folder named by
PLYNTH_FETCH_PRIVATE, every registry write, every program from outside that folder and ending other programs.
Writes in C code are not seen here; low integrity covers them. A task calls require, seal and
require_low_integrity before it uses a store's add-on.
"""
import errno
import json
import os
import re
import sys
import threading

PRIVATE_VARIABLE = "PLYNTH_FETCH_PRIVATE"
VERSIONS_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tested_versions.json")
UNSAFE_SENTENCE = "The download could not be made safe in this Blender, so it was not started."
REFUSED_TEXT = "Refused by the fetch guard"
SEALED_OPERATORS = ("preferences.addon_enable", "preferences.addon_disable", "preferences.addon_install",
                    "preferences.addon_remove", "extensions.package_install_files")
EXTENSION_PREFIX = "EXTENSIONS_OT_package_"
LOW_INTEGRITY = 0x1000

# Events whose listed arguments are paths that get written.
WRITTEN_PATHS = {
    "os.mkdir": (0,), "os.rmdir": (0,), "os.remove": (0,), "os.rename": (0, 1), "os.link": (0, 1),
    "os.chmod": (0,), "os.chown": (0,), "os.chflags": (0,), "os.lchflags": (0,), "os.utime": (0,),
    "os.setxattr": (0,), "os.removexattr": (0,),
    "shutil.copyfile": (1,), "shutil.copymode": (1,), "shutil.copystat": (1,), "shutil.copytree": (1,),
    "shutil.rmtree": (0,), "shutil.move": (0, 1), "shutil.chown": (0,), "shutil.make_archive": (0,),
    "_winapi.CreateJunction": (0, 1), "_winapi.CopyFile2": (1,),
}
REFUSED_EVENTS = frozenset((
    "os.kill", "os.killpg", "os.system", "os.startfile", "os.fork", "os.forkpty", "pty.spawn", "webbrowser.open",
    "winreg.CreateKey", "winreg.DeleteKey", "winreg.DeleteValue", "winreg.SetValue", "winreg.LoadKey",
    "winreg.SaveKey", "winreg.ConnectRegistry", "winreg.DisableReflectionKey", "winreg.EnableReflectionKey"))
PROGRAM_EVENTS = {"subprocess.Popen": (0, 1), "_winapi.CreateProcess": (0, 1), "os.exec": (0, None),
                  "os.spawn": (1, None), "os.posix_spawn": (0, None)}
OTHER_EVENTS = frozenset(("open", "os.symlink", "os.truncate", "shutil.unpack_archive", "sqlite3.connect",
                          "_winapi.CreateFile", "winreg.OpenKey", "_winapi.OpenProcess"))
WATCHED = frozenset(WRITTEN_PATHS) | REFUSED_EVENTS | frozenset(PROGRAM_EVENTS) | OTHER_EVENTS

KEY_WRITE_ACCESS = 0x2 | 0x4 | 0x20 | 0x10000 | 0x40000 | 0x80000 | 0x2000000 | 0x10000000 | 0x40000000
FILE_WRITE_ACCESS = 0x2 | 0x4 | 0x10 | 0x100 | 0x10000 | 0x40000 | 0x80000 | 0x2000000 | 0x10000000 | 0x40000000
FILE_WRITE_DISPOSITIONS = (1, 2, 4, 5)
PROCESS_READ_ACCESS = 0x10 | 0x400 | 0x1000 | 0x100000

_state = None


class GuardError(Exception):
    # A refusal by the guard: reason is a short code, the text is a fixed sentence.
    def __init__(self, reason, sentence=UNSAFE_SENTENCE):
        super().__init__(sentence)
        self.reason = reason
        self.sentence = sentence


class _State:
    # What the guard found when it loaded. root is None when every write is refused.
    def __init__(self):
        self.root = None
        self.versions = ()
        self.reason = "guard_not_ready"
        self.seal_tried = False
        self.sealed = False


# The sentence for a Blender that is not in the tested list.
def version_sentence(versions):
    if not versions:
        return "Downloads from a store work only with a Blender version Plynth is tested with."
    return "Downloads from a store work only with Blender %s." % " or ".join(versions)


# The Blender versions the fetch is tested with, as "5.2", read from the list beside the startup folder.
def _read_versions():
    try:
        with open(VERSIONS_FILE, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return ()
    found = data.get("blender") if isinstance(data, dict) else None
    if not isinstance(found, list) or not all(isinstance(v, str) and re.fullmatch(r"\d+\.\d+", v) for v in found):
        return ()
    return tuple(found)


# This Blender's version as "5.2", or "" when its number and its shown text do not agree.
def _blender_version():
    import bpy
    numbers = tuple(bpy.app.version)[:2]
    shown = re.match(r"(\d+)\.(\d+)", str(bpy.app.version_string))
    if len(numbers) != 2 or shown is None:
        return ""
    text = "%d.%d" % (numbers[0], numbers[1])
    return text if text == "%d.%d" % (int(shown.group(1)), int(shown.group(2))) else ""


# Whether this Blender is in the tested list.
def _version_tested(versions):
    try:
        version = _blender_version()
    except Exception:
        return False
    return bool(version) and version in versions


# The fetch folder from the environment, in the form the hook compares, or None when it is not usable.
def _fetch_root():
    value = os.environ.get(PRIVATE_VARIABLE, "")
    try:
        if not value or not os.path.isabs(value) or not os.path.isdir(value):
            return None
        if getattr(os.lstat(value), "st_file_attributes", 0) & 0x400:
            return None
        root = os.path.normcase(os.path.realpath(value)).rstrip("\\/")
    except (OSError, ValueError):
        return None
    if not root or os.path.dirname(root) == root:
        return None
    return root


# The open flags that write.
def _write_flags():
    flags = 0
    for name in ("O_WRONLY", "O_RDWR", "O_APPEND", "O_CREAT", "O_TRUNC", "O_TEMPORARY"):
        flags |= getattr(os, name, 0)
    return flags


# The program a command line starts: its first word, quoted or not.
def _first_word(command):
    command = command.lstrip()
    if command.startswith('"'):
        end = command.find('"', 1)
        return command[1:end] if end > 0 else command[1:]
    parts = command.split(None, 1)
    return parts[0] if parts else ""


# Builds the audit hook. box holds the fetch folder; None there refuses every write.
def _make_hook(box):
    realpath, normcase, isabs = os.path.realpath, os.path.normcase, os.path.isabs
    join, dirname, getcwd = os.path.join, os.path.dirname, os.getcwd
    fspath, fsdecode = os.fspath, os.fsdecode
    write_flags = _write_flags()
    devices = ("nul", "\\\\.\\nul")
    pipes = "\\\\.\\pipe\\"
    local = threading.local()

    def text(path):
        path = fspath(path)
        return fsdecode(path) if isinstance(path, bytes) else path

    def inside(path):
        root = box[0]
        if root is None or path is None or isinstance(path, int):
            return False
        return normcase(realpath(text(path))).startswith(root + "\\")

    def writable(path):
        if path is None or isinstance(path, int):
            return False
        name = normcase(text(path))
        return name in devices or name.startswith(pipes) or inside(path)

    def program(executable, args):
        if executable is None and args is not None:
            if isinstance(args, (str, bytes)):
                executable = _first_word(text(args))
            elif len(args):
                executable = args[0]
        if executable is None:
            return False
        name = text(executable)
        return isabs(name) and inside(name)

    def refuse():
        raise PermissionError(errno.EACCES, REFUSED_TEXT)

    def check(event, args):
        if event == "open":
            path, mode, flags = args[0], args[1], args[2]
            wants = (isinstance(mode, str) and any(c in mode for c in "wax+")) or \
                (isinstance(flags, int) and flags & write_flags)
            if wants and not isinstance(path, int) and not writable(path):
                refuse()
        elif event in REFUSED_EVENTS:
            refuse()
        elif event in WRITTEN_PATHS:
            for index in WRITTEN_PATHS[event]:
                if not writable(args[index]):
                    refuse()
        elif event in PROGRAM_EVENTS:
            first, second = PROGRAM_EVENTS[event]
            if not program(args[first], None if second is None else args[second]):
                refuse()
        elif event == "os.symlink":
            target = text(args[0])
            if not isabs(target):
                target = join(dirname(text(args[1])), target)
            if not writable(args[1]) or not writable(target):
                refuse()
        elif event == "os.truncate":
            if not isinstance(args[0], int) and not writable(args[0]):
                refuse()
        elif event == "shutil.unpack_archive":
            if not writable(getcwd() if args[1] is None else args[1]):
                refuse()
        elif event == "sqlite3.connect":
            name = args[0]
            if isinstance(name, (str, bytes)) and text(name) in ("", ":memory:"):
                return
            if (isinstance(name, str) and name.startswith("file:")) or not writable(name):
                refuse()
        elif event == "_winapi.CreateFile":
            if (args[1] & FILE_WRITE_ACCESS or args[3] in FILE_WRITE_DISPOSITIONS) and not writable(args[0]):
                refuse()
        elif event == "winreg.OpenKey":
            if args[2] & KEY_WRITE_ACCESS:
                refuse()
        elif event == "_winapi.OpenProcess":
            if args[1] & ~PROCESS_READ_ACCESS:
                refuse()

    def hook(event, args):
        if event not in WATCHED or getattr(local, "busy", False):
            return
        local.busy = True
        try:
            check(event, args)
        except PermissionError:
            raise
        except Exception:
            raise PermissionError(errno.EACCES, REFUSED_TEXT) from None
        finally:
            local.busy = False

    return hook


# Raises events through the installed hook and checks that it refuses outside and allows inside.
def _self_check(root):
    outside = os.path.join(os.path.dirname(os.path.abspath(__file__)), "no-such-folder-%d" % os.getpid())
    refused = (("open", (os.path.join(outside, "probe.txt"), "w", os.O_WRONLY | os.O_CREAT)),
               ("os.remove", (os.path.join(outside, "probe.txt"), None)),
               ("os.rename", (os.path.join(root, "a"), os.path.join(outside, "b"), None, None)),
               ("winreg.CreateKey", (None, "Software", 0)),
               ("subprocess.Popen", (os.path.join(outside, "probe.exe"), None, None, None)),
               ("os.kill", (0, 9)))
    allowed = (("open", (os.path.join(root, "probe.txt"), "w", os.O_WRONLY | os.O_CREAT)),
               ("os.rename", (os.path.join(root, "a"), os.path.join(root, "b"), None, None)),
               ("subprocess.Popen", (os.path.join(root, "probe.exe"), None, None, None)))
    for event, args in refused:
        try:
            sys.audit(event, *args)
        except PermissionError:
            continue
        return False
    for event, args in allowed:
        try:
            sys.audit(event, *args)
        except Exception:
            return False
    return True


# Fills the platform cache, so a later platform.platform() starts no program once the hook refuses that.
def _warm_platform():
    try:
        import platform
        platform.platform()
    except Exception:
        pass


# Puts the guard in place once. A failed check leaves the hook refusing every write.
def register():
    global _state
    existing = getattr(sys, "_plynth_fetch_guard", None)
    if existing is not None:
        _state = existing
        return
    state = _State()
    _state = state
    sys._plynth_fetch_guard = state
    sys.dont_write_bytecode = True
    _warm_platform()
    state.versions = _read_versions()
    root = _fetch_root()
    box = [root]
    try:
        sys.addaudithook(_make_hook(box))
    except Exception:
        return
    if root is None or not _self_check(root):
        box[0] = None
        return
    state.root = root
    state.reason = "" if _version_tested(state.versions) else "untested_blender"


# The guard cannot be taken out, so this does nothing.
def unregister():
    pass


# The guard's state, or None when it never loaded.
def _current():
    return _state if _state is not None else getattr(sys, "_plynth_fetch_guard", None)


# Raises GuardError unless the guard is in force for private_folder in a tested Blender.
def require(private_folder):
    state = _current()
    if state is None or state.root is None or state.reason == "guard_not_ready":
        raise GuardError("guard_not_ready")
    if state.reason == "untested_blender" or not _version_tested(state.versions):
        raise GuardError("untested_blender", version_sentence(state.versions))
    try:
        same = os.path.normcase(os.path.realpath(os.fspath(private_folder))).rstrip("\\/") == state.root
    except (OSError, TypeError, ValueError):
        same = False
    if not same:
        raise GuardError("wrong_folder")


# The type name Blender gives an operator, "preferences.addon_enable" to "PREFERENCES_OT_addon_enable".
def operator_type_name(idname):
    group, name = idname.split(".", 1)
    return "%s_OT_%s" % (group.upper(), name)


# A stand-in for enabling or disabling an add-on after the seal.
def _refused(*args, **kwargs):
    raise PermissionError(errno.EACCES, REFUSED_TEXT)


# Registers a throwaway operator, takes a callable to it and unregisters it. True when the callable worked
# before and raises after, without running the operator again.
def _prove_unregister():
    import bpy
    ran = []

    class PLYNTH_OT_guard_probe(bpy.types.Operator):
        bl_idname = "plynth.guard_probe"
        bl_label = "Guard probe"

        def execute(self, context):
            ran.append(True)
            return {"FINISHED"}

    bpy.utils.register_class(PLYNTH_OT_guard_probe)
    call = bpy.ops.plynth.guard_probe
    try:
        call()
    finally:
        bpy.utils.unregister_class(PLYNTH_OT_guard_probe)
    if ran != [True] or hasattr(bpy.types, "PLYNTH_OT_guard_probe"):
        return False
    try:
        call()
    except Exception:
        return ran == [True]
    return False


# Closes what Python can close once the add-ons are loaded: unregisters the add-on, extension and preference
# operators (SEALED_OPERATORS, every extensions.package_ one and the given idnames), replaces
# addon_utils.enable and disable and each (module, name, function) in replacements, and proves each step.
# Raises GuardError when this Blender is not tested or any proof fails. It runs once.
def seal(operators=(), replacements=()):
    state = _current()
    if state is None or state.root is None or state.reason == "guard_not_ready":
        raise GuardError("guard_not_ready")
    if state.seal_tried:
        raise GuardError("already_sealed")
    state.seal_tried = True
    if state.reason == "untested_blender" or not _version_tested(state.versions):
        raise GuardError("untested_blender", version_sentence(state.versions))
    try:
        import addon_utils
        import bpy
        if not _prove_unregister():
            raise GuardError("seal_failed")
        names = [operator_type_name(idname) for idname in tuple(SEALED_OPERATORS) + tuple(operators)]
        names += [name for name in dir(bpy.types) if name.startswith(EXTENSION_PREFIX) and name not in names]
        for name in names:
            cls = getattr(bpy.types, name, None)
            if cls is None:
                raise GuardError("seal_failed")
            bpy.utils.unregister_class(cls)
        if any(hasattr(bpy.types, name) for name in names):
            raise GuardError("seal_failed")
        swaps = [(addon_utils, "enable", _refused), (addon_utils, "disable", _refused)] + list(replacements)
        for module, name, function in swaps:
            setattr(module, name, function)
        if any(getattr(module, name, None) is not function for module, name, function in swaps):
            raise GuardError("seal_failed")
    except GuardError:
        raise
    except Exception:
        raise GuardError("seal_failed") from None
    state.sealed = True


# The integrity level of this process, from its token: 0x1000 is low.
def _integrity_level():
    import ctypes
    from ctypes import wintypes
    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    advapi.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    advapi.GetTokenInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD,
                                           ctypes.POINTER(wintypes.DWORD)]
    advapi.GetSidSubAuthorityCount.argtypes = [ctypes.c_void_p]
    advapi.GetSidSubAuthorityCount.restype = ctypes.POINTER(ctypes.c_ubyte)
    advapi.GetSidSubAuthority.argtypes = [ctypes.c_void_p, wintypes.DWORD]
    advapi.GetSidSubAuthority.restype = ctypes.POINTER(wintypes.DWORD)
    token = wintypes.HANDLE()
    if not advapi.OpenProcessToken(kernel.GetCurrentProcess(), 0x0008, ctypes.byref(token)):
        raise OSError(ctypes.get_last_error(), "no process token")
    try:
        size = wintypes.DWORD()
        advapi.GetTokenInformation(token, 25, None, 0, ctypes.byref(size))
        buffer = ctypes.create_string_buffer(size.value)
        if not size.value or not advapi.GetTokenInformation(token, 25, buffer, size, ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), "no integrity level")
        sid = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_void_p))[0]
        count = advapi.GetSidSubAuthorityCount(sid)[0]
        return advapi.GetSidSubAuthority(sid, count - 1)[0]
    finally:
        kernel.CloseHandle(token)


# Raises GuardError unless this process runs at low integrity or below.
def require_low_integrity():
    try:
        level = _integrity_level()
    except Exception:
        raise GuardError("not_low_integrity") from None
    if level > LOW_INTEGRITY:
        raise GuardError("not_low_integrity")
