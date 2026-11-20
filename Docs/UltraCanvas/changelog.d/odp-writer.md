- **OpenDocument presentation writer.** `WriteOdpPresentation`
  (`UltraCanvasPresentationOdp.h`) writes a deck - read from an `.odp` or a
  `.pptx`, or laid out from a document - as an Impress deck LibreOffice
  opens: presentation frames, custom shapes turned and mirrored as PowerPoint
  mirrors them, fills and gradients, dashes and arrowheads, connectors,
  pictures, tables with merged and styled cells, charts as chart objects,
  bullets and numbering as list styles, the runs' fonts and effects, speaker
  notes, hidden slides and backgrounds. `.odp`, `.otp` and the flat `.fodp`.
  `SaveTextDocument` and `SavePresentation` write it (a password is refused,
  not saved unprotected), and the Save dialog's document types offer it.
- **The `.odp` reader reads charts** - a package's chart objects and a flat
  file's inline ones.
- **`UltraCanvasOdfChart.h`**: the OpenDocument chart object writer and reader
  moved out of the `.odt` plugin into core, shared with the `.odp` reader and
  writer.
