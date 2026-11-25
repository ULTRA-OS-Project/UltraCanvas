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
- **Conditional formats and validation rules are undo steps**, as comments
  and links are: each change made through the grid - `AddConditionalFormat`,
  `SetConditionalFormatRule` and `RemoveConditionalFormatRule` (new),
  `AddSelectionConditionalFormat`, `ClearSelectionConditionalFormats`,
  `SetSelectionValidation`, and the two dialogs - is one step
  (`UndoActionType::RulesChange`), and undoing a validation rule gives the
  cells back their own rules it replaced.
- **The dialogs, tried in a window.** The chart properties dialog was an
  empty window with no OK or Cancel (a `Custom` dialog builds no sections or
  buttons); it is drawn now. The conditional-format and validation dialogs
  are as tall as the rows the chosen rule needs (they kept room for every
  row, a wide empty band under one or two), keep their controls in place as
  rows come and go (`HoldFormCaptionColumn`, new in `UltraCanvasFormLayout.h`:
  the caption column as wide as every caption, hidden ones too), and their
  lists and fields fill the dialog's width, so the validation value's hint is
  no longer cut off; a comment fills its dialog. A data bar starts from a
  single colour (a bar from "Red, yellow, green" came out red), "Delete
  rule" shows only when there is a rule to delete, and a rule between two
  values reads "between 30 and 50", not "30 / 50".
- Covered by `SpreadsheetRulesTest`: the rules evaluated, drawn on an
  off-screen page, set through the grid's calls, and saved as `.xlsx` and
  `.ods` and loaded back - and, where LibreOffice is installed, converted by
  it and loaded back.
