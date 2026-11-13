- **A text frame's words are edited in place.** A double-click on a
  floating text frame (or a drawing with words) opens its paragraphs where
  the frame stands, at its inner width over its fill and border; a click
  outside it or Escape goes back, and the words are written into the
  document's frame (`UltraCanvasRichTextEdit::EditFrame`, `IsEditingFrame`).
- **EMF and WMF pictures show.** Word 97's metafile pictures (their
  compressed records inflated), Writer's DOC hatches (kept as EMF textures)
  and EMF / WMF pictures in DOCX and ODT are drawn through the Vector
  plugin's readers, at twice their size; they used to be dropped or drawn
  as an empty box.
