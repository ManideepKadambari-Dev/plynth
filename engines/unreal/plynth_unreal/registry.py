import importlib
import pkgutil
from collections import namedtuple

# A delivery step: where it runs in the order, the error code a failure in it gets, whether it runs for a job
# (a function of the context) and the function that does the work.
Step = namedtuple("Step", "name order code applies run")

# A self check: its id in the result, where it runs in the order, and a function returning (status, message).
Check = namedtuple("Check", "id order run")


class Registry:
    # Makes an empty registry for one kind of entry.
    def __init__(self, kind):
        self.kind = kind
        self._entries = {}

    # Adds an entry and refuses a name that is already taken.
    def register(self, name, entry):
        if name in self._entries:
            raise ValueError("%s '%s' is already registered" % (self.kind, name))
        self._entries[name] = entry
        return entry

    # Returns every entry sorted by its order, then by name.
    def ordered(self):
        return sorted(self._entries.values(), key=lambda entry: (entry.order, entry_name(entry)))


# Returns the name or id an entry is known by.
def entry_name(entry):
    return getattr(entry, "name", None) or getattr(entry, "id", "")


STEPS = Registry("step")
CHECKS = Registry("check")


# Registers a delivery step. Used as a decorator on its run function.
def step(name, order, code, applies=None):
    def wrap(run):
        STEPS.register(name, Step(name, order, code, applies or (lambda ctx: True), run))
        return run
    return wrap


# Registers a self check. Used as a decorator on its run function.
def check(check_id, order):
    def wrap(run):
        CHECKS.register(check_id, Check(check_id, order, run))
        return run
    return wrap


# Imports every module of a package, in name order, so each one registers itself.
def discover(package):
    names = sorted(name for _, name, _ in pkgutil.iter_modules(package.__path__) if not name.startswith("_"))
    for name in names:
        importlib.import_module(package.__name__ + "." + name)
    return names
