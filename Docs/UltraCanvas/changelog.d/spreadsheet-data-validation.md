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
  rules are still not evaluated. New test: `SpreadsheetValidationTest`.
- **The spreadsheet grid enforces validation rules on what is typed.** An
  entry that breaks its cell's rule is answered as the rule's `alertStyle`
  says:
  - Stop: Retry (the entry back in the editor) or Cancel (the old value back);
  - Warning: Yes, No or Cancel;
  - Information: OK or Cancel.

  A refused entry leaves no undo step. An application can ask in its own way
  with `onValidationFailed`. After Enter or Tab, Retry stays on the refused
  cell instead of moving on.
- **List rules get a dropdown.** A List rule puts an arrow beside the active
  cell; a click or Alt+Down opens its items, and picking one enters it as one
  undo step (`ShowValidationListAt`, `GetValidationListItems`,
  `ChooseValidationListItem`). `SpreadsheetSheet::GetEffectiveValidation`
  gives the rule that applies to a cell. New test:
  `SpreadsheetValidationGridTest`.
- **A plain value typed into a spreadsheet cell was not an undo step and did
  not fire `onCellChange`.** The commit wrote into the cell directly. It now
  goes through `SpreadsheetSheet::SetCellValueFromString` with undo
  recording.
- `UltraCanvasSpreadsheet::IsCellVisible` was declared but never defined. It
  is defined now; frozen rows and columns count as visible.
- `UltraCanvasRichTextEdit.h` declared `struct FurnitureLayout` twice in the
  class, so Clang warned (`-Wredeclared-class-member`) in every file that
  included it.
- `UltraCanvasFileLoader.h`: the comment on `OpenTextDocument` still said a
  legacy `.doc` file is rejected. It is read by the DOC reader, which is
  read-only, so it is saved as `.docx` or `.odt`.
