- **`UltraCanvasFileLoader` tells which kind of office file a file is.**
  `DetectOfficeDocumentKind(path)` answers `Document`, `Spreadsheet`,
  `Presentation` or `Unknown`, from the extension, else from the package
  inside (an OpenDocument `mimetype`, or `word/document.xml`,
  `xl/workbook.xml`, `ppt/presentation.xml`), so a file without its usual
  extension still opens in the right place. Two dialog helpers go with it:
  - `AddOfficeDocumentLoadTypes(opts)`: one *Office documents* filter with
    every kind, then *Documents*, *Spreadsheets* and *Presentations*;
  - `AddPresentationSaveTypes(opts)`: the decks the file loader writes,
    PowerPoint before OpenDocument, the default save type first.

  UltraOffice, the new office application, opens its files through them.
  Covered by `FileLoaderOfficeKindTest`.
