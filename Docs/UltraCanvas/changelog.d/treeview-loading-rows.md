- **Tree view: a row whose children are being fetched can say so.**
  `TreeNodeData::loading` makes a row slide a thin bar along its foot, from
  its icon to the right edge, in the tree's loading colour
  (`SetLoadingBarColor` / `GetLoadingBarColor`, the accent blue
  `Color(37, 99, 235)` by default) - the sign the filer widget gives a remote
  folder being fetched. A lazy-loading host keeps its placeholder child only
  for the expand button, hidden (`visible = false`), instead of showing a
  "..." row. A 40 ms timer keeps the bars moving while a loading row is drawn
  and stops with the first frame that draws none, and in the destructor
  (`UltraCanvasTreeView` now has one). UltraFiler's folder tree uses it for
  a remote drive's folders.
