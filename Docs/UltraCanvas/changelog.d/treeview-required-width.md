- **TreeView: `GetRequiredWidth` - the width the widest row on show needs.**
  Indent, expander and check-flag slots, left icon and label text, plus the
  tree's right padding and border, a right icon and the vertical scrollbar
  while it is shown; the children of a collapsed node do not count. It
  measures with the window's render context and returns 0 while the tree is
  not in a window yet, so a host can size a sidebar to its names - UltraMail's
  folder list does (see the UltraMail changelog, "the folder list's width").
  Documented in `UltraCanvasTreeViewExamples.md`.
