- **The path check follows a member access through the includes.**
  `scripts/check_path_string.py` already read the headers a file includes
  directly; a member access such as `PathFromUtf8(mailDir_) / env.accountId`
  in UltraMail's preview still slipped through, because the struct
  (`UltraMailTypes.h`) is two includes away. A member access (`x.name`,
  `x->name`) is now looked up in every repository header the file includes,
  transitively (up to 400), a header that the include roots do not reach
  being found by its file name when exactly one header in the repository
  has it; a member name the headers declare two different ways is left
  alone. Bare names keep the direct-include rule. Each header's
  declarations and each include are read once per run, so a full run takes
  about as long as before (36 s against 34 s). The self-test gains a struct
  included through another header, and against the pre-fix UltraMail /
  EmailCleaner sources the check now reports all eight wrong operands.
  Five more sites are wrapped in `PathFromUtf8`: UltraWin's environment
  listing and prefix check, VirtualFS's cached-archive cleanup, UltraSocial's
  attachment check, and an AnchorPoint test.
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
    that case is the configure guard's. The self-test covers the export
    parsers of all three platforms.
