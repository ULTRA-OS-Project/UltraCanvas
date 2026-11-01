- **UltraIM loads only a provider module built for it.** A module now
  exports the version it was built for (`ULTRAIM_DEFINE_PLUGIN_ABI_VERSION()`,
  `UltraIM_PluginAbiVersion`), and `UltraIM_RefreshProviders` skips one that
  answers another version or none - saying on stderr to rebuild it - before
  it calls anything in it; the module refuses a host of another version too.
  Until now a module accepted any host from version 1 on and the host asked
  nothing, so a stale `ultraim_*` module would have been called through a
  vtable that no longer matched. `ULTRAIM_PLUGIN_HOST_ABI_VERSION` is 2 and
  covers the provider interface and every type in `UltraIMTypes.h`, not only
  the host table; `scripts/check_plugin_abi.py` (CI: `plugin-abi.yml`)
  fails a change to those headers that leaves it as it was.
- **An element plug-in built for a newer host table refuses to load.** The
  handshake in `ULTRACANVAS_DEFINE_ELEMENT_PLUGIN` asked the host for table
  version 1 whatever the plugin was built for, so a plugin built against a
  longer `UltraCanvasPluginHost` would have read members past the end of an
  older host's table. It now asks for at least its own version
  (`ULTRACANVAS_PLUGIN_BUILT_FOR_ABI`, the build's
  `ULTRACANVAS_PLUGIN_HOST_ABI_VERSION`; the table stays append-only, so an
  older plugin still loads in a newer host) and says why on the log. The
  same check covers this header: a change to it moves the table's version
  or the `ucN` of `ULTRACANVAS_FRAMEWORK_ABI_TAG`. ElementPluginTest loads a
  plugin built one version ahead and sees it refused.
