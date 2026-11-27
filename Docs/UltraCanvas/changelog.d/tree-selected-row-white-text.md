- **TreeView: a selected row reads on the blue.** A tree not following a theme
  (the DemoApp's category tree among them) drew a selected row's label in its
  normal black on the default blue selection. With no selection text colour
  set the label is now white, or black only on a selection too light for white
  (under 3:1); `SelectedRowTextColor()` returns the colour in use. On a
  selected row the selection text colour now also wins over a node's or a
  cell's own colour (Columns mode too), so the whole row stays readable.
