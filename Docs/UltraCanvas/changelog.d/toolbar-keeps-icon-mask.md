- **A toolbar's re-styling no longer turns coloured icons into black
  silhouettes.** `UltraCanvasToolbar` drew every new button's icon through its
  mask (a monochrome glyph, tinted), which is right for its own glyphs; a host
  that shows a picture in its own colours switches that off with
  `SetUseIconAsMask(false)`. But each re-application of the appearance -
  `SetAppearance`, a theme (`ApplyTheme`), `SetOrientation` - set the mask on
  every button again, so the icons became black shapes the first time
  anything re-styled the bar: UltraDesktop's pinned and running applications
  went black once the running group tightened its spacing for a full bar or
  the look was refreshed, while they were right at start-up. A re-styling now
  keeps each button's choice; only a button just added gets the mask.
  `ToolbarOverflowMenuTest` checks it.
