#!/usr/bin/env python3
"""What scripts/pe_imports.py reads: the DLL names in a PE32+ and a PE32
import table, nothing from a file that is not a PE image or has no import
directory, and the closure over a folder (a DLL named by an import that the
folder carries, and the DLLs it names in turn, copied once each, matched
without regard to case). The images are built here, byte by byte. Run with

    python3 -m unittest discover -s scripts/tests -v
"""
import importlib.util
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "pe_imports.py"
spec = importlib.util.spec_from_file_location("pe_imports", SCRIPT)
pe = importlib.util.module_from_spec(spec)
sys.modules["pe_imports"] = pe
spec.loader.exec_module(pe)


def build_pe(dll_names, plus=True):
    """A minimal PE image with one section holding an import directory that
    names `dll_names`. PE32+ by default, PE32 with plus=False."""
    section_rva = 0x1000
    file_align = 0x200
    # Section contents: the import descriptors, a terminating zero
    # descriptor, then the names.
    descriptors = bytearray()
    names = bytearray()
    name_rvas = []
    desc_size = 20 * (len(dll_names) + 1)
    for name in dll_names:
        name_rvas.append(section_rva + desc_size + len(names))
        names += name.encode("ascii") + b"\0"
    for rva in name_rvas:
        descriptors += struct.pack("<IIIII", 0, 0, 0, rva, 0)
    descriptors += b"\0" * 20
    body = bytes(descriptors + names)
    raw_size = (len(body) + file_align - 1) // file_align * file_align

    optional_size = 240 if plus else 224
    headers_size = 0x40 + 4 + 20 + optional_size + 40
    headers_raw = (headers_size + file_align - 1) // file_align * file_align

    dos = bytearray(0x40)
    dos[:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)
    machine = 0x8664 if plus else 0x14C
    coff = struct.pack("<HHIIIHH", machine, 1, 0, 0, 0, optional_size, 0x22)
    magic = 0x20B if plus else 0x10B
    opt = bytearray(optional_size)
    struct.pack_into("<H", opt, 0, magic)
    directories = 112 if plus else 96
    struct.pack_into("<I", opt, directories - 4, 16)          # NumberOfRvaAndSizes
    struct.pack_into("<II", opt, directories + 8, section_rva, desc_size)  # import table
    section = struct.pack("<8sIIIIIIHHI", b".idata\0\0", len(body), section_rva,
                          raw_size, headers_raw, 0, 0, 0, 0, 0x40000040)
    image = bytearray(dos) + b"PE\0\0" + coff + bytes(opt) + section
    image += b"\0" * (headers_raw - len(image))
    image += body + b"\0" * (raw_size - len(body))
    return bytes(image)


class Imports(unittest.TestCase):
    def check(self, plus):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "app.exe")
            with open(path, "wb") as f:
                f.write(build_pe(["KERNEL32.dll", "libUltraCanvas.dll"], plus=plus))
            self.assertEqual(pe.imports(path), ["KERNEL32.dll", "libUltraCanvas.dll"])

    def test_pe32_plus(self):
        self.check(plus=True)

    def test_pe32(self):
        self.check(plus=False)

    def test_not_a_pe_image(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "readme.txt")
            with open(path, "wb") as f:
                f.write(b"not a program at all\n")
            self.assertEqual(pe.imports(path), [])
            with open(path, "wb") as f:
                f.write(b"MZ")
            self.assertEqual(pe.imports(path), [])

    def test_no_import_directory(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "empty.dll")
            with open(path, "wb") as f:
                f.write(build_pe([]))
            self.assertEqual(pe.imports(path), [])


class Closure(unittest.TestCase):
    def test_copies_what_the_program_reaches_and_nothing_else(self):
        with tempfile.TemporaryDirectory() as d:
            dist = os.path.join(d, "dist")
            pkg = os.path.join(d, "pkg")
            os.makedirs(dist)
            os.makedirs(pkg)
            files = {
                # The program imports a system DLL (not in dist) and A.
                "UltraCanvasStart.exe": ["KERNEL32.dll", "liba.dll"],
                # A imports B; B imports A back (a cycle) and the system.
                "liba.dll": ["libb.dll", "USER32.dll"],
                "libb.dll": ["liba.dll", "KERNEL32.dll"],
                # C is in dist but nothing reaches it.
                "libc.dll": ["liba.dll"],
            }
            for name, deps in files.items():
                with open(os.path.join(dist, name), "wb") as f:
                    f.write(build_pe(deps))
            # The program is copied into the package first, as the script does.
            with open(os.path.join(pkg, "UltraCanvasStart.exe"), "wb") as f:
                f.write(build_pe(files["UltraCanvasStart.exe"]))

            copied = pe.closure(os.path.join(pkg, "UltraCanvasStart.exe"), dist, pkg)
            self.assertEqual(copied, ["liba.dll", "libb.dll"])
            self.assertEqual(sorted(os.listdir(pkg)), ["UltraCanvasStart.exe", "liba.dll", "libb.dll"])

    def test_names_match_without_regard_to_case(self):
        with tempfile.TemporaryDirectory() as d:
            dist = os.path.join(d, "dist")
            pkg = os.path.join(d, "pkg")
            os.makedirs(dist)
            os.makedirs(pkg)
            with open(os.path.join(dist, "LibCairo-2.dll"), "wb") as f:
                f.write(build_pe([]))
            root = os.path.join(pkg, "app.exe")
            with open(root, "wb") as f:
                f.write(build_pe(["libcairo-2.DLL"]))
            self.assertEqual(pe.closure(root, dist, pkg), ["LibCairo-2.dll"])


if __name__ == "__main__":
    unittest.main()
