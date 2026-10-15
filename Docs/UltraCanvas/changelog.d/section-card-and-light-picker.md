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
- **The framework builds without the warnings it had.** Event and key
  switches that handle a few cases and leave the rest to the code after them
  say so with a `default` - `UltraCanvasApplication::DispatchEvent`,
  `OnEvent` in the button, image element, text input and Markdown view, the
  key handling of the segmented control, the slider (both modes) and the
  Gource tree (`-Wswitch`); `FontStyle::ToFontDesc` names the `Normal` slant
  and weight it leaves as they are. `TooltipContent` builds its title, text,
  bullet and separator blocks through a helper instead of a braced
  `{type, text}` that names two of the block's six fields, so the
  `-Wmissing-field-initializers` warning no longer appears in every file that
  includes `UltraCanvasTooltipTypes.h`. No change in behaviour.
- **A key code outside the key table no longer reads past it.**
  `UltraCanvasApplicationBase::IsKeyPressed` indexed the 256-entry table with
  whatever key code it was given, and the platforms make a `UCKeys` out of any
  code (`static_cast<UCKeys>(vk)`); it now answers false for one outside the
  table. The table's size is one constant (`KeyStateCount`), which
  `DispatchEvent` and `GetPressedKeys` use as well - `DispatchEvent`'s own
  check compared the enum with 256, which clang read as always true, since
  `UCKeys` stops at 128.
