- **The desktop's settings folder is in the header too:
  `UltraCanvasDesktopSettingsFolder()`.** `desktop.json` (UltraDesktop,
  UOSSettings) and the clipboard history's key both named `<config>/ultraos`
  with a rule of their own, and on macOS both kept it under `~/.config`, while
  every other per-user folder of the framework there is in Application
  Support. `UltraCanvasSettingsFolder.h` names the folder once -
  `%APPDATA%\ultraos`, `~/Library/Application Support/ultraos`,
  `$XDG_CONFIG_HOME` or `~/.config/ultraos` - and on macOS moves the old
  folder into place on first use, settings and key together, so the history
  still opens.
