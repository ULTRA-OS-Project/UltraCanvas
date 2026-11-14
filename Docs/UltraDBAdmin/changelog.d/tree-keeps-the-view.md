- **The connection tree keeps its place when it is rebuilt.** Adding, editing
  or removing a connection lists the tree again, which threw the view back to
  the first row. It now stays where it was (the framework's
  `TreeRebuildScope`).
