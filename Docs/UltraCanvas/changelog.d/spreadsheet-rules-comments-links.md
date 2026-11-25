- **Spreadsheet conditional formats were stored but never drawn, and no
  file kept them.** `UltraCanvasSpreadsheet::PaintCell` - the grid's and the
  printed page's painter - now draws them (`SpreadsheetSheet::
  GetConditionalFormatting`): value comparisons (a number, text or a
  formula), formula rules moved to each cell, text, blank and error rules,
  top or bottom values, above or below the average, duplicate and unique
  values, two- and three-colour scales and data bars. A rule's style lays
  only what it sets over the cell (`ApplyConditionalStyle`); the figures a
  scale or a bar needs are worked out once per change of the cells. `.xlsx`
  reads and writes them as `conditionalFormatting` with `dxfs`, `.ods` as
  `calcext:conditional-formats` with common cell styles, both as LibreOffice
  does.
- **Data validation in files, and the rules it skipped.** `.xlsx`
  (`dataValidations`) and `.ods` (`table:content-validations`) read and
  write validation rules. A Custom rule's formula is evaluated (it passed
  everything), a list may be taken from cells (`formula1` `"=$G$1:$G$9"`,
  `IsRangeListSource`, `SpreadsheetSheet::GetValidationListItems`), and a
  bound may be a formula (`"=$F$1"`).
- **Comments and links.** A comment is marked by a red corner and shown
  while the pointer rests on its cell; a link's text is blue and underlined,
  shows where it goes, and Ctrl+click opens it (`OpenHyperlink`: an address
  through `OpenURL`, a place in the workbook by going there;
  `onOpenHyperlink` takes it over). `SetCellComment` / `SetCellHyperlink`
  and their `Remove...` are undo steps. `.xlsx` keeps comments (the comments
  part and the VML drawing Excel shows them with) and links (`hyperlinks`),
  `.ods` keeps them as `office:annotation` and `text:a`; a cell that holds
  only a note or a validation is written.
- **The rules dialogs.** `ShowConditionalFormatDialog`,
  `ShowDataValidationDialog`, `ShowCommentDialog` and `ShowHyperlinkDialog`
  edit the selection's rules and the active cell's comment and link, each a
  form of the framework's elements in a modal dialog;
  `AddSelectionConditionalFormat`, `ClearSelectionConditionalFormats` and
  `SetSelectionValidation` do it from code.
- Covered by `SpreadsheetRulesTest`: the rules evaluated, drawn on an
  off-screen page, set through the grid's calls, and saved as `.xlsx` and
  `.ods` and loaded back - and, where LibreOffice is installed, converted by
  it and loaded back.
