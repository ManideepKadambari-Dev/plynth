import importlib
import pkgutil


class Registry:
    # Makes an empty registry for one kind of entry.
    def __init__(self, kind):
        self.kind = kind
        self._entries = {}

    # Adds an entry and refuses an id that is already taken.
    def register(self, entry_id, obj):
        if entry_id in self._entries:
            raise ValueError("%s '%s' is already registered" % (self.kind, entry_id))
        self._entries[entry_id] = obj
        return obj

    # Returns the entry with this id, or None.
    def get(self, entry_id):
        return self._entries.get(entry_id)

    # Returns every (id, entry) pair in the order they were registered.
    def all(self):
        return list(self._entries.items())

    # Returns every id in the order they were registered.
    def ids(self):
        return list(self._entries.keys())


TASKS = Registry("task")
IMPORTERS = Registry("importer")
CLEANUP_STEPS = Registry("cleanup step")
CHECKS = Registry("check")
EXPORTERS = Registry("exporter")
BUILD_STEPS = Registry("build step")
OPENING_FILLS = Registry("opening fill")
MATERIAL_SOURCES = Registry("material source")
BAKERS = Registry("baker")
MATERIAL_CHECKS = Registry("material check")
EXPORT_STEPS = Registry("export step")
QUALITY_CHECKS = Registry("quality check")


# Imports every module of a package, in name order, so each one registers itself.
def discover(package):
    names = sorted(name for _, name, _ in pkgutil.iter_modules(package.__path__) if not name.startswith("_"))
    for name in names:
        importlib.import_module(package.__name__ + "." + name)
    return names
