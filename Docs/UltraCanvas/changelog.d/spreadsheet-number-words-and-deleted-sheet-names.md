- **A typed "nan", "inf" or "0x10" became a number.** `std::stod` also reads
  these, so "nan" became a NaN, "-Infinity" an infinity, and "0x10" 16;
  "nan%" became a NaN percentage. `SpreadsheetCell::SetValueFromString` now
  takes only what a number is written with (digits, sign, decimal point or
  comma, exponent) and only a finite result. Everything else stays text.
- **A CSV field holding a number too large for a double imported as 0.**
  `CSVTryParseNumber` took `ParseFloatClassic`'s "read, but too large"
  (value left untouched) for the number 0. "1e400" now stays text.
- **Deleting a sheet kept the workbook names that belonged to it, broken.**
  As in Excel:
  - names scoped to the deleted sheet are removed;
  - names covering it stay, and a formula using one shows `#REF!` instead of
    `#NAME?`, also as a function's argument (`IsNamedRangeVisible`,
    `FormulaEvaluator::NamedRangeError`);
  - undoing the delete brings both back, under the sheet's new name if it
    had to take one.
