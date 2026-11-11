- **The look is the desktop's, and *View > Theme* is a choice for every
  application.** The dark and light looks follow the framework theme - the
  desktop's light or dark mode, and *Look* on UOS-Settings' Appearance page
  over it - instead of a `darkTheme` setting of UltraTexter's own: a switch
  of the desktop repaints every window at once. *View > Theme* writes the
  other look to `Appearance.conf`, so every UltraCanvas application takes
  it, UltraTexter's other windows with them. `--dark` starts this run in the
  framework's dark look whatever the desktop says. The `darkTheme` line of
  the settings file is no longer read or written.
