import unittest

import helpers  # noqa: F401
from plynth import registry, tasks


class RegistryTests(unittest.TestCase):
    # Every registry refuses a second entry with the same id.
    def test_duplicates_are_refused(self):
        for instance in (registry.TASKS, registry.IMPORTERS, registry.CLEANUP_STEPS, registry.CHECKS):
            fresh = registry.Registry(instance.kind)
            fresh.register("one", object())
            with self.assertRaises(ValueError):
                fresh.register("one", object())
        registry.discover(tasks)
        with self.assertRaises(ValueError):
            registry.TASKS.register("probe", lambda ctx: None)

    # The registries keep the order entries were added in and return None for unknown ids.
    def test_lookup_and_order(self):
        fresh = registry.Registry("thing")
        fresh.register("b", 2)
        fresh.register("a", 1)
        self.assertEqual(fresh.ids(), ["b", "a"])
        self.assertEqual(fresh.all(), [("b", 2), ("a", 1)])
        self.assertEqual(fresh.get("a"), 1)
        self.assertIsNone(fresh.get("missing"))

    # Discovery imports every module of the tasks package, so the probe is registered.
    def test_discover_finds_probe(self):
        names = registry.discover(tasks)
        self.assertIn("probe", names)
        self.assertIsNotNone(registry.TASKS.get("probe"))
        self.assertEqual(registry.discover(tasks), names)


if __name__ == "__main__":
    unittest.main()
