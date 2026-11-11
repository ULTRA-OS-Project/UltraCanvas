- **The badge, card, chips, panels, sidebar, busy indicator, auto-complete,
  new-document dialog and reference point picker follow the theme (T4 of
  the host theme proposal, fifth part).** `FromTheme` on `BadgeStyle` (with
  a `primaryColor` for the Primary variant's fill), `CardStyle`, `ChipStyle`,
  `TagInputStyle`, `CollapsiblePanelStyle`, `SidebarStyle`,
  `BusyIndicatorStyle`, `AutoCompleteStyle`, `NewDocumentDialogStyle` and
  `ReferencePointPickerStyle`, and `ApplyTheme` on the ten elements (the
  auto-complete's field from the text input's reader, its list from its
  own), with the constructor tail and the `SetStyle` rule. `SpellCheckStyle`
  and `ClipboardRowStyle` gain a `FromTheme` for their hosts (the shared
  checker and the row delegate are not elements). Element role with its
  fallback: `busy-indicator.second-arc` (the orange). `UltraCanvasThemeTest`
  covers every mapping and the following by default.
