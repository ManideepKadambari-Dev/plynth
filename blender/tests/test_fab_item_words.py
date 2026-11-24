import unittest

from plynth.stores import fab_materials, fab_names


class ItemWordTests(unittest.TestCase):
    # Bent in an item's name leaves each texture on its channel.
    def test_bent_in_the_item_name(self):
        for title in ("Bent_Wood_Chair", "BentSteel_Rail", "T_Old_Bent_Pipe"):
            with self.subTest(title=title):
                files = ["%s_%s.png" % (title, channel) for channel in ("BaseColor", "Normal", "Roughness")]
                self.assertEqual([fab_names.classify(name)[0] for name in files], ["base_color", "normal", "roughness"])
                sets = list(fab_materials.make_sets(files).values())
                self.assertEqual(len(sets), 1)
                self.assertEqual(sorted(sets[0]["channels"]), ["base_color", "normal", "roughness"])

    # A bent normal map is still left out, also for such an item.
    def test_bent_normal_map(self):
        for name in ("Wood_Bent_Normal.png", "Wood_BentNormal.png", "Bent_Wood_Chair_Bent_Normal.png"):
            with self.subTest(name=name):
                self.assertEqual(fab_names.classify(name)[0], "ignored")


if __name__ == "__main__":
    unittest.main()
