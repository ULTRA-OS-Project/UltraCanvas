- **VideoFX text overlays no longer depend on the machine's fonts.** A text
  overlay without a `fontPath` now takes, in order: the font the application
  set with the new `VideoFX_SetDefaultFontPath()`; the framework's bundled
  Ubuntu font found next to the executable wherever UltraCanvas apps ship
  `media/` (`share/media/fonts`, `Resources/media/fonts`); a system sans font;
  and fontconfig's "Sans" only after a one-time check that this FFmpeg can
  load it. With none usable, the export fails before writing anything, with
  `NotAvailable` and a message naming the fix - on a minimal Linux install
  without DejaVu / Liberation / Noto it used to fail inside the filter graph.
  `VideoFX_GetDefaultFontPath()` reports the choice; `videofx` gains
  `--font`.
- **A text overlay's own `fontPath` was ignored.** It was checked for
  existence and then never passed to FFmpeg, so every title was drawn in the
  default font. It is now used, ahead of the default.
