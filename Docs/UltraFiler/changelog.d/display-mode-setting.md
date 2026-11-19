- **Light or dark, whatever the desktop says.** *Settings > Display > Display
  mode* chooses UltraFiler's look: **As the system says** (the default, and
  what every earlier release did) follows the desktop and UOS-Settings' Look
  page, live; **Light mode** and **Dark mode** keep UltraFiler in that look.
  The desktop's accent colour, fonts and high contrast still apply. The main
  window switches at once, and the choice is kept in `config.ini` as
  `display.mode`. The settings are now read before the window is built, so a
  filer set to Dark starts dark rather than switching after it appears.
