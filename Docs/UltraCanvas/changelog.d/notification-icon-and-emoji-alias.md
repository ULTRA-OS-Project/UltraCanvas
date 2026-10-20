- **Windows notifications show the posting application's icon.** A
  notification on the bus is put on screen by the process that hosts the
  broker, and the windows-presenter gave every balloon that process's own
  icon: UltraMail's "New mail" toast came with UltraFiler's folder icon when
  UltraFiler hosted the broker. The balloon's picture is now the
  notification's icon file (a PNG, as UltraMail sends `media/appicon/UltraMail.png`,
  or an `.ico`); the host executable's icon is used only when the
  notification names no file. The toast's header line still names the host
  application: Windows takes it from the process that owns the
  notification-area icon, and a balloon cannot carry another identity.
- **An emoji goes to the colour emoji font.** Pango asks for the family
  `emoji` for a run of emoji, and the fonts.conf UltraCanvas writes where the
  system has none - always on Windows and Android - had no alias for it. Every
  font then matched it equally badly, and the emoji went to whichever font
  covering it fontconfig ranked first (on Linux, Unifont before Noto Color
  Emoji). The generated config now aliases `emoji` to Segoe UI Emoji and
  Segoe UI Symbol on Windows, Noto Color Emoji on Android, and Noto Color
  Emoji, Twemoji, JoyPixels and Symbola elsewhere.
