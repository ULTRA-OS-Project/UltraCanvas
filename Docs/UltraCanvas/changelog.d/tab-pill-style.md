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
- **Dragging a tab to another place works on every tab position and past
  the visible range.** The swap compared the pointer's x with the target's
  horizontal centre whatever the `TabPosition`, so reordering on a `Left` or
  `Right` bar, where the tabs are stacked, hardly ever happened; it follows
  the bar's axis now, as does the drag-out threshold and the insertion line,
  which is drawn at last (it had no caller). A pointer held within
  `dragAutoScrollZone` (24px) of either end of a scrolled strip carries the
  tab one place in that direction every `dragAutoScrollIntervalMs` (250) and
  scrolls to keep it in view, so a tab reaches any place in one drag. The
  hovered tab, its close button and the right-clicked tab follow a reorder
  like the active tab did, instead of pointing at the wrong tab until the
  next mouse move. The drag ghost of a `TabStyle::Pill` tab is the capsule
  itself. `SetAllowTabReordering()` joins `SetAllowTabDragOut()`.
  `Tests/MenuAndTabBehaviourTest.cpp` drives the drag steps headless.
