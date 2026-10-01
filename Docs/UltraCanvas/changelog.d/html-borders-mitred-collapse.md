- **Borders: inline images per side, mitred corners, collapsed tables.**
  - An image in running text draws each border side on its own (width, colour,
    dashed / dotted), like a block image: `LabelInlineImageFrame` has `borderTop` ...
    `borderLeft` (`LabelInlineImageBorder`) and `SetBorders(width, colour)` in place of
    `borderWidth` / `borderColor`.
  - `DrawRoundedRectangleWidthBorders` (Cairo) fills each solid side as its wedge of
    the border ring - from the outer corners to the inner ones - so two sides meet on
    the corner's diagonal, each in its own colour, as in CSS; rounded corners are
    shared the same way and a border one colour all round is filled in one piece.
    Before, straight strokes overlapped at the corners and corner arcs took a blend of
    the two colours. Dashed and dotted sides are still strokes.
  - `border-collapse: collapse`: two cells sharing an edge draw it once - the wider of
    the two borders, kept by the cell to the left or above - and cells leave an outer
    edge the table draws itself to the table. `<table border>` rules take part.
    `ElementBuilder::ApplyBorders` applies a style's sides (ApplyBoxStyle calls it).
