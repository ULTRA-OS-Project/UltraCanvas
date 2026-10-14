- **The settings root is in the header: `UltraCanvasSettingsFolderRoot()`.**
  `UltraCanvasSettingsFolder()` names the shared `UltraCanvas` folder, but an
  application with a folder of its own name there (UltraTexter, UltraAI)
  rebuilt the root above it by hand, each from narrow `getenv` reads that miss
  a Windows profile named outside the ANSI code page. The root is now a
  header-only, pure `SettingsFolderRootFor(platform, ...)` with
  `UltraCanvasSettingsFolderRoot()` for the live environment - usable without
  linking the core, like `UltraCanvasDataFolder.h` - and the shared folder
  joins onto it. UltraTexter's config and autosave folders and UltraAI's
  endpoint settings resolve through the framework now, in the same places as
  before. `Tests/DataFolderTest.cpp` covers the rule.
- **`$XDG_DATA_HOME` names the clipboard history's and the UltraMessage
  journal's folder on every platform.** Both honoured it on Linux only, while
  every application honours it everywhere; a portable desktop setup can now
  point them elsewhere too. Without it nothing moves: `%LOCALAPPDATA%` on
  Windows (machine-local stores, on purpose not the applications' roaming
  `%APPDATA%` - `UltraCanvasDataFolder.h` says so), Application Support on
  macOS, `~/.local/share` elsewhere, under the folder names they always had.
