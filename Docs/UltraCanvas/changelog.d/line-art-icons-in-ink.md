- **TreeView, FilerWidget: one-colour line icons in the theme's ink.** A
  black outline icon with no colours of its own (a cloud, a server) stayed
  black on a dark theme. `TreeNodeIcon::drawnInTextColor` draws a tree row's
  icon as a mask in the row's text colour (the selection text colour on the
  selected row), and `UltraCanvasFilerWidget::folderIconIsLineArt` does the
  same for an icon `folderIconProvider` returned, in `FilerStyle::textColor`.
  Both are off by default: a coloured icon keeps its colours.
