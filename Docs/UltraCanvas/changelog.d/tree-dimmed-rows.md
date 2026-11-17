- **A tree row can be dimmed.** `TreeNodeData::dimmed` draws a row's label
  in the tree's dimmed text colour (`SetDimmedTextColor`, the theme's muted
  ink, so it follows a light / dark switch) instead of the text colour - for
  an entry the tree normally leaves out and shows for now, such as a hidden
  folder. A `textColor` of the node's own and the selection text on a
  selected row still win. Documented in `UltraCanvasTreeViewExamples.md`
  (*Dimmed rows*).
