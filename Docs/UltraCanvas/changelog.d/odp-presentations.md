- **OpenDocument presentations (.odp) open - as text, like a PowerPoint
  deck.** `UltraCanvasPresentationOdp.h` (new) reads an Impress `.odp`, a
  template `.otp` and the flat `.fodp` into UltraCanvasPresentationPptx's
  model, so the same outline, preview and text extraction serve both kinds of
  deck: titles, subtitles, outline and text-box paragraphs with their list
  levels, bullets and numbering (from the list styles), bold / italic /
  underline through the style parents, groups and hyperlinked shapes, tables,
  pictures' parts and descriptions, speaker notes, hidden slides, and the
  slide size from the master's page layout. Slide furniture, prompting
  placeholders, embedded objects and page comments are left out; a
  password-protected deck is refused with a reason.
  - `UltraCanvasFileLoader::LoadTextDocument` / `OpenTextDocument` open one by
    its content, the Open dialog's "Documents" offer it, `ExtractText` reads
    it, the format inventory lists "OpenDocument Presentation", and
    UltraFiler names the type and previews the slides' text.
  - A file named like a deck (`.pptx` and kin, `.odp`, `.fodp`) that is no
    deck is refused by `LoadTextDocument` rather than read as Markdown text.
  - The format inventory lists `.ppsm` and `.potm` with PowerPoint, which its
    reader always took, so the Open dialog offers them; UltraFiler names them
    and the legacy `.ppt` "PowerPoint".
  - `Tests/PresentationOdpTest.cpp` reads the PowerPoint test deck as
    LibreOffice converts it (`Tests/fixtures/office-pptx-sample.odp`, made by
    `make-odp-fixtures.py`) and checks it slide by slide against the `.pptx`
    reader's view of the original, and a hand-written flat deck
    (`presentation-features.fodp`) for white space, lists, styles, groups,
    tables, pictures and what is left out.
