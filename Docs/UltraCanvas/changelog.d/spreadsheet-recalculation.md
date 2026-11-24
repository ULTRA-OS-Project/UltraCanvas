- **Spreadsheet formulas did not follow their inputs.** `Recalculate` only
  calculated formulas that were already marked dirty. The engine kept a list
  of the changed cells and their dependents but never read it, so changing a
  cell left every formula reading it at its old result until something
  called `RecalculateAll`. With `C2 = B2*2`, setting B2 from 4 to 5 left C2
  at 8, and a formula entered before its input stayed at 0.
  - `Recalculate` now starts from what changed and follows every formula
    that reads it, through chains, ranges and other sheets. It works from the
    references each formula holds, so formulas from a file, a paste, a fill
    or an undo are followed as well as those typed in. Formulas that name a
    range, and volatile ones (NOW, TODAY, RAND, RANDBETWEEN), are always
    recalculated.
  - The grid tells the engine about every change the sheet reports: a cell,
    a range, or rows and columns (which recalculate everything).
  - Pasting recalculates (it did not at all), and sorting marks the sorted
    block. A change nobody recalculated is calculated before the grid draws.
  - New engine calls: `MarkRangeDirty`, `MarkAllDirty`, `HasPendingChanges`;
    a formula's `GetFunctionNames` and `ReadsNamedRanges`.
  - New test: `SpreadsheetRecalcTest`.
