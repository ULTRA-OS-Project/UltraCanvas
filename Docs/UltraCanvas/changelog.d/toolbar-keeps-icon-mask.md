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
- **A toolbar's re-styling keeps an icon size the host chose.** The same
  re-application set every button's icon to the appearance's size, so a host
  that sized its icons itself (UltraDesktop's 24 px bar icons) had them snap
  back at each theme refresh. A re-styling now moves only the buttons still
  at the toolbar's earlier size, which follow a new appearance as before;
  `ToolbarOverflowMenuTest` checks both.
