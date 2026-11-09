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
