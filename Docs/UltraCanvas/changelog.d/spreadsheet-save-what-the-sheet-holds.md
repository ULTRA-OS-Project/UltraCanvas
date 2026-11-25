- **A spreadsheet saved as .xlsx or .ods lost everything but its cells,
  sizes, styles and merges.** A workbook opened and saved again in
  UltraOffice came back with:
  - no frozen rows or columns;
  - its hidden rows and columns shown;
  - no named ranges - neither the workbook's nor those scoped to a sheet;
  - no auto-filter, and the rows it had filtered out simply hidden;
  - no print area and the default print settings: orientation, paper size,
    margins, scale or fit to pages, gridlines and headings, centring, header
    and footer, the rows and columns repeated on every page, and the manual
    page breaks.

  Both formats now read and write all of it. In .xlsx: the frozen pane
  (`sheetViews`), `hidden` on rows and columns, `definedNames` (a sheet's
  names with its `localSheetId`), `autoFilter` with its value filters and the
  `_xlnm._FilterDatabase` name Excel writes, `_xlnm.Print_Area`,
  `_xlnm.Print_Titles`, `printOptions`, `pageMargins`, `pageSetup` (with
  `fitToPage`), `headerFooter` and `rowBreaks` / `colBreaks`, each in the
  order the schema gives. In .ods: the split in `settings.xml`,
  `table:visibility`, `table:named-expressions` of the document and of each
  table, a `table:database-range` with filter buttons and its "="
  conditions, `table:print-ranges`, header rows and columns, page breaks
  (`fo:break-before`), and a page layout and master page per sheet in
  `styles.xml` with the header and footer.
- **`PrintSettings` holds the rows and columns printed on every page**
  (`repeatRowsFirst` / `repeatRowsLast`, `repeatColumnsFirst` /
  `repeatColumnsLast`, -1 for none; `HasRepeatRows`, `HasRepeatColumns`),
  which both formats carry and the model had no place for, and says whether a
  print area is set (`HasPrintArea`). Header and footer texts are kept in
  Excel's codes (`&P`, `&N`, `&A` ...), which .ods writes as text fields.
- **An .ods merge whose first cell was empty was dropped on load, and a
  covered cell (`table:covered-table-cell`, which LibreOffice writes inside
  every merge) was not counted,** so every cell after it in the row landed
  one column too far left. The writer now writes the covered cells too, and
  rows and columns inside `table:table-header-rows`, `-columns` and row or
  column groups are read where they were skipped.
- **LibreOffice opened every formula of a saved .ods as an error.** The
  writer put the `of:` prefix on each `table:formula` but never declared its
  namespace, so LibreOffice could not tell the formula's syntax. It is
  declared now; `SpreadsheetWorkbookRoundTripTest` checks the formulas'
  values after LibreOffice's conversion.
- The .xlsx and .ods writers wrote cell numbers through the stream's locale
  (`1.234,5` under a host that set a comma-decimal global locale); the
  worksheet and content streams are dot-decimal now.
- `UltraCanvasSpreadsheet::GetNamedRangeList` lists every name with its
  scope; `SpreadsheetSheet::GetColumnDefinitions` / `GetRowDefinitions` list
  the sized and hidden columns and rows.
- New test: `SpreadsheetWorkbookRoundTripTest` - a two-sheet workbook with
  all of the above saved as .xlsx and .ods, loaded back, saved in the other
  format and loaded again; where LibreOffice is installed, also converted by
  `soffice` (.xlsx to .ods and to .xlsx, .ods to .xlsx) and checked for the
  frozen panes, hidden rows and columns, names, filter and print areas. An
  .ods that `soffice --convert-to` writes holds no view settings from any
  source, so the frozen panes are not checked in that one; LibreOffice
  opened with a window reads them from ours.
