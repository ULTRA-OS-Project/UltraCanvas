- **The menu cursor shows on Windows, and has a clearer picture.**
  `UCMouseCursor::ContextMenu` - the pointer over a breadcrumb item's dropdown
  (UltraFiler's path bar, the media viewer), the breadcrumb's `...` item and
  the dropdown widget's button - never appeared on Windows: the backend handed
  `media/lib/cursor/context-menu.png` to `LoadCursorFromFileW` and
  `LoadImageW`, which read only `.cur`, `.ani` and `.ico`, and fell back to the
  arrow. `LoadCursorFromImageFile` now decodes any other image through
  `UCImage` and builds an alpha cursor from it, drawn at the window's DPI scale
  with the hotspot scaled along. The context-menu cursor is drawn from the new
  `context-menu.svg`, so it is sharp at 125 %, 150 % and 200 %. The same fix
  brings the colour picker's eyedropper cursor (`color-picker.png`) to
  Windows, where it was the arrow too.
  - New artwork: a pointer arrow with a small menu beside it, hotspot at the
    arrow tip. The old picture was a large page with a faint grey cross at
    its corner and no arrow, so it did not show where the pointer pointed.
    `context-menu.png` (Linux, macOS) is the SVG rendered at 32 x 32.
- **A breadcrumb dropdown opens from the full height of the breadcrumb.** The
  dropdown zone at the end of an item, and the whole `...` item, now reach
  from the top to the bottom of the element, padding and border included,
  instead of stopping at the item row. The menu cursor covers the same zone.
  `UltraCanvasBreadcrumb` 1.6.1.
