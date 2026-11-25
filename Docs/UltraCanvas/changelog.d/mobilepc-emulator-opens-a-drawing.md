- **The MobilePC emulator test opens a drawing.** Besides starting
  UltraPaint and UltraFiler on the second display, it now has ArtCreator
  open an SVG the APK carries and fails unless the drawing was read with its
  shapes - the first check, on every run, that the Vector plug-in reads
  files on Android (`scripts/android-mobilepc-smoke.sh`).
