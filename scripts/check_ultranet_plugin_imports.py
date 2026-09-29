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
load. The fix for a finding is to add the function to UltraNetPluginHost
(UltraNetPlugins.h, appended, ABI bumped), to the host's table in
core/UltraNet/UltraNetPlugins.cpp and to the shim - or, for UltraCanvas
utility code, to use a header-only helper in the plug-in instead.

Usage:
    check_ultranet_plugin_imports.py <plug-in dir>

Exits 0 when no plug-in needs a core symbol (or none was built), 1 otherwise.
Needs `nm` and `c++filt` (binutils, or the Xcode command-line tools).
"""

import os
import re
import subprocess
import sys

# The symbols a plug-in must not be checked for: its own entry points and the
# C++ runtime. Only names from the framework are the host's to provide.
CORE_PREFIXES = ("UltraNet", "UltraCanvas::", "IUltraNet")


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
    demangled = subprocess.run(["c++filt"], input="\n".join(names),
                               capture_output=True, text=True)
    return demangled.stdout.splitlines() if demangled.returncode == 0 else names


def core_function(symbol):
    """'UltraNet_ParseUrl(std::string const&, ...)' -> 'UltraNet_ParseUrl';
    None for anything that is not the framework's to provide."""
    if symbol.startswith(("vtable for ", "typeinfo for ", "typeinfo name for ")):
        kind, _, rest = symbol.partition(" for ")
        return f"{kind} for {rest}" if rest.startswith(CORE_PREFIXES) else None
    name = symbol.split("(")[0].replace("[abi:cxx11]", "").strip()
    return name if name.startswith(CORE_PREFIXES) else None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    plugin_dir = argv[1]

    plugins = []
    if os.path.isdir(plugin_dir):
        plugins = sorted(os.path.join(plugin_dir, f) for f in os.listdir(plugin_dir)
                         if f.endswith((".so", ".dylib")))
    if not plugins:
        print(f"check_ultranet_plugin_imports: no plug-ins built in {plugin_dir} - nothing to check.")
        return 0

    failures = []
    for plugin in plugins:
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
