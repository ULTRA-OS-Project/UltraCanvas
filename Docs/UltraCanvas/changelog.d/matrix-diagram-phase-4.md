- **Matrix diagram, Phase 4.** Pan and zoom through the shared
  `UltraCanvasDiagramViewport`, opt-in with `SetPanZoomEnabled(true)`: the wheel
  zooms about the pointer, dragging empty space or the middle button pans,
  Ctrl +/-/0 zoom and reset, and the viewport's zoom buttons and minimap are
  available through `Viewport()`. Only the matrix moves; titles, the form header
  and the legend stay put, and nothing outside the matrix area can be hit.
- Matrix diagram: keyboard navigation. The diagram takes the focus when
  clicked; the arrow keys move the selected cell in the direction they point on
  screen - on turned X quadrants, Y rhombi and the House roof too - Tab moves
  between panels, Escape clears, and the selected cell is outlined while
  focused. New `SelectCell()` and `onSelectionChanged`.
- Matrix diagram: editing, opt-in with `SetEditable(true)`. A click, Space or
  Enter steps a cell through its panel's scale and back to empty, Shift steps
  back, Delete clears; `onCellEdited` reports each change and `CycleCell()`
  does it from code.
- Matrix diagram: JSON for the whole model - `ExportMatrixJson` /
  `ImportMatrixJson`, and `MatrixModelToJson` / `MatrixModelFromJson` on a
  `JSONValue`. Cells are keyed by item id, so a file survives reordered items;
  the import refuses another format, a newer version, unknown items or levels
  and an undrawable model, and leaves the model untouched when it does.
- DemoApp: Pan & zoom and Editable switches on the Matrix Diagram page, and Open
  JSON... / Save JSON... beside Import table....
