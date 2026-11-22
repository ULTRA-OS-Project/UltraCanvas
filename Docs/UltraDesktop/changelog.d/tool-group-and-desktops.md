- **UltraToDo on the tool group.** The right bar's tool group (Sticker board,
  notes, clipboard, screenshot) gains an UltraToDo button
  (`media/icons/desktop/todo.svg`) that opens the task list.
- **The tool applications stay off the taskbar.** The windows of
  UltraClipboard, UltraSnip and UltraToDo are no longer listed among the
  taskbar's running applications, since each has its button on the tool
  group; `toolAppsOnTaskbar` in `desktop.json` (UOS-Settings → Desktop →
  *Tool applications*, or *Tool applications on the taskbar too* on the
  wallpaper's right-click menu) lists them there as well.
- **The organiser showed one desktop when three were set.** The window
  manager's count won over the settings', and Windows' Task View starts with
  one desktop, so a setting of 3 showed a lone "1". The organiser now shows
  at least the settings' count; clicking a desktop the manager lacks asks it
  for more (on Windows through the framework's new
  `SetVirtualDesktopCount`).
- **Desktop 1 is the wallpaper's.** Clicking button 1 switches to desktop 1;
  clicking it while already there minimises that desktop's windows so the
  wallpaper shows.
