- **Legacy Excel workbooks (`.xls`) open.** `UltraCanvasSpreadsheet` reads
  Excel 97-2003 (BIFF8) and Excel 5.0/95 (BIFF5) files through a new reader,
  `UltraCanvasSpreadsheetXls.h` (`ReadXlsWorkbook` into a UI-free model), and
  `LoadFromFile` sends `.xls` to the new `LoadXLS`. What arrives: every
  worksheet (hidden ones stay hidden), typed values - dates, times,
  percentages and currency by their number format, the 1904 date system
  converted - text in any script (BIFF8 is UTF-16; BIFF5's code pages 1250,
  1251 and 1252 are decoded), merged cells, column widths, row heights,
  hidden rows and columns, defined names, and fonts, fills, borders and
  alignment. Formulas are translated into the engine's syntax - shared
  formulas expanded per cell, other sheets as `'Sheet'.A1`, names by name -
  and every formula cell keeps the result Excel cached; a formula is kept only
  when the engine can evaluate it (it parses, and its functions and names
  exist here), otherwise the cell holds Excel's value. An encrypted workbook is
  refused with a message saying it is password-protected. There is no `.xls`
  writer: `SaveToFile` says to save as `.xlsx` or `.ods`.
- **What else travels as `.xls` opens too, as Excel opens it.** An `.xlsx`
  renamed is read as `.xlsx`, delimited text (UTF-8, a code page, or Excel's
  UTF-16 "Unicode Text") as CSV, an HTML table - what web applications export,
  with Excel's own `x:num` values - as one sheet, and an Excel 2003 XML
  Spreadsheet with its styles and its R1C1 formulas turned into A1.
  `DetectXlsFileKind` says which a file is; binary data that is none of them
  is refused rather than read as text.
- **The file display previews `.xls`, and the media viewer shows `.xls` and
  `.xlsx`.** `UltraCanvasFilerWidget`'s preview page reads the first rows of
  an `.xls`'s first sheet as a cell grid, like `.xlsx` and `.ods` (it kept its
  type glyph), and `UltraCanvasMediaViewer` - the detail pane UltraFiler
  shows - now opens `.xls` and `.xlsx` in the spreadsheet: `.xlsx` had been
  left off its list although the spreadsheet loads it. The format inventory
  (`UltraCanvasSupportedFormats`) lists `.xls` as loaded, not saved.
- **`UCCompoundFileReader` (`UltraCanvasCompoundFile.h`)** reads OLE2
  compound files - the container of `.doc`, `.xls`, `.ppt` and `.msg`. It was
  the `.doc` importer's private reader; it now lives in the core for both
  readers, finds a stream in the root storage by walking the directory tree
  (so an embedded object's stream of the same name is not taken for the
  document's), compares names without regard to case, and decodes them as
  UTF-8.
- **Excel number formats are classified more carefully** (shared by the
  `.xlsx` and `.xls` loaders, now `ExcelNumberFormatCategory`): the bracketed
  parts of a format code are read for what they say rather than as letters, so
  `0.00;[Red]-0.00` is a number and not a date (the `d` of `Red`), a locale tag
  such as `[$-409]` no longer makes a date format currency, `[h]:mm:ss` is a
  time, and an escaped `\$` - how LibreOffice writes a dollar sign - is
  currency.
