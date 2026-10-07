- **Windows: the system fonts are scanned in the background; a cold font
  cache no longer holds the start.** The generated `fonts.conf` names
  `C:\Windows\Fonts` and the user's font folder, and fontconfig read every
  font in them before the first text could be laid out. From its cache that
  is quick; with a cold cache - the first start on a computer, after Windows
  changed its fonts, after the cache was cleaned - it opens every file, and
  with Windows 11's fonts that takes many seconds in which no window could
  appear.
  - `SetupBundledFontconfig()` now writes two configs when the bundled fonts
    are present: `fonts-startup.conf` (the bundled fonts alone, which
    fontconfig starts with) and `fonts.conf` (the full set), and scans the
    full set on a thread of its own.
  - `AdoptSystemFontsWithin(waitMs)`, called by the Windows
    `LoadBundledFontsNative()` with 400 ms: a scan that ends within the wait
    - a warm cache does - is the current set from the start, exactly as
    before. A longer one: the application starts with the bundled fonts and
    Windows' two symbol fonts (`seguisym.ttf`, `seguiemj.ttf`, added by file:
    the symbols the UI draws, ● ↩ ✓ ▲ ▼, and emoji), and when the scan ends
    every window switches to the full set on the UI thread - fontconfig's
    current config, Pango's font map, the text caches
    (`RenderContextCairo::InvalidateAllFontMetricsCaches`) and a new layout of
    every element. Fonts registered at run time move with it.
  - `GetSystemFontScanStatus()` and `SetSystemFontsSwitchedHandler()` tell an
    application how it went, for a start-up report.
  - Other platforms are unchanged: Linux and the BSDs use the system's own
    fonts.conf, macOS CoreText.
