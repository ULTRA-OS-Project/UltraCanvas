- **UltraNet plug-ins load into an app built on a static core.** A plug-in DSO
  resolves the core functions it calls (`UltraNet_ParseUrl`,
  `UltraNet_UrlEncode`, `UltraNet_ResolveCaBundlePath`, `UltraNet_MimeBuild`,
  ...) from the host when it is loaded. A shared libUltraCanvas always has
  them; a static one — the default on macOS and Windows, and on Linux without
  DemoApp — only puts into the executable the objects the app itself uses, so
  `dlopen(RTLD_NOW)` refused any plug-in calling one the app never did. That is
  how EmailCleaner, which never parses a URL, could not load the IMAP plug-in
  (`UltraNet_ParseUrl` and `UltraNet_UrlEncode` missing), and UltraMail was
  exposed the same way. `core/UltraNet/UltraNetPlugins.cpp` now names every
  such function in one table, `kPluginHostImports`, whose address
  `UltraNet_RefreshPlugins()` takes, so every app that loads plug-ins links
  them all. Verified on a static-core Linux build: all twelve built plug-ins
  resolve every symbol against the EmailCleaner executable.
  - The LDAP and WebDAV plug-ins called `UltraCanvas::Trim`, UltraCanvas
    utility code rather than UltraNet; they use the header-only
    `UltraCanvas::TrimWhitespace` now, so a plug-in depends on nothing but the
    UltraNet host functions.
  - New `UltraNetPluginHostImports` test (`ctest -R UltraNet`, Linux and
    macOS): `scripts/check_ultranet_plugin_imports.py` reads every built
    plug-in's undefined symbols and fails when one names a core function the
    table does not keep. Linux CI builds a shared core, which would resolve a
    forgotten function anyway, so a load test there could not catch it.
