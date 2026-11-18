#!/usr/bin/env python3
"""How scripts/check_libcxx_syntax.py treats the build it is pointed at: one
outside the checkout is named without a ValueError; one configured without a
compile database is configured again under the folders its own cache names -
so a build reached through a symbolic link keeps its home - and one whose
cache names another folder is refused, not re-homed, with or without its
database; a database the cache promises and the folder lacks is said; the sources a build
generated are left out wherever the build is. CMake is not run: the call is
recorded. Run with

    python3 -m unittest discover -s scripts/tests -v
"""
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parent.parent / "check_libcxx_syntax.py"
spec = importlib.util.spec_from_file_location("check_libcxx_syntax", SCRIPT)
lc = importlib.util.module_from_spec(spec)
sys.modules["check_libcxx_syntax"] = lc
spec.loader.exec_module(lc)


def write_cache(folder, home, source, export=False):
    """A CMakeCache.txt as CMake writes one, with the entries the script reads."""
    lines = [
        "# This is the CMakeCache file.",
        "//Path to a program.",
        "CMAKE_CACHEFILE_DIR:INTERNAL=%s" % home,
        "CMAKE_HOME_DIRECTORY:INTERNAL=%s" % source,
        "CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=%s" % ("ON" if export else "OFF"),
    ]
    (Path(folder) / "CMakeCache.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


class FakeCmake:
    """subprocess.run in place of CMake: records the arguments and writes the
    database into the -B folder, as a configure with the option would."""

    def __init__(self):
        self.calls = []

    def __call__(self, args, **kwargs):
        self.calls.append(list(args))
        home = Path(args[args.index("-B") + 1])
        (home / "compile_commands.json").write_text("[]", encoding="utf-8")
        return mock.Mock(returncode=0, stdout="", stderr="")


class BuildOutsideTheCheckout(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.real = Path(self.tmp.name).resolve() / "build"
        self.real.mkdir()

    def load(self, folder):
        fake = FakeCmake()
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(lc.subprocess, "run", fake), redirect_stdout(out), redirect_stderr(err):
            db = lc.load_database(Path(folder) / "compile_commands.json")
        return db, fake.calls, out.getvalue() + err.getvalue()

    def test_a_folder_outside_is_shown_as_it_is(self):
        self.assertEqual(lc.shown(self.real), str(self.real))
        self.assertEqual(lc.shown(lc.ROOT / "scripts"), "scripts")

    def test_it_is_configured_under_the_folders_its_cache_names(self):
        write_cache(self.real, self.real, lc.ROOT)
        db, calls, said = self.load(self.real)
        self.assertEqual(db, [])
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][calls[0].index("-B") + 1], str(self.real))
        self.assertEqual(calls[0][calls[0].index("-S") + 1], str(lc.ROOT))
        self.assertIn(str(self.real), said)

    @unittest.skipIf(os.name == "nt", "symbolic links need privileges on Windows")
    def test_one_reached_through_a_link_keeps_its_home(self):
        write_cache(self.real, self.real, lc.ROOT)
        link = Path(self.tmp.name) / "link"
        link.symlink_to(self.real, target_is_directory=True)
        db, calls, _ = self.load(link)
        self.assertEqual(db, [])
        # The folder the cache names, not the link's spelling.
        self.assertEqual(calls[0][calls[0].index("-B") + 1], str(self.real))

    def test_one_whose_cache_names_another_folder_is_refused(self):
        write_cache(self.real, Path(self.tmp.name) / "elsewhere", lc.ROOT)
        db, calls, said = self.load(self.real)
        self.assertIsNone(db)
        self.assertEqual(calls, [])
        self.assertIn("elsewhere", said)

    def test_one_with_the_database_is_read_as_it_is(self):
        write_cache(self.real, self.real, lc.ROOT, export=True)
        (self.real / "compile_commands.json").write_text('[{"file": "x.cpp"}]', encoding="utf-8")
        db, calls, _ = self.load(self.real)
        self.assertEqual(db, [{"file": "x.cpp"}])
        self.assertEqual(calls, [])

    def test_a_moved_build_with_its_database_is_refused(self):
        # Its database runs the commands in the old folder: refused before
        # a compile is started in a folder that is not there.
        write_cache(self.real, Path(self.tmp.name) / "elsewhere", lc.ROOT, export=True)
        (self.real / "compile_commands.json").write_text('[{"file": "x.cpp"}]', encoding="utf-8")
        db, calls, said = self.load(self.real)
        self.assertIsNone(db)
        self.assertEqual(calls, [])
        self.assertIn("elsewhere", said)

    def test_the_database_missing_is_said(self):
        write_cache(self.real, self.real, lc.ROOT, export=True)
        db, calls, said = self.load(self.real)
        self.assertIsNone(db)
        self.assertEqual(calls, [])
        self.assertIn("compile_commands.json", said)

    def test_no_cache_is_no_build(self):
        db, calls, said = self.load(self.real)
        self.assertIsNone(db)
        self.assertEqual(calls, [])
        self.assertIn("no build", said)


class GeneratedSources(unittest.TestCase):
    def entry(self, path):
        return {"file": str(path), "directory": str(lc.ROOT), "command": "c++ -c %s" % path}

    def test_the_build_checked_is_left_out_wherever_it_is(self):
        source = lc.ROOT / "UltraCanvas" / "core" / "UltraCanvasUtils.cpp"
        in_build = lc.ROOT / "build" / "generated" / "a.cpp"
        in_other = lc.ROOT / "cmake-build-release" / "generated" / "b.cpp"
        db = [self.entry(source), self.entry(in_build), self.entry(in_other)]
        kept = [f for f, _ in lc.select_units(db, [], lc.ROOT / "cmake-build-release")]
        self.assertEqual(kept, [source])
        # Without the build named, only build/ is known to be generated.
        kept = [f for f, _ in lc.select_units(db, [])]
        self.assertEqual(kept, [source, in_other])


if __name__ == "__main__":
    unittest.main()
