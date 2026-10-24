- **The palette gallery is the Palette manager, a window to drag about.**
  `UltraCanvasColorPaletteManager`'s popups - the gallery, the entry editor
  and the new palette creator - have a title bar ("Palette manager", "Colour
  3 of Basic", "New palette") that moves them about the window; the
  gallery's "Colour palettes" caption is gone into it.
- **Palettes of your own.** *Add palette* at the foot of the Palette manager
  opens the creator over it: a name, a picker, *Add colour* / *Remove* and
  the colours so far (a click on one lets the picker change it). *Create*
  keeps the palette under *My palettes* - the manager's first section, each
  with a button that deletes it - and adds it as a row. New API:
  `SetUserPalettes` / `GetUserPalettes` / `AddUserPalette` /
  `RemoveUserPalette` / `UniquePaletteName`, `OpenPaletteCreator`,
  `onUserPalettesChanged`.
- **A palette swatch under the pointer floods the rows**, and
  `onColorHovered` / `onColorHoverEnded` report it so the host floods its
  colour picker too - `UltraCanvasColorPicker::SetSurfacePreview` /
  `ClearSurfacePreview`, the same flood its own foreground and background
  swatches give. `UltraCanvasColorSwatchBar::onColorHovered` reports the
  entry under the pointer (the gap between two swatches keeps the last).
- **Full rows.** The presets of eight to twelve colours (Pastel, Earth, Skin
  tones, Sunset, Ocean, Forest, Neon) have sixteen and Rainbow 32, so each
  fills its row under a picker and in the gallery; one saved with its old,
  unchanged colours comes back whole (`ParsePalettes`). A 256-colour palette
  is eight full lines of 32: a wrapping swatch bar takes a line or two more
  when that leaves no short last line. Game Boy, PICO-8 and VGA 16 are no
  longer offered.
