- **The Filer's move / copy question is redrawn, in two styles.** The
  question a drop or a Cut + Paste asks (`SetDropConfirmation`) put its two
  paths in code spans - red on most themes, which read as an error - wrapped
  the destination in the middle of a folder name, and repeated the same long
  prefix in *From* and *Into*, so the one folder that differed was hard to
  find. `UltraCanvasFilerWidget::SetTransferDialogStyle(FilerTransferDialogStyle)`
  chooses between `Summary` (the default: the entry's icon beside the
  question, its type and size, then *From* and *Into* as breadcrumb rows with
  the last folder bold and the middle of a long path elided) and `Cards`
  (source and destination as two cards with an arrow between them, the verb
  that was not asked for set apart as *Copy instead* / *Move instead*).
  `UltraCanvasModalDialog::SetMessageVisible()` / `IsMessageVisible()` are
  new for it: a dialog whose elements present the message hides the message
  area and still answers `GetMessage()` and `GetDetails()` with the text.
  `Tests/FilerPaneDragTest.cpp` asks the question in both styles.
