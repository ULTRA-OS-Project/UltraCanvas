- **The UltraNet plug-in folder comes from UltraNet.** EmailCleaner looked for
  `Plugins/UltraNet` beside its executable itself, with the same code as
  UltraMail. UltraNet now does that by default, so EmailCleaner keeps only its
  `EMAILCLEANER_PLUGIN_DIR` override, which it now reads as UTF-8.
