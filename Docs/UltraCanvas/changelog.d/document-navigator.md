- **A word processor's navigator, in the framework.**
  `UltraCanvasDocumentNavigator` (new, `UltraCanvasDocumentNavigator.h`) is
  the side panel beside an `UltraCanvasRichTextEdit`: the headings as a tree
  by level, whose selected row is the heading the caret is under and follows
  it; the tables, pictures, charts, comments (with their authors; a
  double-click opens the list of comments), bookmarks, footnotes and
  endnotes and tracked changes; a click goes there and selects the picture,
  chart, comment or change. Its search box filters every list and lists the
  places the document says it. Buttons promote, demote and move a heading's
  section (the heading, its text and its subheadings) up or down, and a
  heading's row dragged onto another heading moves the section before it.
  The lists follow the edits once the typing pauses (`SetRefreshDelay`), and
  the tree is rebuilt only when the outline changed. UltraOffice and
  UltraTexter show it.
- **The outline, UI-free.** `BuildRichDocumentOutline`
  (`UltraCanvasRichDocumentOutline.h`, new) reads a `UCRichDocument` into a
  `RichDocumentOutline`: headings (level, words, parent, section end),
  tables (rows, columns, caption, first cell), pictures and charts (where
  they are, caption, alt text, title), comments (author, the range and words
  they are on), bookmarks, notes (mark, words, reference) and tracked changes
  (who, when, what); `HeadingAt` is the heading a block is under.
- **Heading sections, as undo steps.** `UCRichDocumentEditor::HeadingSectionEnd`,
  `SiblingHeading`, `MoveHeadingSection` (before any block, or to the end),
  `MoveHeadingSectionUp` / `Down` and `ChangeHeadingSectionLevel` (promote or
  demote a heading with every heading under it; a heading in its level's
  style takes the new level's), each one undo step; the same on
  `UltraCanvasRichTextEdit`, which also gains `GoToPosition`, `GoToRange` and
  `AddDocumentObserver` / `RemoveDocumentObserver` - any number of listeners
  to its edits and caret moves besides the host's own callbacks.

  Covered in `RichDocumentOutlineTest` (no display) and
  `DocumentNavigatorElementTest` (Xvfb).
