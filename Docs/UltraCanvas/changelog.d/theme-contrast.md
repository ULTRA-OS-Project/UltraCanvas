- **`UltraCanvasTheme` 1.2.0: the presets' readability, measured.** `Light`'s
  `lineStrong` is a mid grey (128) instead of `ButtonShadow` (160): a control
  border drawn by an element that follows the theme now reaches 3:1 on
  `panel` and `raised` (it was 2.6:1 on white); hand-styled elements keep
  their default. `Dark`'s greys hold the same promises as they were. The
  promises - ink 4.5:1 on every surface, muted ink 4.5:1 down to `hover`,
  faint ink 4.5:1 on `panel`, `well` and `raised`, `lineStrong` 3:1 on
  `panel` and `raised`, `focus` 3:1 everywhere, text on the accent 4.5:1 -
  are in `UltraCanvasTheme.md` and `UltraCanvasThemeTest` measures them.
