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
- **`UltraCanvasImageElement::LoadFromPixmap`** shows a picture that is
  already drawn - a file's icon (`LoadHostFileIconPixmap`) or thumbnail
  (`RenderFileThumbnail`) - fitted to the box like a loaded image. UltraDesktop's
  desktop icons use it.
- **`UltraCanvasKanbanBoardElement::onDeleteKey`**: the application decides
  what the Delete key does to the selected card (returning true, the board
  removes nothing itself). UltraDesktop's Sticker board asks first and
  deletes the card's file or note.
