#!/usr/bin/env python3
"""Check that no UltraNet plug-in needs a core symbol from its host.

An UltraNet plug-in DSO (Plugins/UltraNet/*.so|.dylib) reaches the core only
through the UltraNetPluginHost table the host hands to UltraNet_PluginInit:
the functions it calls - UltraNet_ParseUrl, UltraNet_MimeBuild, ... - are
defined inside the plug-in by Plugins/UltraNet/common/UltraNetPluginHostShim.cpp,
which forwards each call to that table. So a plug-in must have no undefined
core symbol at all. One that has one loads only where the host happens to
carry it: always with a shared libUltraCanvas, but not into an app on a static
core (which carries only the objects the app itself uses - EmailCleaner could
not load the IMAP plug-in that way), and a Windows DLL cannot link against a
static core at all.

A shared-core build resolves such a symbol anyway, so without this check
nothing would notice until a static build shipped a plug-in that does not
load.

A Windows plug-in (.dll) is read through its import table instead: it must
import nothing from a core DLL (libUltraCanvas*.dll, UltraNet*.dll - nor
from another plug-in) and no core function from any DLL. A plug-in that
started calling the core would otherwise fail to link only on a static-core
build; on a shared-core one it would quietly import from libUltraCanvas.dll
and then not load next to an app that carries the core statically.

The fix for a finding is to add the function to UltraNetPluginHost
(UltraNetPlugins.h, appended, ABI bumped), to the host's table in
core/UltraNet/UltraNetPlugins.cpp and to the shim - or, for UltraCanvas
utility code, to use a header-only helper in the plug-in instead.

Usage:
    check_ultranet_plugin_imports.py <plug-in dir> [--require]

    check_ultranet_plugin_imports.py --self-test

Exits 0 when no plug-in needs a core symbol (or none was built - unless
--require, which the build passes when it builds plug-ins, so an empty or
misplaced output folder fails instead of passing), 1 otherwise.
Needs `nm` and `c++filt` (binutils, or the Xcode command-line tools); for
DLLs `llvm-objdump` or `objdump`, and `llvm-cxxfilt` or `c++filt` (MSYS2's
clang toolchain has the llvm ones).
"""

import os
import re
import shutil
import subprocess
import sys

# The symbols a plug-in must not be checked for: its own entry points and the
# C++ runtime. Only names from the framework are the host's to provide.
CORE_PREFIXES = ("UltraNet", "UltraCanvas::", "IUltraNet")

# A DLL a plug-in must not import from at all: the core libraries, and the
# plug-ins themselves (ultranet_imap.dll ...). Matched case-insensitively.
CORE_DLL = re.compile(r"^(lib)?(ultracanvas|ultranet)", re.IGNORECASE)

# One import entry under a "DLL Name:" header. llvm-objdump prints
# "<hint>  <name>", GNU objdump "<vma> <hint>  <name>"; anything with more
# columns (GNU's per-DLL table summary) or a header word is not an entry.
IMPORT_ENTRY = re.compile(r"^\s*(?:[0-9a-fA-F]+\s+)?\d+\s+(\S+)\s*$")


def first_tool(*names):
    for name in names:
        if shutil.which(name):
            return name
    return None


def demangle(names):
    tool = first_tool("c++filt", "llvm-cxxfilt")
    if not names or not tool:
        return names
    result = subprocess.run([tool], input="\n".join(names), capture_output=True, text=True)
    out = result.stdout.splitlines() if result.returncode == 0 else []
    return out if len(out) == len(names) else names


def parse_pe_imports(objdump_text):
    """[(dll, function)] from `objdump -p` / `llvm-objdump -p` output."""
    imports = []
    dll = None
    for line in objdump_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("DLL Name:"):
            dll = stripped.split(":", 1)[1].strip()
            continue
        if dll is None:
            continue
        if not stripped:
            continue
        if stripped.startswith(("Export Table", "The Export Tables", "There is an export table",
                                "The Function Table", "PE File Base Relocations")):
            dll = None
            continue
        if "Hint/Ord" in stripped or stripped.startswith("lookup "):
            continue
        m = IMPORT_ENTRY.match(line)
        if m:
            imports.append((dll, m.group(1)))
    return imports


def pe_imports(path):
    """[(dll, demangled function)] a DLL imports; None when no objdump."""
    tool = first_tool("llvm-objdump", "objdump")
    if not tool:
        return None
    result = subprocess.run([tool, "-p", path], capture_output=True, text=True)
    if result.returncode != 0:
        return None
    pairs = parse_pe_imports(result.stdout)
    names = demangle([f for _, f in pairs])
    return [(dll, name) for (dll, _), name in zip(pairs, names)]


def dll_findings(path):
    """What a plug-in DLL imports that it must not, as display strings."""
    imports = pe_imports(path)
    if imports is None:
        return None
    found = set()
    for dll, function in imports:
        if CORE_DLL.match(dll):
            found.add(f"imports from {dll}: {function}")
        else:
            name = core_function(function)
            if name:
                found.add(f"{name} (from {dll})")
    return sorted(found)


def undefined_symbols(path):
    """Demangled undefined symbols of one DSO."""
    result = subprocess.run(["nm", "-u", path], capture_output=True, text=True)
    if result.returncode != 0:
        # A MODULE on ELF keeps its dynamic table even when stripped.
        result = subprocess.run(["nm", "-D", "-u", path], capture_output=True, text=True)
    names = []
    for line in result.stdout.splitlines():
        name = line.split()[-1] if line.split() else ""
        name = name.split("@")[0]
        # Mach-O prefixes every C/C++ symbol with an underscore.
        if sys.platform == "darwin" and name.startswith("_"):
            name = name[1:]
        if name:
            names.append(name)
    if not names:
        return []
    return demangle(names)


def core_function(symbol):
    """'UltraNet_ParseUrl(std::string const&, ...)' -> 'UltraNet_ParseUrl';
    None for anything that is not the framework's to provide."""
    if symbol.startswith(("vtable for ", "typeinfo for ", "typeinfo name for ")):
        kind, _, rest = symbol.partition(" for ")
        return f"{kind} for {rest}" if rest.startswith(CORE_PREFIXES) else None
    name = symbol.split("(")[0].replace("[abi:cxx11]", "").strip()
    return name if name.startswith(CORE_PREFIXES) else None


# Both objdump layouts, as printed for a DLL importing UltraNet_ParseUrl from
# libUltraCanvas.dll and GetTickCount from KERNEL32.dll.
_LLVM_SAMPLE = """
The Import Tables:
  lookup 00002098 time 00000000 fwd 00000000 name 000020fc addr 000020b8

    DLL Name: libUltraCanvas.dll
    Hint/Ord  Name
           0  UltraNet_ParseUrl

  lookup 000020a8 time 00000000 fwd 00000000 name 0000210f addr 000020c8

    DLL Name: KERNEL32.dll
    Hint/Ord  Name
           0  GetTickCount

Export Table:
 DLL name: ultranet_bad.dll
 Ordinal      RVA  Name
       1   0x1000  UltraNet_PluginInit
"""
_GNU_SAMPLE = """
\tDLL Name: libUltraCanvas.dll
\tvma:  Hint/Ord Member-Name Bound-To
\t20d8\t    0  UltraNet_ParseUrl

 0000206b\t000020a8 00000000 00000000 0000210f 000020c8

\tDLL Name: KERNEL32.dll
\tvma:  Hint/Ord Member-Name Bound-To
\t20ec\t    0  GetTickCount

There is an export table in .edata at 0x180002000
"""


def self_test():
    """The import-table parser against both objdump layouts."""
    expected = [("libUltraCanvas.dll", "UltraNet_ParseUrl"), ("KERNEL32.dll", "GetTickCount")]
    ok = True
    for label, sample in (("llvm-objdump", _LLVM_SAMPLE), ("GNU objdump", _GNU_SAMPLE)):
        got = parse_pe_imports(sample)
        if got != expected:
            print(f"self-test: {label}: expected {expected}, got {got}")
            ok = False
    if not CORE_DLL.match("libUltraCanvas.dll") or not CORE_DLL.match("ultranet_imap.dll") \
            or CORE_DLL.match("libcurl-4.dll"):
        print("self-test: CORE_DLL matches the wrong names")
        ok = False
    print("check_ultranet_plugin_imports: self-test " + ("passed." if ok else "FAILED."))
    return 0 if ok else 1


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[1] == "--self-test":
        return self_test()
    plugin_dir = argv[1]
    require = "--require" in argv[2:]

    plugins = []
    if os.path.isdir(plugin_dir):
        plugins = sorted(os.path.join(plugin_dir, f) for f in os.listdir(plugin_dir)
                         if f.lower().endswith((".so", ".dylib", ".dll")))
    if not plugins:
        if require:
            print(f"check_ultranet_plugin_imports: no plug-ins in {plugin_dir}, but the build makes "
                  "some - the check would have looked at nothing.")
            return 1
        print(f"check_ultranet_plugin_imports: no plug-ins built in {plugin_dir} - nothing to check.")
        return 0

    failures = []
    for plugin in plugins:
        if plugin.lower().endswith(".dll"):
            found = dll_findings(plugin)
            if found is None:
                # Not a pass: a check that cannot look must not report clean.
                failures.append(f"{os.path.basename(plugin)}: cannot read its import table "
                                "(no llvm-objdump or objdump, or not a PE file)")
                continue
            for line in found:
                failures.append(f"{os.path.basename(plugin)}: {line}")
            continue
        needed = {core_function(s) for s in undefined_symbols(plugin)}
        needed.discard(None)
        for name in sorted(needed):
            failures.append(f"{os.path.basename(plugin)}: {name}")

    if failures:
        print("check_ultranet_plugin_imports: plug-ins need core symbols from their host.\n"
              "Route each through UltraNetPluginHost (UltraNetPlugins.h, the host table in "
              "core/UltraNet/UltraNetPlugins.cpp and Plugins/UltraNet/common/"
              "UltraNetPluginHostShim.cpp) - or, for UltraCanvas utility code, use a "
              "header-only helper in the plug-in:")
        for line in failures:
            print("  " + line)
        return 1
    print(f"check_ultranet_plugin_imports: clean ({len(plugins)} plug-ins, no core "
          f"symbol needed from the host).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
