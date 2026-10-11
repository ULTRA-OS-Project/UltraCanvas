- **`UltraCanvasListView`: the hover follows the rows when they scroll under
  a still pointer.** A wheel scroll (or `EnsureRowVisible`) left the row
  highlight on the row index the pointer had been over, which had moved
  away; now the row under the pointer is worked out again and `onItemHovered`
  / `onCellHovered` are reported for it, so a preview a host hangs on them
  follows the scroll. The tooltip stays hidden after a wheel scroll as before,
  and comes back with the next move.
- **`UltraCanvasMediaSurface::SetFitEnlarges(false)`** (forwarded by
  `UltraCanvasMediaViewer`): fit-to-window shrinks only, so a picture smaller
  than the pane is shown at its own size, centred, instead of enlarged to
  fill it. For hover previews; the wheel still zooms in from there.
- **`ClipboardHistoryRowDelegate::IsOverThumbnail(posInCell)`**: whether a
  point of a row is on the thumbnail's column, for a host that opens a
  preview from it (UltraClipboard 0.1.3).
- **`UltraCanvasMediaViewer`: the info bar's zoom follows a resize.** It was
  written on load and on a zoom, so a viewer resized afterwards - or one that
  loaded its file while hidden, as a hover preview does, and read `0%` on
  its first showing - kept the stale figure until the next zoom. `Arrange`
  now refreshes it when the surface's size changes.
- **`UltraCanvasMediaViewer::SetInfoBarVisible(false)`** hides the info bar
  under the display surface, as `SetTopBarsVisible(false)` hides the bars
  above it: a host that wants nothing but the picture.
- **`ClipboardHistoryPolicy::previewStyle`** (`ClipboardPreviewStyle::Modern`
  or `Classic`, kept with the policy as `previewStyle`): how UltraClipboard
  previews an image whose thumbnail the pointer rests on.
