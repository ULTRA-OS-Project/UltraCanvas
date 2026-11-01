- **UltraIM loads only a provider module built for it.** A module now
  exports the version it was built for (`ULTRAIM_DEFINE_PLUGIN_ABI_VERSION()`,
  `UltraIM_PluginAbiVersion`), and `UltraIM_RefreshProviders` skips one that
  answers another version or none - saying on stderr to rebuild it - before
  it calls anything in it; the module refuses a host of another version too.
  Until now a module accepted any host from version 1 on and the host asked
  nothing, so a stale `ultraim_*` module would have been called through a
  vtable that no longer matched. `ULTRAIM_PLUGIN_HOST_ABI_VERSION` is 2 and
  covers the provider interface and every type in `UltraIMTypes.h`, not only
  the host table; `scripts/check_ultraim_abi.py` (CI: `ultraim-abi.yml`)
  fails a change to those headers that leaves it as it was.
