#!/usr/bin/env python3
"""Check that every core function an UltraNet plug-in calls is kept in the host.

An UltraNet plug-in DSO (Plugins/UltraNet/*.so) calls a few core functions -
UltraNet_ParseUrl, UltraNet_ResolveCaBundlePath, UltraNet_MimeBuild, ... - and
resolves them from the host when it is loaded. With a shared libUltraCanvas
they are always there. With a static one (the default on macOS and Windows,
and on Linux without DemoApp) the executable only carries the objects it uses
itself, so a function the app never calls is missing and dlopen(RTLD_NOW)
refuses the plug-in: EmailCleaner, which never parses a URL, could not load
the IMAP plug-in.

UltraCanvas/core/UltraNet/UltraNetPlugins.cpp therefore lists every such
function in `kPluginHostImports`, and UltraNet_RefreshPlugins() takes the
table's address, so any host that loads plug-ins links all of them. This check
keeps that table complete: it reads the undefined symbols of every built
plug-in and fails when one names a core function (UltraNet_*, UltraNet* or
UltraCanvas::*) the table does not. A shared-core build resolves such a symbol
anyway, so without this check nothing would notice until a static build
shipped a plug-in that does not load.

Usage:
    check_ultranet_plugin_imports.py <plug-in dir> [<UltraNetPlugins.cpp>]

Exits 0 when every plug-in is covered (or no plug-in was built), 1 otherwise.
Needs `nm` and `c++filt` (binutils, or the Xcode command-line tools).
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TABLE_SOURCE = os.path.join(
    HERE, "..", "UltraCanvas", "core", "UltraNet", "UltraNetPlugins.cpp")

# The symbols a plug-in must not be checked for: its own entry points and the
# C++ runtime. Only names from the framework are the host's to provide.
CORE_PREFIXES = ("UltraNet", "UltraCanvas::", "IUltraNet")


def table_names(source_path):
    """The functions named in kPluginHostImports, e.g. 'UltraNet_ParseUrl' or
    'UltraNetHttpHeaders::Set'."""
    with open(source_path, encoding="utf-8") as f:
        text = f.read()
    match = re.search(r"kPluginHostImports\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        sys.exit(f"check_ultranet_plugin_imports: no kPluginHostImports table in {source_path}")
    return set(re.findall(r"&\s*([A-Za-z_][A-Za-z0-9_:]*)", match.group(1)))


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
    table_source = argv[2] if len(argv) > 2 else DEFAULT_TABLE_SOURCE
    kept = table_names(table_source)

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
        missing = sorted(n for n in needed if n not in kept)
        for name in missing:
            failures.append(f"{os.path.basename(plugin)}: {name}")

    if failures:
        print("check_ultranet_plugin_imports: plug-ins call core code the host is not "
              "made to keep.\nAdd each function to kPluginHostImports in "
              "UltraCanvas/core/UltraNet/UltraNetPlugins.cpp - or, for UltraCanvas "
              "utility code, use a header-only helper in the plug-in instead:")
        for line in failures:
            print("  " + line)
        return 1
    print(f"check_ultranet_plugin_imports: clean ({len(plugins)} plug-ins, "
          f"{len(kept)} host functions kept).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
