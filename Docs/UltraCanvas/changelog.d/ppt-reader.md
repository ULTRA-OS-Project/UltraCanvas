- **PowerPoint 97-2003 decks (.ppt, and .pps / .pot) are read**
  (`UltraCanvasPresentationPpt.h`: `ReadPptPresentation`,
  `ReadPptPresentationFromMemory`, `IsPptPresentationFile`, `IsPptExtension`)
  into the deck model the .pptx and .odp readers fill, so the slide editor,
  the show, the page views, PDF export and the outline work on them, and
  "Save as" writes .pptx or .odp. The reader follows the Current User stream
  and the UserEditAtom chain to the last edit, then reads the slide size,
  fonts, masters (their text styles, colour schemes and placeholders; a
  title master as a "title" layout), each slide's shapes - places in EMU,
  turns and mirroring, preset and freeform outlines, fills, gradients,
  outlines, pictures with their bytes and crop, placeholders, tables (a
  group marked as one) - and their text with bullets, numbering, levels,
  spacing, bold, italic, underline, size, scheme colours, typefaces and
  hyperlinks, inherited from the master where a run says nothing; notes,
  hidden slides, transitions and backgrounds. An encrypted deck is refused
  with a reason. Every length is checked; a damaged file never crashes it
  (`PptReaderFuzzSmoke`).
- `UltraCanvasFileLoader::LoadPresentation` reads any deck by its content -
  .pptx and kin, .odp and kin, .ppt and kin - and is what the page views
  (`UltraCanvasPresentationPages`), the file previews and UltraOffice read
  with. `LoadTextDocument`, `ExtractText`, `DetectOfficeDocumentKind` (a
  compound file with a "PowerPoint Document" stream, by its content too),
  the format inventory and the media viewer take a .ppt.
- The OfficeArt drawing layer of the .doc reader - shape properties, the
  picture store, PNG / JPEG / EMF / WMF pictures, freeforms - is shared with
  the .ppt reader (`Plugins/Documents/Word/UltraCanvasOfficeArt.h`), and
  knows about 150 Office shape types by their DrawingML preset (it knew 30),
  so a .doc, a .docx's VML shape and an .odt's `mso-spt` shape draw more of
  them as what they are.
