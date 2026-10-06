- **The layout engine stretches nothing unless the layout asks for it, and a
  container's stretch no longer overrides a child's own size.** Flex
  `align-items` defaulted to `Stretch`, and a grid item to `justify-self` /
  `align-self: Stretch` whatever its container said, so every child of a flex
  or grid container was stretched across it unless someone opted out - and a
  child with its own `width` or `height` was stretched over that too (a 200 px
  button in a column came out full width), although `UltraCanvasUIElement.h`
  and `Docs/CSSLayout.md` already promised the CSS rule. Now, in one place,
  for every application, the most specific statement wins:
  - the child's own `align-self` / `justify-self: Stretch` stretches it, also
    over a size it carries (CSS would keep the size; here a size often comes
    from a constructor that requires one, the request never does);
  - otherwise a child's set width (in a column, a grid cell's width) or
    height (in a row, a cell's height) is kept, at the start (CSS Flexbox 9.4
    step 11, CSS Box Alignment 6.1);
  - otherwise the container's `align-items` / `justify-items` decides, and
    those now start at `Start` - flex `FlexLayout::alignItems`, grid
    `GridLayout::justifyItems` / `alignItems` - with a grid item's own values
    at `Auto`, which takes the container's; the grid code never read the
    container's values before, so a grid's `SetGridAlignItems(Center)` takes
    effect now.
  - A negative width or height counts as no size (CSS rejects it; here it is
    the "not set" -1, or a size computed before the window had one).
  - A child that is not stretched is still measured against the container's
    width as an upper bound, so a wrapping label still wraps.
  - New: `Layout::SetGridJustifyItems`, and `ULTRACANVAS_LAYOUT_AUDIT=1`,
    which prints once per element every set size that outranked its
    container's stretch - the way to find a size given only because a
    constructor wanted one.
- **Every container that relied on the old default asks for it now**, so it
  looks as it did: 72 flex containers (the CSV import and export dialogs,
  the image export dialog, the e-book viewer; Texter's find, replace and
  go-to dialogs and toolbar; UltraFiler's find-text, RAM-disk and Windows-app
  dialogs; UltraAI's chat and settings dialogs; UltraMail's account bar and
  message preview; DemoApp pages) set `SetFlexAlignItems(Stretch)`, and 10
  grid containers (DemoApp's main window, gauges and diagram pages, Texter's
  file statistics, UltraAI's settings modes) set both grid stretches.
  `CreateFormGrid` asks for its horizontal stretch, so form controls still
  fill their column - except a control given a width of its own, which now
  keeps it (the image export dialog's 200 px file-name field, for one).
- Tests: `Apps/CSSLayoutTests` Phase 11 (default start, the container's
  stretch keeps a set size, the item's own stretch does not, a negative size,
  grid start / stretch / set size); the track-sizing cases ask for the
  stretch they read their tracks through; `Tests/CSSLayoutFormGridTest.cpp`
  adds a sized control.
