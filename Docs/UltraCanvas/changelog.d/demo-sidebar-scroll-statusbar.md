- **UltraCanvasTextLayoutCache: a no-wrap text stays one line again.** A
  `TextLayoutKey` that named no ellipsize applied `EllipsizeNone` over the
  end-cut `SetWrap(WrapNone)` had chosen, so a `WrapNone` label measured
  against a narrow width wrapped a character a line instead of being cut.
  `TextLayoutKey::ellipsize` is a `std::optional` now: unset leaves the wrap
  mode's choice; the button and the menu still name their own.
  `TextLayoutCacheTest` covers it.
- **DemoApp: the tree fills the sidebar, the status bar is gone, and the
  page area scrolls both ways.** The status legend under the tree was
  measured with its title wrapped a character a line (the cache fix above)
  and took ~420 px of a medium-size window from the tree. The status line at
  the bottom of the window has been removed with all its code. A page built
  at a fixed width (the SVG gallery's 1000 px) was cut down to the area and
  its right-hand column cut off; it keeps its width as its min-width now, and
  the area shows a horizontal scrollbar when the window is narrower than the
  page.
