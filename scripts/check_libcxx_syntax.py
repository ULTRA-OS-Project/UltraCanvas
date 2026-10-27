#!/usr/bin/env python3
"""Type-check the tree against libc++ - the standard library the Windows CI
builds use (MSYS2 CLANG64 / CLANGARM64) - with clang++ -fsyntax-only, so a
Windows-only compile error is seen before a push rather than 15 minutes into
the Windows job. Linux compiles against libstdc++, and the two disagree on
details a Linux build never shows: libc++ gives
std::filesystem::file_time_type a 128-bit tick count, which std::to_string
has no overload for, and PR #973's first Windows build failed on exactly
that ("call to 'to_string' is ambiguous").

Every C++ translation unit of the build's compile database
(build/compile_commands.json - CMake writes it with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON, and this script adds that to an existing
build directory itself) is compiled again with the flags the build gave it,
by clang++ with -stdlib=libc++ and -fsyntax-only. C sources and the Linux,
macOS and Android OS arms (never compiled on Windows) are left out, as is
anything generated under build/. Only errors are printed: the Windows build
does not treat warnings as errors either.

    python3 scripts/check_libcxx_syntax.py                 # the whole tree
    python3 scripts/check_libcxx_syntax.py VideoFX Apps/UltraVideo
    python3 scripts/check_libcxx_syntax.py VideoFX/core/VideoFXExport.cpp

A path argument keeps the translation units under that directory or that
file. --clang names the compiler (default: clang++), --jobs the parallel
compiles (default: the processor count), --db another compile database.

Needs clang++ with libc++ (Debian/Ubuntu: apt install clang libc++-dev
libc++abi-dev) and a configured build (the headers the build found).
Exit 0: every unit compiles; 1: some do not; 2: the toolchain or the
database is missing.
"""
# Version: 2.0.0 - every translation unit of the compile database, not only
#                 VideoFX's sources with a flag block of their own; Python
# Version: 1.0.0 - scripts/check_libcxx_syntax.sh: VideoFX against libc++
# Author: UltraCanvas Framework

import argparse
import concurrent.futures
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Not compiled on Windows, so an error there is not a Windows error.
SKIPPED_DIRS = ("/OS/Linux/", "/OS/MacOS/", "/OS/Android/", "/third_party/")
CXX_SUFFIXES = (".cpp", ".cc", ".cxx", ".mm")

# The build's flags that shape what the compiler sees; the rest (-o, -c,
# -g, the dependency files, the warnings) is dropped - warnings are not the
# question here, and -w keeps the output to errors.
KEPT_PREFIXES = ("-D", "-U", "-I", "-std=", "-f", "-pthread")
KEPT_WITH_VALUE = ("-isystem", "-iquote", "-include", "-idirafter")


def check_toolchain(cxx, work):
    if shutil.which(cxx) is None:
        print("check_libcxx_syntax: %s not found (apt install clang libc++-dev libc++abi-dev, "
              "or pass --clang)" % cxx, file=sys.stderr)
        return False
    probe = work / "probe.cpp"
    probe.write_text("#include <__config>\n#include <filesystem>\nint main() { return 0; }\n")
    r = subprocess.run([cxx, "-std=gnu++20", "-stdlib=libc++", "-fsyntax-only", str(probe)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("check_libcxx_syntax: %s cannot compile with -stdlib=libc++ "
              "(apt install libc++-dev libc++abi-dev)" % cxx, file=sys.stderr)
        return False
    return True


def load_database(path):
    """The compile database, written first when the build directory has none."""
    if not path.exists():
        build = path.parent
        if (build / "CMakeCache.txt").exists():
            print("check_libcxx_syntax: %s has no compile database - configuring it with "
                  "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" % build.relative_to(ROOT))
            r = subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print(r.stdout[-2000:], r.stderr[-2000:], file=sys.stderr)
                return None
        if not path.exists():
            print("check_libcxx_syntax: no %s - configure a build first "
                  "(cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)" % path, file=sys.stderr)
            return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def kept_flags(command):
    """The flags of one build command that shape the compile."""
    args = shlex.split(command)
    out = []
    i = 1   # args[0] is the compiler
    while i < len(args):
        a = args[i]
        if a in KEPT_WITH_VALUE and i + 1 < len(args):
            out += [a, args[i + 1]]
            i += 2
            continue
        if a in ("-o", "-MF", "-MT", "-MQ") and i + 1 < len(args):
            i += 2
            continue
        if a.startswith(KEPT_PREFIXES) and not a.startswith(("-fdiagnostics", "-fdebug", "-fprofile", "-fno-omit-frame")):
            out.append(a)
        i += 1
    return out


def select_units(db, wanted):
    """The C++ translation units to check: every one of the database, or
    those under the paths given, minus what Windows never compiles."""
    roots = [(ROOT / w).resolve() for w in wanted]
    units = []
    seen = set()
    for entry in db:
        file = Path(entry["file"])
        if not file.is_absolute():
            file = (Path(entry["directory"]) / file).resolve()
        s = str(file)
        if file.suffix not in CXX_SUFFIXES or s in seen:
            continue
        if not s.startswith(str(ROOT) + os.sep) or s.startswith(str(ROOT / "build") + os.sep):
            continue
        if any(d in s for d in SKIPPED_DIRS):
            continue
        if roots and not any(file == r or r in file.parents for r in roots):
            continue
        seen.add(s)
        units.append((file, entry))
    return units


def compile_one(cxx, file, entry):
    cmd = [cxx, "-stdlib=libc++", "-fsyntax-only", "-w"] + kept_flags(entry["command"]) + [str(file)]
    r = subprocess.run(cmd, cwd=entry["directory"], capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("paths", nargs="*", help="directories or files to keep (default: everything)")
    parser.add_argument("--db", type=Path, default=ROOT / "build" / "compile_commands.json")
    parser.add_argument("--clang", default=os.environ.get("CLANGXX", "clang++"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--list", action="store_true", help="print the units that would be checked and exit")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        if not check_toolchain(args.clang, Path(tmp)):
            return 2
    db = load_database(args.db)
    if db is None:
        return 2
    units = select_units(db, args.paths)
    if not units:
        print("check_libcxx_syntax: nothing to check under %s" % " ".join(args.paths), file=sys.stderr)
        return 2
    if args.list:
        for file, _ in units:
            print(file.relative_to(ROOT))
        return 0

    failed = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(compile_one, args.clang, f, e): f for f, e in units}
        for fut in concurrent.futures.as_completed(futures):
            ok, output = fut.result()
            if not ok:
                failed.append((futures[fut], output))
    failed.sort()
    for file, output in failed:
        print("  FAIL  %s" % file.relative_to(ROOT))
        for line in output.rstrip().splitlines():
            print("        " + line)
    if failed:
        print("check_libcxx_syntax: %d of %d unit(s) do not compile against libc++" % (len(failed), len(units)))
        return 1
    version = subprocess.run([args.clang, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    print("check_libcxx_syntax: %d unit(s) compile against libc++ (%s)" % (len(units), version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
