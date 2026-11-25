- **A host's own entries after "Open with" in the file display's context
  menu.** `UltraCanvasFilerWidget::openMenuProvider` is asked every time the
  menu of a file or a selection opens, with the entries it acts on, and its
  items go right after *Open with* - so an application offers an entry only
  where it applies. UltraFiler adds *Edit in UltraPaint* for pictures and *Edit in ArtCreator*
  for drawings there.
