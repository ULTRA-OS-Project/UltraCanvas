- **Right click sets the background colour everywhere a picker offers it.**
  Choosing the background (line, secondary) colour with the right mouse
  button left the background swatch unchanged in three places.
  - `UltraCanvasColorSwatchBar` ignored every button but the left one. A
    right click now raises the new `onColorAdjustSelected(Color)` callback
    (the selection outline stays on the left-click colour); unset, right
    clicks are still ignored. Fast repeat clicks, which arrive as
    double-clicks, now select like single clicks.
  - A right-button drag in `UltraCanvasColorPicker` could end with the
    background swatch showing the foreground colour: a host that re-synced
    the foreground from `onBackgroundChanged` (ArtCreator does, from the
    selected shape) wrote it into the working state that was holding the
    background, and the release committed that. `SetColor` /
    `SetForegroundColor` during such a drag now set the saved foreground,
    and the release keeps the background the drag produced.
  - `SetBackgroundColor(c, notify = false)`: `notify` raises
    `onBackgroundChanging` / `onBackgroundChanged`. The built-in eyedropper's
    right-button sample and the swap arrow now report the new background
    through `onBackgroundChanged`; before, the swatch changed but the host
    never heard about it.
