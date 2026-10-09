- **`UltraCanvasListView` measures what fitting a column takes.**
  `MeasureHeaderWidth(ctx, column)` is the narrowest width at which a
  column's header shows its whole title and, beside it, the sort triangle -
  reserved whether or not the column is the sorted one, so a fitted column
  keeps its width when the order changes, and a translated title is measured
  as it reads. `MeasureColumnTextWidth(ctx, column, font)` is the widest
  `DisplayRole` text of the column's rows in a font; each distinct text is
  measured once and remembered, so a column of thousands of dates costs a few
  hundred measurements. UltraMail's Date column is fitted with them
  (UltraMail 0.10.44). The header's insets and the triangle's strip are named
  constants now, shared by the painting and the measuring.
