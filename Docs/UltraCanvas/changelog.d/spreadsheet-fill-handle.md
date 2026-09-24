- **Spreadsheet: the fill handle fills.** The small square at the corner of
  the selection was drawn but dragging it did nothing. Dragging it down, up,
  right or left now shows a dashed outline of the range and, on release,
  fills it from the selection: two or more numbers continue as a series
  (1, 2 → 3, 4, 5), a text ending in a number counts on ("Item 1" →
  "Item 2"), formulas are copied with their relative references shifted
  (`=C2*$D$1` → `=C3*$D$1`), and anything else is repeated, formatting
  included. The fill is one undo step, and every formula is recalculated so
  totals reading the new cells update.
  - `SpreadsheetSheet::AutoFill` was a plain copy that nothing called; it now
    implements the above. New: `UltraCanvasSpreadsheet::AutoFillSelection`
    and the free function `ShiftFormulaReferences`.
  - A header sort now recalculates every formula too, so formulas outside
    the sorted block that read it are up to date.
  - New `SpreadsheetAutoFillTest`.
