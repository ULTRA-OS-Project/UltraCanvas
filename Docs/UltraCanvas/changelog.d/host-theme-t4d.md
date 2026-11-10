- **The containers and pickers follow the theme (T4 of the host theme
  proposal, fourth part).** `FromTheme` on `ContainerStyle` (the
  scrollbars), `GroupBoxVisualStyle`, `BreadcrumbStyle`, `PaginationStyle`,
  `ColorPickerStyle` (without a base `Dark()` or `Light()` by the theme's
  flag), `ColorSwatchBarStyle`, `ColorPaletteManagerStyle` (its editor's
  picker with it) and `RichTextEditStyle` (the chrome and the inks; the
  document's fonts stay), and `ApplyTheme` on the container, group box,
  breadcrumb, pagination, colour picker, swatch bar, palette manager and
  rich text edit, with the constructor tail and the `SetStyle` rule; a
  filled frame, strip or bar takes the theme's surface and a transparent one
  stays transparent. `SetContainerStyle` is behaviour, not a look, so a
  derived element that turns its scrollbars off in its constructor still
  follows. Element roles with their fallbacks: `color-picker.marker` and
  `color-picker.marker-outline` (white and black), `rich-text.page` (the
  well, for paper of another colour). `UltraCanvasThemeTest` covers every
  mapping and the following by default.
