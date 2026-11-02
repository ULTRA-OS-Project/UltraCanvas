- **"The system font" is the desktop's font now, on every platform.**
  `UltraCanvasApplication::GetSystemFontStyle()` and
  `GetDefaultMonospacedFontStyle()` - what an element with no font family
  of its own is drawn in - returned the literal Ubuntu 12 on Linux and on
  Windows, so a Windows desktop's Segoe UI and a GNOME desktop's Cantarell
  never reached a window. Windows reads the message font of the non-client
  metrics at 96 dpi (`SystemParametersInfoForDpi`, else the system dpi) and
  uses Consolas for code; Linux and the BSDs read GSettings
  `org.gnome.desktop.interface` (`font-name`, `monospace-font-name`),
  `~/.config/kdeglobals` (`font`, `fixed`; first on a KDE session) and the
  GTK `settings.ini`; macOS keeps its system font and now gives the system
  monospace (Menlo for SF Mono, which Pango cannot name) at the interface
  size instead of Ubuntu Mono 12. The parsers are the header-only
  `UltraCanvasSystemFont.h` (`ParseGtkFontName`, `ParseKdeFontEntry`,
  `ReadIniValue`), `SystemFontTest` holds them, and the bundled Ubuntu
  families stay the answer where nothing is set.
- **One detector for the person's language.** `UltraCanvasSystemLanguage.h`:
  `SystemLanguageTags()` gives the desktop's languages best first as BCP 47
  tags - the POSIX variables as gettext reads them, then the platform's
  own list (Windows' preferred UI languages and default locale, macOS's
  Language & Region, `navigator.languages` in a browser, the configuration
  on Android). The spell checker's `DetectPreferredLanguage`, which read the
  variables itself and had no platform answer except the Windows
  backend's, uses it; the localisation module of
  `UltraCanvasLocalisationProposal.md` will. `SystemLanguageTest` holds the
  rules.
- **The standard set of the localisation proposal is measured.**
  `scripts/survey_ui_strings.py` lists the UI strings the applications
  share and the framework's own; its findings are the proposal's new
  appendix, the draft contents of `Standard.json`.
