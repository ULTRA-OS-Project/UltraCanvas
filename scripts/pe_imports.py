#!/usr/bin/env python3
"""The DLLs a Windows program or library imports, read from its PE import
table in Python - no objdump, no process per file.

    pe_imports.py FILE...                      the DLL names each file imports
    pe_imports.py --closure ROOT --from DIR --into PKG
                                               copy into PKG every DLL that ROOT
                                               imports and DIR carries, and
                                               theirs in turn; print the count

The closure is what scripts/package-ultracanvasstart.sh ships beside
UltraCanvasStart.exe: a DLL an import names that DIR carries (Windows' own
are not in DIR and stay the system's), then the DLLs those name, until
nothing new turns up. It used to be a bash loop of `objdump -p | sed | tr`
with `sort -u` through a process substitution - five processes per DLL, some
five hundred for the 132 DLLs the application loads. On the Windows ARM
runner MSYS2 runs x86-64-emulated, and on 2026-11-13 (#1235) that loop
ended eight seconds in, silently, with status 0: no DLL count, no archive,
the step green, and the upload after it the one that failed. One Python
process reads every import table and copies every file.

PE32 and PE32+ alike (x86, x86-64, ARM64); a file that is not a PE image, or
has no import directory, imports nothing. Names are compared as Windows
does, without regard to case; the copy keeps the name DIR has.
"""
import argparse
import os
import shutil
import struct
import sys


def imports(path):
    """The DLL names `path` imports, in import-table order; [] when it is
    not a PE image or has no import directory."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 0x40 or data[:2] != b"MZ":
        return []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        return []
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if optional + 2 > len(data):
        return []
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x10B:          # PE32
        directories = optional + 96
    elif magic == 0x20B:        # PE32+
        directories = optional + 112
    else:
        return []
    import_entry = directories + 8      # data directory 1: the import table
    if import_entry + 8 > len(data):
        return []
    import_rva, import_size = struct.unpack_from("<II", data, import_entry)
    if import_rva == 0:
        return []

    sections = []
    table = optional + optional_size
    for i in range(section_count):
        header = table + 40 * i
        if header + 40 > len(data):
            break
        virtual_size, virtual_address, raw_size, raw_pointer = \
            struct.unpack_from("<IIII", data, header + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer, raw_size))

    def offset(rva):
        for address, size, pointer, raw in sections:
            if address <= rva < address + size:
                delta = rva - address
                return pointer + delta if delta < raw else None
        return None

    def c_string(rva):
        start = offset(rva)
        if start is None or start >= len(data):
            return None
        end = data.find(b"\0", start)
        return data[start:end if end >= 0 else len(data)].decode("ascii", "replace")

    names = []
    descriptor = offset(import_rva)
    while descriptor is not None and descriptor + 20 <= len(data):
        fields = struct.unpack_from("<IIIII", data, descriptor)
        if not any(fields):
            break
        name = c_string(fields[3])
        if name:
            names.append(name)
        descriptor += 20
    return names


def closure(root, source, into):
    """Copy into `into` every DLL `root` imports that `source` carries, and
    theirs in turn. Returns the names copied, in the order found."""
    carried = {}
    for entry in os.listdir(source):
        if entry.lower().endswith(".dll"):
            carried[entry.lower()] = entry
    copied = []
    queue = [root]
    seen = set()
    while queue:
        current = queue.pop(0)
        for name in imports(current):
            actual = carried.get(name.lower())
            if actual is None or actual.lower() in seen:
                continue
            seen.add(actual.lower())
            shutil.copy2(os.path.join(source, actual), os.path.join(into, actual))
            copied.append(actual)
            queue.append(os.path.join(into, actual))
    return copied


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="*", help="PE files to list the imports of")
    parser.add_argument("--closure", metavar="ROOT", help="the program to start from")
    parser.add_argument("--from", dest="source", metavar="DIR",
                        help="the folder whose DLLs are candidates (the suite package)")
    parser.add_argument("--into", metavar="PKG", help="where the DLLs are copied")
    args = parser.parse_args(argv)

    if args.closure:
        if not (args.source and args.into):
            parser.error("--closure needs --from DIR and --into PKG")
        for name in closure(args.closure, args.source, args.into):
            print(name)
        return 0
    if not args.files:
        parser.error("name a file, or use --closure")
    for path in args.files:
        prefix = f"{path}: " if len(args.files) > 1 else ""
        for name in imports(path):
            print(f"{prefix}{name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
