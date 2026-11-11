- **The window follows the desktop's look.** Light or dark as the desktop
  is - or as *Look* on UOS-Settings' Appearance page says - in the desktop's
  font and size, and a change while UltraFiler runs is taken at once. Every
  surface the window, its dialogs and its tab strips painted in a colour of
  their own (the tool rows and buttons, the status strip, the folder tree,
  the computer page, the search field, the New folder split button, the
  Scan sub folder button, the connection log badge) is a role of the
  framework theme now (`UltraFilerLook.h`); the file displays, dropdowns,
  menus and tree follow the theme by themselves, so a dark desktop gives a
  dark filer throughout. The window's text size is the theme's body size
  (the desktop's interface font size) rather than a fixed 9 pt. A drive row
  of the folder tree keeps its chosen background and picks its text colour
  by it, so it reads in both looks.
