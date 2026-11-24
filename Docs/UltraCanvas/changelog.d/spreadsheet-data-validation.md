- **A validation rule set on a spreadsheet cell accepted every value.**
  `SpreadsheetCell::ValidateValue` was a stub that always returned true.
  Range rules had their own checker in `SpreadsheetSheet::ValidateCell`,
  which had problems of its own:
  - text in a number rule read as 0 and could pass a "between 0 and 10" rule;
  - a bound read with `std::stod` honoured the desktop's decimal comma;
  - a bound that was not a number counted as 0;
  - a date bound could only be a serial number.

  Both now go through one checker, `CheckDataValidation`
  (`UltraCanvasSpreadsheetCell.h`):
  - Number, date and time rules need a number, and bounds are read
    dot-decimal (a Date bound may also be `YYYY-MM-DD`, a Time bound `HH:MM[:SS]`).
  - A bound that cannot be read fails the check.
  - Text length counts characters, not bytes.
  - A list takes comma-separated items as typed, or ODF's quoted,
    semicolon-separated form.

  `ValidateCell` falls back to the cell's own rule when no range rule covers
  the cell. The default error messages go through `Tr`. Custom (formula)
  rules are still not evaluated, and the grid does not yet refuse an entry
  that fails its rule. New test: `SpreadsheetValidationTest`.
- `UltraCanvasFileLoader.h`: the comment on `OpenTextDocument` still said a
  legacy `.doc` file is rejected. It is read by the DOC reader, which is
  read-only, so it is saved as `.docx` or `.odt`.
