- **Performance matrix, Phase 3: calibration and files.** The 9-box can now
  be used in a calibration meeting. People can be moved between boxes, and a
  matrix can be saved and opened again.
  - `SetAllowItemDrag(true)` lets a dot or a name be dragged to another box.
    It is off by default and never on while anonymised. `onItemMove` is asked
    first and can veto the move. A moved person gets the box as an explicit
    cell, and their scores stay in the data beside it. A press becomes a drag
    only after a few pixels, so `onItemClick` still fires for a click, on the
    release.
  - Keyboard: the arrows move the cell selection, Enter activates the
    selected person or box, and Escape steps back (drag, then person, then
    box). With dragging on, Ctrl+arrows move the selected person one box.
    `MoveItem` does the same from code.
  - A click on "+N" / "+N more" hands everyone in that box to
    `onOverflowClick`, so the application can list them all;
    `GetCellItems` gives the same list.
  - `ExportPerformanceMatrixJson` / `ImportPerformanceMatrixJson` write and
    read the whole model as `"format": "ultracanvas-performance-matrix"`.
    A file written by hand may name levels by id or label. A file that does
    not validate is refused, and the model is left untouched.
  - `ExportPerformanceItemsCsv` / `ImportPerformanceItemsCsv` carry the
    people alone, under an `id,label,group,row,column,x,y,note` header. The
    columns may come in any order and letter case. Scores are dot-decimal
    whatever the locale. An error names the line and leaves the people
    untouched.

  The demo's Performance Matrix page gains Open JSON / Save JSON and Import
  CSV / Export CSV buttons through `UltraCanvasFileLoader`, an *Allow moves*
  switch with a veto that holds the top box to its target, and an *In the
  box* list behind "+N more". Model and render tests cover the round trips,
  the reader's refusals, drag against click, the veto, the keys and the
  overflow list.
