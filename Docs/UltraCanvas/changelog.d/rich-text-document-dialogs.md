- **The document dialogs of a word processor, in the framework.**
  `UltraCanvasRichTextEditDialogs.h` puts eleven dialogs over an
  `UltraCanvasRichTextEdit`, each a form (`UltraCanvasFormLayout`) of the
  framework's elements in an `UltraCanvasModalDialog`: page setup (paper,
  orientation, size, margins, header and footer distance), paragraph
  (indents, space before and after, line spacing, alignment, tab stops),
  named styles (apply, change, new from the selection), table properties,
  a picture's alt text, wrapping, position and size, table of contents,
  caption, cross-reference, footnote or endnote, a comment, and the list of
  a document's comments (go to, edit, resolve or reopen, delete). Each is
  `Read...DialogValues`, `Apply...DialogValues` - what OK does, testable
  without a window (`RichTextDialogsTest`) - and `Show...Dialog`; Cancel
  changes nothing. UltraOffice and UltraTexter both open them.
- **What they set, as undo steps.** `SetParagraphFormat`
  (`RichParagraphFormat`: a paragraph's whole layout, or a cell's alignment
  and line spacing), `SetPageSetup` (the page is now part of an undo step,
  and an undo that changes it lays the document out again),
  `SetTableProperties` (`RichTableProperties`: width, alignment, indent,
  header row, fitting the contents, every cell's lines, padding and vertical
  alignment), `SetSelectedImageWrap` / the core's `SetImageWrap` (a picture
  of its own paragraph that is made to float becomes the picture in a
  paragraph's line, at the same position), and `InsertNote(kind, text)`, a
  note with its text. `GoToPreviousChange`, `GoToNextComment`,
  `GoToPreviousComment` and `GoToComment` select a change or a comment's
  text. `RichPageSetup` and `RichTabStop` compare with `==`.
