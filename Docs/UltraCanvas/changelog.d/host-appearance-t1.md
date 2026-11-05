- **The desktop's look, read from the host (T1 of the host theme proposal).**
  `UltraCanvasApplication::GetHostAppearance()` says whether the desktop is
  dark, asks for high contrast or less motion, what its accent is, its
  interface and monospace fonts and its text scale, with where it came
  from. Windows reads the registry (`AppsUseLightTheme`, `AccentColorMenu`,
  `TextScaleFactor`), the metrics and DWM; macOS reads AppKit
  (`effectiveAppearance`, `controlAccentColor`, the accessibility options);
  the other platforms answer light with their font detectors' fonts until
  T2. A switch to dark while an application runs (`WM_SETTINGCHANGE` and
  its kin, KVO on `effectiveAppearance`) is read once per burst and
  `AddHostAppearanceHandler` hears it; the start-up report names the host's
  look. `GetSystemFontStyle()` and `GetDefaultMonospacedFontStyle()` are the
  value's fonts, a pin still winning. `ULTRACANVAS_HOST_APPEARANCE`
  (`dark,font=Noto Sans 14,accent=#e95420,scale=1.25,contrast=high`) overrides
  any of it for a dark, light or 14 pt run on any desktop.
  `UltraCanvasHostAppearance.h` is header-only and pure; `HostAppearanceTest`
  covers it without a display. `Docs/UltraCanvas/UltraCanvasHostAppearance.md`.
