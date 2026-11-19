- **Light or dark, whatever the desktop says.** *Settings > Display > Display
  mode* chooses UltraFiler's look: **As the system says** (the default, and
  what every earlier release did) follows the desktop and UOS-Settings' Look
  page, live; **Light mode** and **Dark mode** keep UltraFiler in that look.
  The desktop's accent colour, fonts and high contrast still apply. The main
  window switches at once, and the choice is kept in `config.ini` as
  `display.mode`. The settings are now read before the window is built, so a
  filer set to Dark starts dark rather than switching after it appears.
- **The settings window follows a theme change while it is open.** It kept
  the look it opened with; now it switches at once with the main window -
  from its own Display mode page as well as from the desktop - its switches
  included.
