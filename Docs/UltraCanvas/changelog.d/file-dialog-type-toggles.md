- **The file dialog can show its filters as toggle buttons.** With
  `FileDialogConfig::filterToggles` (or `FileDialogOptions::SetFilterToggles`
  through `UltraCanvasFileLoader`) the "Files of type" dropdown becomes a
  "Show:" row of toggle buttons, one per filter, labelled with the filter's
  name and with its extensions in the tooltip. Any number can be on, the
  listing shows the files any of them matches, and the last one on cannot be
  switched off. All start on except an "All files" (`*`) filter. It is for an
  Open dialog whose filters are kinds of file (images, audio, video ...) and
  would otherwise be a dropdown of endless extension lists. A native dialog
  has no toggles, so `UltraCanvasFileLoader` hands it the same filters as a
  list headed by "All supported files". The listing's filter now holds a copy
  of the filters in force, so a toggle change applies to a new predicate
  rather than editing one the listing may be running.
- **`UltraCanvasMediaViewer`'s Open dialog uses the toggles: Images, Audio,
  Video, Documents, Text, All files.** Its only filter had been a short list
  of picture formats, so in the framework dialog every video, document and
  text file the viewer opens was hidden. Each button now holds every
  extension of that kind the viewer opens, worked out at run time from the
  checks that decide what browsing a folder shows (a codec or reader a plugin
  registers is included) and sorted by the viewer's own `ClassifyFile`:
  vector drawings and 3D models are Images; PDFs, spreadsheets, e-books,
  `.ucd` and fonts are Documents; Text is plain text, markup and every
  programming language the syntax highlighter knows. A kind the build cannot
  show (no video backend) gets no button.
