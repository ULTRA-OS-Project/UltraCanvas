- **A shared card look for window sections, and a light colour picker.**
  UltraMail's panes and UltraPaint's side panel each kept their own copy of
  the same white-card style, and UltraFiler and UltraPaint each their own
  light colour-picker style beside the picker's dark default, so the copies
  could only drift apart. Both are now framework presets:
  - `GroupBoxVisualStyle::SectionCard()` - one section of a window as a white
    card with a hairline border, 8 px corners and a bold grey caption over a
    hairline, for the `Header` frame style - and
    `GroupBoxVisualStyle::SectionCardPageColor()`, the light page the cards
    sit on (`UltraCanvasGroupBox.h`).
  - `ColorPickerStyle::Light()` - a white surface, grey hairline fields, near-
    black text and a blue accent - and `ColorPickerStyle::Dark()`, which names
    the default (`UltraCanvasColorPicker.h`).

  UltraMail, UltraPaint and UltraFiler use them (their changelogs: UltraMail
  0.10.53, UltraPaint 0.2.22, UltraFiler 1.74.1). Docs:
  `UltraCanvasGroupBoxExamples.md`, `UltraCanvasColorPicker.md`.
