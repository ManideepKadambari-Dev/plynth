import io
import os
import struct

PREFERENCES_FILE = "userpref.blend"
UNREADABLE_MESSAGE = "The saved Blender settings could not be read."

MAX_FILE_BYTES = 256 * 1024 * 1024
MAX_BLOCKS = 2_000_000
MAX_LIST_ITEMS = 100_000
MAX_DNA_ITEMS = 200_000
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"

ID_PROPERTY_STRING = 0
ID_PROPERTY_GROUP = 6
REPO_FLAG_DISABLED = 1 << 1
REPO_FLAG_CUSTOM_DIRECTORY = 1 << 2
REPO_SOURCE_SYSTEM = 1
BLENDERKIT_MODULE = "blenderkit"


class PreferencesError(Exception):
    # Always carries the same fixed sentence, so nothing from the file can reach a log or a result.
    def __init__(self):
        super().__init__(UNREADABLE_MESSAGE)


class Repository:
    # One extension repository as saved: its module name, whether it is enabled, where it lives.
    def __init__(self, module, enabled, system, custom_folder):
        self.module = module
        self.enabled = enabled
        self.system = system
        self.custom_folder = custom_folder


class Preferences:
    # What the probe needs from the saved settings. The BlenderKit key is only ever a yes or no.
    def __init__(self):
        self.exists = False
        self.addons = []
        self.repositories = []
        self.script_folders = []
        self.blenderkit_enabled = False
        self.blenderkit_has_key = False
        self.blenderkit_global_dir = ""


# Returns the last dotted part of a module name, so bl_ext.user_default.blenderkit gives blenderkit.
def short_name(module_name):
    return module_name.rsplit(".", 1)[-1]


# Opens the file in read only mode, reads it once and closes it again.
def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read(MAX_FILE_BYTES + 1)


# Unpacks a zstd file in memory with a size limit. Plain files come back as they are.
def unpack(raw):
    if raw[:4] != ZSTD_MAGIC:
        return raw
    import zstandard
    reader = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(raw), read_across_frames=True)
    out = bytearray()
    while True:
        chunk = reader.read(1024 * 1024)
        if not chunk:
            break
        out += chunk
        if len(out) > MAX_FILE_BYTES:
            raise PreferencesError()
    return bytes(out)


class BlendFile:
    # A blend file held in memory: the block table and the file's own list of struct layouts.
    def __init__(self, data):
        self.data = data
        self.blocks = {}
        self.first_user_block = None
        self.dna_block = None
        self.structs = {}
        self.type_sizes = {}
        self.read_header()
        self.read_blocks()
        self.read_dna()

    def read_header(self):
        data = self.data
        if len(data) < 17 or data[:7] != b"BLENDER":
            raise PreferencesError()
        if data[7:9] == b"17" and data[9:10] == b"-" and data[10:12].isdigit() and data[12:13] == b"v":
            self.header_size = 17
            self.head = struct.Struct("<4siQqq")
        elif data[7:8] == b"-" and data[8:9] == b"v" and data[9:12].isdigit():
            self.header_size = 12
            self.head = struct.Struct("<4siQii")
        else:
            raise PreferencesError()

    def read_blocks(self):
        data = self.data
        head = self.head
        new_layout = self.header_size == 17
        pos = self.header_size
        count = 0
        while True:
            if pos + head.size > len(data) or count > MAX_BLOCKS:
                raise PreferencesError()
            if new_layout:
                code, _, old, length, _ = head.unpack_from(data, pos)
            else:
                code, length, old, _, _ = head.unpack_from(data, pos)
            start = pos + head.size
            if code == b"ENDB":
                break
            if length < 0 or start + length > len(data):
                raise PreferencesError()
            if code == b"DNA1":
                self.dna_block = (start, length)
            else:
                self.blocks.setdefault(old, (start, length))
                if code == b"USER" and self.first_user_block is None:
                    self.first_user_block = (start, length)
            pos = start + length
            count += 1
        if self.dna_block is None:
            raise PreferencesError()

    def read_dna(self):
        data = self.data
        start, length = self.dna_block
        end = start + length

        def take(pos, tag):
            pos = start + ((pos - start + 3) & ~3)
            if pos + 8 > end or data[pos:pos + 4] != tag:
                raise PreferencesError()
            count = struct.unpack_from("<i", data, pos + 4)[0]
            if count < 0 or count > MAX_DNA_ITEMS:
                raise PreferencesError()
            return pos + 8, count

        def strings(pos, count):
            found = []
            for _ in range(count):
                stop = data.find(b"\0", pos, end)
                if stop < 0:
                    raise PreferencesError()
                found.append(data[pos:stop].decode("utf-8", "replace"))
                pos = stop + 1
            return found, pos

        if data[start:start + 4] != b"SDNA":
            raise PreferencesError()
        pos, count = take(start + 4, b"NAME")
        names, pos = strings(pos, count)
        pos, count = take(pos, b"TYPE")
        types, pos = strings(pos, count)
        pos = start + ((pos - start + 3) & ~3)
        if data[pos:pos + 4] != b"TLEN" or pos + 4 + 2 * len(types) > end:
            raise PreferencesError()
        lengths = struct.unpack_from("<%dh" % len(types), data, pos + 4)
        pos += 4 + 2 * len(types)
        pos, struct_count = take(pos, b"STRC")
        self.type_sizes = {types[i]: lengths[i] for i in range(len(types))}
        for _ in range(struct_count):
            if pos + 4 > end:
                raise PreferencesError()
            type_index, field_count = struct.unpack_from("<hh", data, pos)
            pos += 4
            if pos + 4 * field_count > end or not 0 <= type_index < len(types):
                raise PreferencesError()
            fields = {}
            offset = 0
            for _ in range(field_count):
                field_type, field_name = struct.unpack_from("<hh", data, pos)
                pos += 4
                if not 0 <= field_type < len(types) or not 0 <= field_name < len(names):
                    raise PreferencesError()
                raw_name = names[field_name]
                size = self.field_size(types[field_type], raw_name)
                fields[self.base_name(raw_name)] = offset
                offset += size
            self.structs[types[type_index]] = (lengths[type_index], fields)

    @staticmethod
    def base_name(raw):
        return raw.lstrip("*").lstrip("(*").split(")")[0].split("[")[0]

    def field_size(self, type_name, raw_name):
        if raw_name.startswith("*") or raw_name.startswith("(*"):
            return 8
        size = self.type_sizes.get(type_name)
        if size is None:
            raise PreferencesError()
        for part in raw_name.split("[")[1:]:
            number = part.rstrip("]")
            if not number.isdigit():
                raise PreferencesError()
            size *= int(number)
        return size

    # Returns the offset of a field inside its struct, or None when this file's layout has no such field.
    def offset(self, struct_name, field_name):
        entry = self.structs.get(struct_name)
        return None if entry is None else entry[1].get(field_name)

    def need(self, struct_name, field_name):
        found = self.offset(struct_name, field_name)
        if found is None:
            raise PreferencesError()
        return found

    # Returns the start of the block the old pointer refers to and its length, or None.
    def block(self, pointer):
        return self.blocks.get(pointer) if pointer else None

    def pointer(self, position):
        if position < 0 or position + 8 > len(self.data):
            raise PreferencesError()
        return struct.unpack_from("<Q", self.data, position)[0]

    def integer(self, position):
        if position < 0 or position + 4 > len(self.data):
            raise PreferencesError()
        return struct.unpack_from("<i", self.data, position)[0]

    def byte(self, position):
        if position < 0 or position >= len(self.data):
            raise PreferencesError()
        return self.data[position]

    def text(self, position, limit):
        if position < 0 or position + limit > len(self.data):
            raise PreferencesError()
        chunk = self.data[position:position + limit]
        return chunk.split(b"\0", 1)[0].decode("utf-8", "replace")

    # Walks a linked list of structs whose first member is the next pointer, giving each item's start.
    def walk(self, first_pointer, struct_name):
        size = self.structs[struct_name][0] if struct_name in self.structs else 0
        seen = set()
        pointer = first_pointer
        while pointer:
            if pointer in seen or len(seen) > MAX_LIST_ITEMS:
                raise PreferencesError()
            seen.add(pointer)
            found = self.block(pointer)
            if found is None or found[1] < size:
                raise PreferencesError()
            yield found[0]
            pointer = self.pointer(found[0])

    def list_first(self, base, struct_name, field_name):
        return self.pointer(base + self.need(struct_name, field_name))


# Finds a child of an add-on's property group by name. Returns the child's start, or None.
def group_child(blend, group_start, wanted):
    first = blend.pointer(group_start + blend.need("IDProperty", "data") + 8)
    for child in blend.walk(first, "IDProperty"):
        if blend.text(child + blend.need("IDProperty", "name"), 64) == wanted:
            return child
    return None


# Reads the length a string property holds. The text itself is not touched.
def string_length(blend, start):
    if blend.byte(start + blend.need("IDProperty", "type")) != ID_PROPERTY_STRING:
        return 0
    return blend.integer(start + blend.need("IDProperty", "len"))


# Reads the text of a string property. Used for the BlenderKit data folder only, never for a key.
def string_value(blend, start):
    if blend.byte(start + blend.need("IDProperty", "type")) != ID_PROPERTY_STRING:
        return ""
    found = blend.block(blend.pointer(start + blend.need("IDProperty", "data")))
    if found is None:
        return ""
    return blend.text(found[0], min(found[1], 4096))


# Reads the BlenderKit part of the saved settings: whether a key is stored and the data folder.
def read_blenderkit(blend, addon_start, prefs):
    prop = blend.pointer(addon_start + blend.need("bAddon", "prop"))
    found = blend.block(prop)
    if found is None or blend.byte(found[0] + blend.need("IDProperty", "type")) != ID_PROPERTY_GROUP:
        return
    key = group_child(blend, found[0], "api_key")
    if key is not None:
        prefs.blenderkit_has_key = string_length(blend, key) > 1
    folder = group_child(blend, found[0], "global_dir")
    if folder is not None:
        prefs.blenderkit_global_dir = string_value(blend, folder)


# Decodes the add-ons, repositories and script folders of the saved settings in the blend file.
def decode(blend):
    if blend.first_user_block is None:
        raise PreferencesError()
    user_start, user_length = blend.first_user_block
    if "UserDef" not in blend.structs or user_length < blend.structs["UserDef"][0]:
        raise PreferencesError()
    prefs = Preferences()
    prefs.exists = True

    module_at = blend.need("bAddon", "module")
    first = blend.list_first(user_start, "UserDef", "addons")
    for item in blend.walk(first, "bAddon"):
        name = blend.text(item + module_at, 128)
        if not name:
            continue
        prefs.addons.append(name)
        if short_name(name) == BLENDERKIT_MODULE and not prefs.blenderkit_enabled:
            prefs.blenderkit_enabled = True
            read_blenderkit(blend, item, prefs)

    if blend.offset("UserDef", "extension_repos") is not None and "bUserExtensionRepo" in blend.structs:
        first = blend.list_first(user_start, "UserDef", "extension_repos")
        for item in blend.walk(first, "bUserExtensionRepo"):
            flag = blend.byte(item + blend.need("bUserExtensionRepo", "flag"))
            custom = ""
            if flag & REPO_FLAG_CUSTOM_DIRECTORY:
                custom = blend.text(item + blend.need("bUserExtensionRepo", "custom_dirpath"), 1024)
            prefs.repositories.append(Repository(
                blend.text(item + blend.need("bUserExtensionRepo", "module"), 48),
                not flag & REPO_FLAG_DISABLED,
                blend.byte(item + blend.need("bUserExtensionRepo", "source")) == REPO_SOURCE_SYSTEM,
                custom))

    if blend.offset("UserDef", "script_directories") is not None and "bUserScriptDirectory" in blend.structs:
        first = blend.list_first(user_start, "UserDef", "script_directories")
        for item in blend.walk(first, "bUserScriptDirectory"):
            path = blend.text(item + blend.need("bUserScriptDirectory", "dir_path"), 768)
            if path:
                prefs.script_folders.append(path)
    return prefs


# Reads the saved settings of one config folder as data. No file means Blender's defaults. A file that is
# not understood raises PreferencesError. Nothing is written, imported or loaded, and no key is decoded.
def read_preferences(config_folder):
    path = os.path.join(config_folder, PREFERENCES_FILE)
    try:
        raw = read_bytes(path)
    except FileNotFoundError:
        return Preferences()
    except Exception:
        raise PreferencesError() from None
    try:
        if len(raw) > MAX_FILE_BYTES:
            raise PreferencesError()
        return decode(BlendFile(unpack(raw)))
    except Exception:
        raise PreferencesError() from None
