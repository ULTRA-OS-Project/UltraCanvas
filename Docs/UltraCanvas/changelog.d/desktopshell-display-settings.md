- **`UltraCanvasDesktopShell::OpenDisplaySettings` and
  `CanOpenDisplaySettings`** open the system's own display settings, what a
  desktop's "Display settings" does: `ms-settings:display` on Windows, System
  Settings > Displays on macOS, and on Linux (X11 and Wayland alike) the
  first display tool installed, the session's own desktop's first
  (`XDG_CURRENT_DESKTOP`): `gnome-control-center display`, `systemsettings`
  / `kcmshell6` / `kcmshell5 kcm_kscreen`, `xfce4-display-settings`,
  `cinnamon-settings display`, `mate-display-properties`,
  `lxqt-config-monitor`, `budgie-control-center display`, then `wdisplays`,
  `nwg-displays`, and outside Wayland `arandr` and `lxrandr`. UltraDesktop's
  desktop menu uses it.
