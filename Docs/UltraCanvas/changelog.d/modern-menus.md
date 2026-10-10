- **Popup menus and submenus look the way current desktops draw them.**
  Windows 11 (WinUI's `MenuFlyout`), macOS and GNOME (libadwaita's popover
  menus) agree on a rounded panel lifted off the window by a soft shadow,
  padding inside it, rows whose highlight is a rounded rectangle inset from
  the panel's sides in a neutral shade, muted shortcuts and faint hairline
  separators; `MenuStyle::Default()` now draws exactly that, on Windows 11's
  mouse metrics: an 8 px panel radius, 4 px padding and inset, a 4 px
  highlight radius, 28 px rows, a translucent black hover that darkens any
  background, and a shadow fading over 18 px, 6 px down. A small menu - an
  attachment's Open / Save As - no longer reads as a square grey box with a
  full-width blue bar. `Dark()` and `Flat()` follow (`Dark()` with a light
  hairline outline); a menubar keeps its edge-to-edge items with a rounded
  highlight.
  - New `MenuStyle::itemInset` (gap between the border and a row's
    highlight) and `itemCornerRadius` (the highlight's rounding);
    `paddingTop` / `paddingBottom`, which a vertical menu ignored, are the
    panel's padding above the first row and below the last, and
    `borderRadius` rounds the panel (it was never drawn).
  - Separators run across the whole panel and are filled on one pixel row,
    so they stay sharp; they no longer take the hover highlight. Checkbox
    outlines use the muted secondary colour instead of the faint border.
  - A submenu opens with its first row level with the item it came from,
    scrolled or not, and a menubar's drop-down lines up with the item's
    highlight. An overflowing menu widens by its scrollbar instead of
    covering the rows' shortcuts.
- **Popups can have rounded corners and a drop shadow.** The window used to
  copy a popup's surface over its content, transparent pixels and all; it
  now blends it over with its alpha, so a popup's transparent corners show
  the window beneath (at any `SetPopupOpacity`), and paints the soft shadow a
  popup asks for through the new virtual
  `UltraCanvasUIElement::GetPopupShadow()` (a `PopupShadow`: colour, blur,
  offset, corner radius) beneath it, outside the popup's own surface - the
  popup keeps its bounds and the shadow takes no clicks. The menu answers it
  from its style; its old "shadow", a 1 px outline drawn inside its own
  surface, is gone. New
  `IRenderContext::CompositeToSurfaceWithOpacity()` (an OVER composite at an
  opacity) does the blending; `RenderContextTest` checks it, and the
  render-context page describes the four ways a surface goes onto another.
- `MenuAndTabBehaviourTest` checks the panel on a composited window: corners
  that show the window, a shadow outside the bounds that fades out and goes
  with the menu, a highlight inset from the panel's edges, top padding that
  hovers nothing, and a separator across the panel; its popup-opacity checks
  expect the blend. `TextMetricsScreenshotTest` measures the menu's first row
  where it now is.
