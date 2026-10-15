- **A shared card look for window sections, and a light colour picker.**
  UltraMail's panes and UltraPaint's side panel each kept their own copy of
  the same white-card style, and UltraFiler and UltraPaint each their own
  light colour-picker style beside the picker's dark default, so the copies
  could only drift apart. Both are now framework presets:
  - `GroupBoxVisualStyle::SectionCard()` - one section of a window as a white
    card with a hairline border, 8 px corners and a bold grey caption over a
    hairline, for the `Header` frame style (`UltraCanvasGroupBox.h`). Its
    colours are compile-time constants beside the other UI colours
    (`UltraCanvasCommonTypes.h`): `Colors::SectionPage`, the light page the
    cards sit on, and `Colors::SectionCardBackground`, `SectionCardBorder`,
    `SectionCardCaption`.
  - `Colors::ListRowHover` and `Colors::ListRowSelected` - the rows of a list
    on such a card: a soft grey under the pointer, a light tint of the accent
    blue behind the selected row (UltraMail's lists, UltraPaint's layer list).
  - `ColorPickerStyle::Light()` - a white surface, grey hairline fields, near-
    black text and a blue accent - and `ColorPickerStyle::Dark()`, which names
    the default (`UltraCanvasColorPicker.h`).

  UltraMail, UltraPaint, UltraFiler and ArtCreator use them (their
  changelogs: UltraMail 0.10.53, UltraPaint 0.2.22, UltraFiler 1.74.1,
  ArtCreator 0.7.1). Docs: `UltraCanvasGroupBoxExamples.md`,
  `UltraCanvasColorPicker.md`, `UltraCanvasListViewExamples.md`.
- **Two compiler warnings in `UltraCanvasApplication.cpp` are gone.**
  `DispatchEvent`'s event switch named four event types and let the other 40
  fall through silently (`-Wswitch`); it now says so with a `default`. And
  the bound check before `keyStates[virtualKey]` compared the enum with 256,
  which clang reads as always true (`UCKeys` stops at 128) - but the
  platforms make a `UCKeys` from any key code, so the check matters: it now
  compares the key code as an int.
