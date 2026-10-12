- **`UltraCanvasSettingsFolder()` reads `HOME` and `XDG_CONFIG_HOME` through
  `GetEnvUtf8`**, as it already read `APPDATA`, instead of the narrow
  `getenv`. Nothing changes on macOS and Linux, where the environment is
  bytes already; the file now reads one way on every platform and leaves no
  narrow read for the UTF-8 path check to tell apart from a Windows one.
