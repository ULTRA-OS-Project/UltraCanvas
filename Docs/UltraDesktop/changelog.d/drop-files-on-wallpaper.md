- **Files can be dropped on the wallpaper.** Where they go is chosen on the
  desktop's right-click menu, *Drop files*, or on UOS-Settings' Desktop page
  (`dropFilesOn` in `desktop.json`):
  - *Drop on sticker board* (the default): into the Sticker board's
    **Unsorted** category, added when the board has none, and the board
    shows.
  - *Drop on desktop*: moved into the Desktop folder (copied from another
    drive; a taken name becomes `name (2).ext`), and the wallpaper shows the
    Desktop folder as icons - the file's thumbnail or the system's icon over
    its name. A dropped file lies where it was dropped; a double click opens
    it, a drag moves it (kept in `desktopIcons`), a right click offers
    *Open*, *Show in the file manager*, *Delete* and *Arrange icons*. A file
    another program saves to the Desktop appears within two seconds.
- **Notes on the wallpaper show again.** The notes and their "+" button were
  placed on the notes layer but never added to it, so none was drawn while
  the notes were switched on.
- **Several desktop icons at once.** Ctrl+click adds or removes an icon,
  Shift+click adds one, and a box drawn on the wallpaper selects the icons it
  touches (with Ctrl, adds them). The selected icons open, move and are
  deleted together, and *Select all* is on their menu.
- **Desktop icons can be dragged out to other programs.** Dragged over
  another program's window, the selected files continue as a native file
  drag, which a file manager, a mail or an editor takes.
- **The panels follow the light theme.** The clock, loudspeaker, Wi-Fi,
  Bluetooth, device and clipboard panels and the window pictures are near
  white with dark text in the light theme.
- **The desktop's menu works on the wallpaper while desktop icons show.**
- **The desktop icons answer the keyboard**, after a click on an icon or
  the wallpaper: Delete (after the question), Ctrl+A, Enter, and Escape,
  which clears the selection or puts a drag or a rubber band back.
- **F2 renames a desktop icon** (also *Rename* on its menu): its name
  becomes a text field with the part before the extension chosen; Enter or a
  click elsewhere renames the file, which keeps its place, and Escape keeps
  the old name. The icons are a little wider, so a name like
  `groceries.txt` is no longer cut at both ends.
