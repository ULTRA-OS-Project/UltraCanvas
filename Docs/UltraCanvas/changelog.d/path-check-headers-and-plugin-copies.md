- **The path check reads the headers.** `scripts/check_path_string.py` told
  a string from a path only by the declarations in the file itself, so a
  struct member or getter declared in a header slipped through
  (`PathFromUtf8(mailDir_) / env.accountId` in UltraMail's preview,
  `fs::exists(GetConfigPath())`). It now reads the file's own header for
  the class's members, and every in-repo header the file includes
  (transitively, up to 400) for a member access or a call; a name those
  headers declare two different ways is left alone. Calls to a function
  declared as returning `std::string` count as strings in `fs::` arguments
  and stream constructors too, not only in `/` joins. 52 more sites are
  wrapped in `PathFromUtf8`: the Filer widget (current folder, link targets,
  templates), UltraVault's device-key vault, UltraWin's environments and
  associations, the LaTeX reader, the Z-Wave controller path, UltraCloud's
  plug-in folder, VirtualFS's RAM disk and temp files, the Git reader, rich
  document export, and the UltraFiler, UltraAI and UltraSocial apps.
- **A plug-in carrying a copy of the core is caught.** Linking a core
  library *statically* into an UltraNet plug-in leaves nothing undefined
  and nothing imported, so neither import check saw it - but the plug-in
  then has its own copy of the core's globals (the plug-in registry, the
  HTTP session pool, the TLS trust store) beside the host's.
  - `UltraCanvas/CMakeLists.txt` now stops the configure when a plug-in
    target links `UltraCanvas`, `UltraNet`, `UltraCanvasTextUtils` or
    `UltraCanvasAllFormats`, directly or through another target (followed
    through `LINK_LIBRARIES` / `INTERFACE_LINK_LIBRARIES` and
    `$<LINK_ONLY:...>`). Tried with a direct link and with one through an
    interface target: both stop the configure.
  - `scripts/check_ultranet_plugin_imports.py` also reads each plug-in's
    exports - `nm -D` on Linux, `nm -gUm` on macOS, the DLL export table on
    Windows - and fails on a core function exported as a strong definition
    (a plug-in exports `UltraNet_PluginInit`; the weak inline functions and
    type information of the headers are not counted). A test plug-in linked
    against the real `libultranet.a` is reported with the five URL
    functions it pulled in; a DLL exporting `UltraNet_ParseUrl` is
    reported; the 12 real plug-ins are clean. The core marks nothing
    `dllexport`, so a static copy inside a Windows DLL exports nothing -
    that case is the configure guard's. The self-test now covers the export
    parsers of all three platforms.
