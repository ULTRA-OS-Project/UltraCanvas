- **Saving an attachment opens in the Downloads folder on Windows.** The save
  dialog's first folder looked for `Downloads` under `HOME`, which Windows does
  not set, and read it through the narrow `getenv`. So on Windows it fell back
  to `.` - the folder UltraMail was started from, which is System32 from the
  MSIX package. It now takes the Downloads folder Windows names, wherever it
  was moved or redirected (`GetWellKnownUserFolders`), and the home folder
  (`HOME`, or `USERPROFILE` on Windows) as UTF-8 when there is none.
- **The UltraNet plug-in folder comes from UltraNet.** UltraMail looked for
  `Plugins/UltraNet` beside its executable itself. UltraNet now does that by
  default, so UltraMail keeps only its `ULTRAMAIL_PLUGIN_DIR` override, which
  it now reads as UTF-8.
