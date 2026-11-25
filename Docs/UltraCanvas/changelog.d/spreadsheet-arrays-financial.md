- **Spreadsheet: array formulas and spills.** A range or an array constant
  (`{1,2;3,4}`) is a value: arithmetic and the functions of single values
  (`ABS`, `IF`, `LEN` ...) work element by element, and a formula whose
  result is an array spills into the cells to its right and below, as in
  Excel 365 - `#SPILL!` when a cell in the way is taken, the spill following
  its source as it grows and shrinks, `A1#` for the whole spill and `@` for
  one value. The spilled cells hold values, not formulas
  (`SpreadsheetCell::IsSpilledValue`). Legacy array formulas fill a fixed
  range: Ctrl+Shift+Enter in the grid enters one over the selection
  (`SpreadsheetSheet::SetArrayFormula`). `LET`
  and `LAMBDA` (called directly, through a LET name, or by `MAP`, `REDUCE`,
  `SCAN`, `BYROW`, `BYCOL`, `MAKEARRAY`), and arguments left out
  (`SEQUENCE(3,,10)`).
- **Spreadsheet: dynamic-array functions** `FILTER`, `SORT`, `SORTBY`,
  `UNIQUE`, `SEQUENCE`, `RANDARRAY`, `TRANSPOSE` and `XMATCH`; `XLOOKUP`
  returns whole rows or columns and takes its match and search modes.
- **Spreadsheet: financial functions** `NPER`, `RATE`, `IPMT`, `PPMT`,
  `CUMIPMT`, `CUMPRINC`, `IRR`, `XIRR`, `XNPV`, `MIRR`, `SLN`, `SYD`, `DB`,
  `DDB`, `VDB`, `EFFECT` and `NOMINAL`, with Excel's iteration limits for
  `RATE`, `IRR` and `XIRR` (`#NUM!` when they do not converge).
- **.xlsx and .ods keep array formulas.** .xlsx: `<f t="array" ref>`, a
  dynamic one with the cell metadata (`cm`, `xl/metadata.xml`) that keeps it
  a spill in Excel; `A1#`, `@`, the new functions' prefixes and LET's names
  as Excel stores them; a formula's text, Boolean or error result is written
  with its type. .ods: `table:number-matrix-*-spanned`, inline arrays and
  Excel's functions in their `COM.MICROSOFT.` namespace (read too). A
  formula read from a file without dynamic arrays is worth one value, a
  range in it giving the cell on its row, as Excel and LibreOffice read it.
- The spreadsheet documentation lists the common Excel functions still
  missing, by category.
