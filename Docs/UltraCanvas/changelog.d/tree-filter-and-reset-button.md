- **A tree view can be filtered: `UltraCanvasTreeView::SetFilterText`.** It
  narrows the tree to the rows whose text contains the filter - ignoring case,
  in any script - with the rows above them, expanded so every match is on
  screen; a heading that matches keeps its entries. An empty text ends the
  filter and puts the tree back: every row, the expanded state it had before
  the filter began, and the selected row scrolled into view.
  `GetFilterMatchCount` says how many rows matched. The filter hides rows with
  its own flag (`TreeNode::hiddenByFilter`, `TreeNode::IsShown`), so a row the
  application hid through `TreeNodeData::visible` stays hidden, and "jump to
  first entry" goes to the first child on show (`TreeNode::FirstShownChild`).
  `TreeViewFilterTest` covers it.
- **The text input's clear button can carry a word: `SetClearButtonText`.**
  `SetClearButtonText("Reset")` makes the in-field clear button a small
  "Reset" button at the right end of the field instead of the cross - same
  behaviour, sized to the word (TextInput 1.12.0).
- **DemoApp: a filter field over the component tree.** Typing narrows the
  tree to the pages whose names contain the text, opened to them, and the
  status line says how many matched; "Reset" inside the field, or Escape,
  brings the whole tree back. The *Text Input* page shows the in-field
  buttons: the clear cross and the same button as "Reset".
