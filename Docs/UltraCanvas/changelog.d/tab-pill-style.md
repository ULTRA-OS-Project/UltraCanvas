- **`TabStyle::Pill` - a modern capsule tab for `UltraCanvasTabbedContainer`.**
  Every tab is a capsule floating in its slot of the tab bar: the open tab
  is filled with `activeTabColor` and outlined with the new
  `activeTabBorderColor` (lavender by default), an inactive one is filled
  with `inactiveTabColor` (transparent for a text-only tab) and outlined with
  `inactiveTabBorderColor`, a hovered one with `hoveredTabColor` /
  `hoveredTabBorderColor`. The page below gets a hairline in
  `tabContentBorderColor` on the bar's side instead of a frame.
  `SetPillInset()`, `SetPillBorderWidth()` and `SetPillCornerRadius()`
  (0 = capsule, otherwise a rounded chip) shape it; `GetPillBounds()` is the
  capsule drawn. `SetTabBarColor()`, `SetHoveredTabBackgroundColor()` and
  `SetActiveTabTextColor()` join the colour setters.
  `Tests/TabPillStyleScreenshotTest.cpp` checks the one-pixel outline on
  composited pixels and writes five colourways as a screenshot;
  `Docs/UltraCanvas/UltraCanvasTabExamples.md` *Pill Style* lists them.
