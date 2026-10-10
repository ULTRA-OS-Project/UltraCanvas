- **`TabStyle::Pill` - a modern capsule tab for `UltraCanvasTabbedContainer`.**
  Every tab is a capsule floating in its slot of the tab bar: the open tab
  is filled with `activeTabColor` and outlined with the new
  `activeTabBorderColor` (a mid blue by default), an inactive one is filled
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
- **Tab titles, close buttons.** A truncated tab title now runs up to the
  close button: `GetTruncatedTabText` stopped 20px short of the width it was
  given, leaving a gap before the X in every style, and it cut UTF-8 titles
  byte by byte; it takes whole characters off now. The close X has one
  weight on every tab (`closeButtonStrokeWidth`, default 1px) instead of 2px
  on inactive tabs and 1px on the open one, and the open tab's X can have a
  colour of its own (`activeTabCloseButtonColor`), so a solid accent pill
  shows a pale X while the other tabs keep a grey one.
  `SetCloseButtonColor()`, `SetCloseButtonHoverColor()` and the two new
  setters join the colour setters.
