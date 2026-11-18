- **UltraIM: provider modules on demand.** A provider module is no longer
  loaded until it is used: `UltraIM_LoadProvider(id)` loads the one module
  `ultraim_<id>` from `pluginDirectory`, `UltraIM_ListProviderModules()`
  names the modules there without loading them, and
  `UltraIM_LoadUsedProviders()` loads at start only the networks in use -
  those listed in `<dataDirectory>/ultraim-providers.txt`, which UltraIM
  keeps itself: a provider is added when it connects and taken out by
  `UltraIM_SignOut` (`UltraIM_GetUsedProviders()` reads it). With no list
  yet a first start begins an empty one, while a data folder used before
  has every module loaded once and the providers with a sign-in kept
  written down, so an update signs no one out. A module loaded before an
  `UltraIM_Shutdown` registers its provider again after the next
  `UltraIM_Initialize` (before, `UltraIM_RefreshProviders` skipped it). The
  test module is now `ultraim_moduletest`, named after its provider id.
  Tested in `UltraIMTests`.
