- **The input elements follow the theme (T4 of the host theme proposal,
  second part).** `FromTheme` on `CheckboxVisualStyle`, `RadioVisualStyle`
  (through a shared `LabeledToggleVisualStyle::FromTheme` for the label and
  the focus ring), `SegmentedControlStyle`, `SpinnerVisualStyle`,
  `StepperStyle`, `RatingStyle`, `CalendarStyle`, `DatePickerStyle` and
  `TimePickerStyle`, and `ApplyTheme` on the checkbox, radio, segmented
  control, spinner, stepper, rating, calendar view, date picker, date range
  picker, time picker and clock view: the roles mapped onto their colours
  and the interface font at the body size (a stepper's descriptions at the
  caption size) over the struct's own shape, so under the policy every one
  of them comes up light or dark with the desktop and keeps following. A
  colour no shared role fits is an element role with its fallback:
  `checkbox.mark` and `radio.dot` (the accent), `rating.on` / `rating.hover` /
  `rating.border` (the amber), `stepper.on-error` (white), `calendar.weekend`
  (red), `time-picker.clock-face` (the pressed fill). A style set by hand
  stops the following, as for the other readers. The time picker's
  `SetStyle` now reaches its field's editor and an open clock view.
  `UltraCanvasThemeTest` covers every mapping and the following by default.
