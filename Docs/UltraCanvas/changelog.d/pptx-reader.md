- **PowerPoint decks (.pptx) open - as text, and password-protected ones
  too.** `UltraCanvasPresentationPptx.h` (new) reads a deck (`.pptx`,
  `.pptm`, `.ppsx`, `.potx`) into a UI-free model: slides in order with their
  titles, the paragraphs of every placeholder and text box with outline levels,
  bullets and numbering, tables, pictures' descriptions and parts, speaker
  notes and hidden slides; slide furniture (date, footer, slide number) is left
  out. A protected deck decrypts through the same code as a protected `.xlsx` /
  `.docx`.
  - `PptxPresentationToRichDocument` makes an outline of it, and
    `UltraCanvasFileLoader::LoadTextDocument` / `OpenTextDocument` open a deck
    that way (asking for a protected one's password); the format inventory
    lists PowerPoint as readable, so the Open dialog offers it.
  - Not read: the layout as drawn, charts, SmartArt, embedded objects. No
    writer.
  - `OfficeEncryptionTest` reads a deck python-pptx wrote from its default
    template, plain and encrypted.
