- **The UltraNet loader no longer accepts the v1 plug-in entry.** A plug-in
  DSO that exported only `UltraNet_PluginRegister()` used to be loaded as a
  fallback. That entry registered itself by resolving `UltraNet_RegisterPlugin`
  - and every other core function it called - from the host's symbol table at
  load time, which only POSIX allows and which only worked when the host
  happened to carry all of them. Every in-tree plug-in has used
  `UltraNet_PluginInit(host)` and the host table (ABI 2) since the previous
  release, so `UltraNet_RefreshPlugins()` now loads that entry alone, the same
  way on every platform, and leaves a v1-only library unregistered. A
  third-party plug-in still exporting only v1 has to be rebuilt with
  `UltraNet_PluginInit` and `Plugins/UltraNet/common/UltraNetPluginHostShim.cpp`.
  New test `plugin_loader_refuses_a_v1_only_plugin` (POSIX) builds such a
  plug-in and checks it is not registered; against the previous loader it
  fails.
