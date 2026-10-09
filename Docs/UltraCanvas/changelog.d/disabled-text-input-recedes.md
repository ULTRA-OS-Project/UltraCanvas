- **A disabled `UltraCanvasTextInput` now looks disabled.** It drew exactly as
  an enabled one - white face, the normal border, black text - so a field
  that could not be typed into gave no sign of it (UltraClaude's locked
  folder field looked editable). `TextInputStyle` gains
  `disabledBackgroundColor`, `disabledBorderColor` and `disabledTextColor`,
  defaulting to the framework's `Colors::ControlDisabled`,
  `ControlDisabledBorder` and `TextDisabled`, and a disabled field draws with
  them and hides its clear button. The `Outlined()` and `Underlined()`
  presets keep their transparent face. Documented in
  `UltraCanvasTextInputExamples.md`.
