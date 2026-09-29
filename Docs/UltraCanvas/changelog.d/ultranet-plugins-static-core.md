- **UltraNet plug-ins reach the core only through the host table, so they
  load on a static core and link on Windows without one.** A plug-in DSO used
  to resolve the core functions it calls (`UltraNet_ParseUrl`,
  `UltraNet_UrlEncode`, `UltraNet_ResolveCaBundlePath`, `UltraNet_MimeBuild`,
  `UltraNet_HttpRequest`, ...) from the host when it loaded. A shared
  libUltraCanvas always had them. A static one - the default on macOS and
  Windows, and on Linux without DemoApp - only puts into the executable the
  objects the app itself uses, so `dlopen(RTLD_NOW)` refused any plug-in
  calling one the app never did: EmailCleaner, which never parses a URL, could
  not load the IMAP plug-in, and UltraMail was exposed the same way. And a
  Windows plug-in DLL could only be linked against a shared core's import
  library - hidden only because the LaTeX plug-in forces a shared core there.
  - `UltraNetPluginHost` is ABI 2: after `RegisterPlugin` it carries every
    core function a plug-in uses (appended, so an ABI-1 plug-in still reads a
    new host's table). `UltraNet_GetPluginHost()` returns the host's table.
  - `Plugins/UltraNet/common/UltraNetPluginHostShim.cpp`, compiled into all
    eighteen plug-ins with hidden visibility, defines those functions inside
    the plug-in and forwards each call to the table, so plug-in sources are
    unchanged; `UltraNet_PluginInit` attaches the host first and registers
    nothing for a host older than ABI 2.
  - The plug-ins no longer link the core on Windows, macOS no longer links
    them with `-undefined dynamic_lookup`, and the in-tree plug-ins no longer
    export the POSIX-only v1 entry (`UltraNet_PluginRegister`), which worked
    only by resolving `UltraNet_RegisterPlugin` from the host. The loader
    still accepts v1 from third-party plug-ins.
  - The LDAP and WebDAV plug-ins used `UltraCanvas::Trim`, UltraCanvas
    utility code; they use the header-only `UltraCanvas::TrimWhitespace` now.
  - New `UltraNetPluginHostImports` test (`ctest -R UltraNet`, Linux and
    macOS): `scripts/check_ultranet_plugin_imports.py` fails when a built
    plug-in has any undefined core symbol - a shared-core Linux build would
    resolve it anyway, so a load test there could not notice.
  - Verified on a static-core Linux build: twelve built plug-ins with no
    undefined core symbol, the UltraNet suites green, and EmailCleaner loading
    the IMAP plug-in and reaching the server through it.
