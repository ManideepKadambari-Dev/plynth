import ast
import importlib.util
import json
import zlib

import bpy

from .. import registry, surfaces

MARKER = "plynth-generator"
FORMAT = "plynth-generator"
GENERATED = "Generated"


class AddonUnavailable(Exception):
    pass


class GeneratorError(Exception):
    pass


# Reads the declaration from the docstring of a generator file without running it. Raises GeneratorError.
def read_declaration(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            tree = ast.parse(handle.read(), filename=path)
    except (OSError, SyntaxError, ValueError) as error:
        raise GeneratorError("The generator file %s cannot be read: %s" % (path, error))
    text = ast.get_docstring(tree, clean=False)
    if text is None:
        raise GeneratorError("The generator file has no docstring.")
    first, _, rest = text.lstrip().partition("\n")
    if first.strip() != MARKER:
        raise GeneratorError("The docstring does not start with %s." % MARKER)
    try:
        declaration = json.loads(rest)
    except ValueError as error:
        raise GeneratorError("The declaration is not valid JSON: %s" % error)
    if not isinstance(declaration, dict) or declaration.get("format") != FORMAT:
        raise GeneratorError("The declaration is not a %s." % FORMAT)
    return declaration


class Loaded:
    # A generator file that was read and run once: its declaration and its generate function.
    def __init__(self, generator_id, declaration, generate):
        self.id = generator_id
        self.declaration = declaration
        self.generate = generate


# Reads the declaration and runs the file to get its generate function. Raises GeneratorError.
def load_generator(path, generator_id):
    declaration = read_declaration(path)
    spec = importlib.util.spec_from_file_location("plynth_generator_%s" % generator_id, path)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as error:
        raise GeneratorError("The generator file could not be run: %s" % last_line(error))
    generate = getattr(module, "generate", None)
    if not callable(generate):
        raise GeneratorError("The generator file has no generate function.")
    return Loaded(generator_id, declaration, generate)


# Returns the last line of an error's message, or its class name when the message is empty.
def last_line(error):
    lines = [line for line in str(error).strip().splitlines() if line.strip()]
    return lines[-1].strip() if lines else type(error).__name__


class Gen:
    # What generate(gen) works with. See docs/GENERATORS.md.
    def __init__(self, ctx, loaded, item, collection):
        self._ctx = ctx
        self._loaded = loaded
        self._collection = collection
        self._declared = {p["id"]: p for p in loaded.declaration["params"]}
        self.item = item["id"]
        self.generator = loaded.id
        self.params = self._convert(item["params"])
        self.slots = dict(item.get("slots") or {})
        size = item["size"]
        self.size = (size["width"], size["depth"], size["height"])
        self.seed = zlib.crc32(item["id"].encode("utf-8"))
        self.keep_uvs = False
        self.materials = {}
        self.objects = []

    # Gives numbers as floats and integers as ints, by what the declaration says.
    def _convert(self, values):
        result = {}
        for key, value in values.items():
            kind = self._declared.get(key, {}).get("type")
            if kind == "number" and value is not None:
                value = float(value)
            elif kind == "integer" and value is not None:
                value = int(value)
            result[key] = value
        return result

    # Returns the Blender material of a material parameter, named after the slot the job gives it.
    def material(self, param_id):
        if self._declared.get(param_id, {}).get("type") != "material":
            raise GeneratorError("%s is not a material parameter." % param_id)
        mat = surfaces.material(slot_of(self.slots, param_id))
        self.materials[param_id] = mat
        return mat

    # Makes a mesh object in the generator's collection from vertices and faces.
    def mesh_object(self, name, verts, faces):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        return self.link(obj)

    # Links an object made another way into the generator's collection.
    def link(self, obj):
        for old in list(obj.users_collection):
            old.objects.unlink(obj)
        self._collection.objects.link(obj)
        return obj

    # Returns the bridge of an add-on the declaration lists, with the add-on enabled.
    def bridge(self, short_name):
        if short_name not in self._loaded.declaration.get("addons", []):
            raise AddonUnavailable("The generator does not list the add-on %s." % short_name)
        from . import addons
        if not addons.enable(short_name):
            raise AddonUnavailable("The add-on %s could not be enabled." % short_name)
        try:
            from .. import bridges
            registry.discover(bridges)
        except ImportError:
            raise AddonUnavailable("There is no bridge for the add-on %s." % short_name)
        found = registry.ADDON_BRIDGES.get(short_name)
        if found is None:
            raise AddonUnavailable("There is no bridge for the add-on %s." % short_name)
        return found

    # Writes a line to the log.
    def log(self, text):
        print("generator %s %s: %s" % (self.generator, self.item, text))


# Returns the slot name the job gives a material parameter, or the neutral slot when the job gives none.
def slot_of(slots, param_id):
    name = slots.get(param_id)
    return name if isinstance(name, str) and name else GENERATED
