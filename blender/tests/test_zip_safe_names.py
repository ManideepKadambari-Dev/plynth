import os
import unittest
import zipfile

import helpers
from plynth.importers import zip as zip_importer
from plynth.jobs import WorkerError

UNSAFE = ("../a.png", "/a.png", "C:a.png", "Textures/a:b.png", "a.png:stream", "CON.png", "con.PNG", "Textures/nul",
          "PRN.txt", "COM1.jpg", "LPT9.png", "AUX", "NUL.tar.png", "Aux/a.png", "Wood./a.png", "Wood /a.png",
          ".. /a.png", "a.png.", "a.png ", "nul .png", "CONIN$", "conout$.png", "COM¹.png", "lpt³.jpg")
SAFE = ("./a.png", "Textures/Console.png", "a..b.png", "COM10.png", "Auxiliary/a.png", "My Textures/a b.png")


class SafeNameTests(helpers.TempFolderTest):
    # A name that Windows maps to a device or another file is unsafe and nothing of such a zip is written. The
    # zip is only unpacked once its name is known to be refused, so a failing test never opens a device.
    def test_unsafe_names(self):
        for number, name in enumerate(UNSAFE):
            with self.subTest(name=name):
                self.assertTrue(zip_importer.is_unsafe(name))
                archive = self.path("%d.zip" % number)
                with zipfile.ZipFile(archive, "w") as target:
                    target.writestr("ok.png", b"x")
                    target.writestr(name, b"x")
                for members in (None, ["ok.png", name]):
                    folder = self.path("out_%d_%s" % (number, "all" if members is None else "some"))
                    with self.assertRaises(WorkerError) as caught:
                        zip_importer.unpack(archive, folder, members)
                    self.assertEqual(caught.exception.code, "unpack_failed")
                    self.assertFalse(os.path.exists(folder) and os.listdir(folder))

    # Names that only look alike stay safe.
    def test_safe_names(self):
        for name in SAFE:
            with self.subTest(name=name):
                self.assertFalse(zip_importer.is_unsafe(name))


if __name__ == "__main__":
    unittest.main()
