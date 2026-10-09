- **Every format another program offers on the clipboard is seen (X11).**
  Xlib returns a format-32 property, the `TARGETS` list among them, as an
  array of C `long`s - 8 bytes each on a 64-bit system - and the X11
  clipboard copied it at 4 bytes an item, so `GetAvailableFormats()` and
  `IsFormatAvailable()` saw only the first half of the list. A program that
  offered its image or text type late in the list looked as if it offered
  nothing usable: UltraFiler's Paste stayed off, for one. The copy now uses
  the size Xlib hands back. `Tests/ClipboardTargetsTest.cpp` offers eight
  formats from a second X connection and checks all eight are seen.
