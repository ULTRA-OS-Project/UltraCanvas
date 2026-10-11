- **When the fonts change, every window is drawn again, not only laid out.**
  On Windows the system fonts arrive after a cold start (the first start, or
  after Windows changed its fonts or the application moved), and until then
  only the bundled fonts and the symbol fonts are available. When the scan
  ended, `SwitchToSystemFonts` laid every window out again but repainted
  nothing: a frame draws only its dirty rectangles. A character the start's
  fonts lacked therefore stayed a box of hex digits until something else
  redrew that spot.
  - New `UltraCanvasWindowBase::RefreshAfterFontChange()`: every element
    measures its text again, and the whole window and each open popup is
    drawn again. The switch to the system fonts calls it on every window.
    So does `RegisterFontFile`, so text in a family registered at run time
    shows in it at once.
  - Windows: the start's symbol fonts are Segoe UI Symbol, Segoe UI Emoji
    and now also Cambria Math, for the mathematical symbols (⋮ ∩ ≔).
  - Windows: every thread's Pango font map reads fontconfig. The map
    `LoadBundledFontsNative` makes is only the main thread's default. Any
    other thread built Pango's Win32 map, which never reads fontconfig and
    draws a box for any character its font lacks. `PANGOCAIRO_BACKEND=fc`
    (the variable pangocairo reads, not `PANGO_BACKEND`) is set once the
    main thread's map exists. The debug log names the map.
  - Tested in the new `FontChangeRepaintTest`.
