- **UltraNet plug-ins are loaded `RTLD_LOCAL`.** `UltraNet_RefreshPlugins()`
  opened every plug-in DSO with `RTLD_GLOBAL`, which put everything a plug-in
  exports into the process-wide symbol scope, where it binds the symbols of
  every library loaded after it - another plug-in's included, and plug-ins
  built from the same helper sources export the same names. That scope was
  only ever needed by the retired v1 entry; plug-ins now take nothing from
  the host's symbol table (host table, ABI 2), so they are loaded
  `RTLD_LOCAL`. A plug-in still resolves its own references against the host
  first, so the host's `dynamic_cast` to the richer plug-in interfaces is
  unaffected (`imap_plugin_exposes_mailbox_interface`). New test
  `plugins_are_loaded_without_joining_the_global_symbol_scope` (POSIX): after
  loading, a process-wide `dlsym` finds no plug-in's `UltraNet_PluginInit`; it
  fails with `RTLD_GLOBAL`.
