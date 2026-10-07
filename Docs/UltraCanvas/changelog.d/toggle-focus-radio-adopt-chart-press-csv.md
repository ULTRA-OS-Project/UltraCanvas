- **Checkboxes, radios and switches take the keyboard focus.**
  `UltraCanvasLabeledToggleBase` never overrode `AcceptsFocus()`, so Tab
  skipped every toggle and their Space handling and focus ring were never
  reached. They take the focus like buttons now: Tab reaches them, a press
  focuses them, and Space activates the focused one. Enter is no longer
  handled by a toggle, so in a dialog it still presses the default button,
  as in HTML and the native toolkits. `SetAcceptsFocus(false)` keeps a toggle
  out of the Tab order. Apps whose dialogs hold toggles gain Tab stops.
- **A radio added to a group already checked becomes its selection.**
  `UltraCanvasRadioGroup::AddRadioButton` ignored a radio's checked state, so
  one created with `checked = true` showed its dot while
  `GetSelectedButton()` returned null, and two such radios both stayed
  checked. The last checked radio added wins and the others are cleared, as
  in an HTML group; building the group does not call `onSelectionChanged`.
- **Charts leave a left press to the parent unless it starts a pan.** The
  chart base took every left press and release, so a chart in a scrolling or
  draggable container swallowed its clicks. It takes them only to start and
  end a pan of a zoomed chart; charts that react to clicks handle them
  first, as before.
- **A CSV row that cannot be read is skipped, not plotted at (0,0).**
  `ChartDataVector` and `ChartDataStream` turned a row after the first whose
  x or y is not a number into a point at the origin. Every line is now kept
  only when its x and y read as numbers - headers, blank lines and broken
  rows are skipped wherever they stand - and `ChartDataStream` numbers its
  points over the data rows, so its count and chunks agree.
- `IsFocused()` on an element whose window has no application (a test, a
  tool) called through a null pointer and crashed;
  `UltraCanvasWindowBase::IsWindowFocused()` is false then.
