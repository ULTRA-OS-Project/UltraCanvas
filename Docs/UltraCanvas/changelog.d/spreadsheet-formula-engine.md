- **A formula reading another formula cell gets its result, typed.** The
  engine handed a formula cell on as its display text, so `='Data'.B5` of a
  formula cell held the text `"1008.75"` (arithmetic on it worked only by
  luck of the text-to-number conversion), and `SUM` over formula cells left
  them out. It now reads the stored result - number, text, Boolean or error.
- **Cells are calculated in dependency order.** `RecalculateAll` visited cells
  in storage order, so a formula reading a formula further on - a total above
  the rows it adds up, an earlier sheet reading a later one - read nothing. A
  reference to a formula not yet calculated in the pass now calculates it
  first; a circular reference reads the value it has, and a chain deeper than
  200 cells is finished by further passes rather than a deeper stack.
- **A reference to a sheet that does not exist is `#REF!`**, as in Excel; it
  read the current sheet's cell of the same address.
- **A formula with an error literal no longer hangs.** The tokenizer never
  stepped past a `#`, so `=IF(ISNA(A1),#N/A,1)` - or a stray `#` typed into a
  cell - spun forever in whatever parsed it. Error literals (`#N/A`,
  `#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`, `#NUM!`, `#NULL!`, ...) are now
  read as errors in any case, an unknown one as `#NAME?`, and the tokenizer
  makes progress on every character whatever it holds.
- **`.xlsx` cross-sheet formulas work.** The loader kept Excel's `Data!B5`,
  which the engine cannot read (it recalculated to `#NAME?`), and the saver
  wrote the engine's `'Data'.B5`, which Excel cannot read. Both directions are
  translated now (`UltraCanvasSpreadsheetExcelFormula.h`:
  `ExcelFormulaToNative`, `NativeFormulaToExcel`), including the `_xlfn.`
  prefix Excel stores before its newer functions. A quoted sheet name may now
  hold a `.` or a doubled quote (`'It''s'.A1`).
- **About a hundred Excel-compatible functions** join the 45 the engine had
  (`UltraCanvasSpreadsheetFormulaFunctions.cpp`): lookups (`VLOOKUP`,
  `HLOOKUP`, `LOOKUP`, `XLOOKUP`, and `INDEX` / `MATCH` over two dimensions
  with match types and wildcards), conditional aggregation (`SUMIF`,
  `SUMIFS`, `COUNTIF`, `COUNTIFS`, `AVERAGEIF`, `AVERAGEIFS`, `MAXIFS`,
  `MINIFS`) with Excel's criteria, `SUMPRODUCT`, `SUBTOTAL`, rounding and
  number theory, the standard statistics, text (`FIND`, `SEARCH`,
  `SUBSTITUTE`, `TEXTJOIN`, ...), dates (`EDATE`, `EOMONTH`, `DATEDIF`,
  `WEEKDAY`, ...), `IFNA`, `IFS`, `SWITCH`, `XOR`, the `IS*` family, `FV`, `PV`
  and `NPV`. A function now sees its arguments in the order written and each
  range's rows and columns (`FormulaEvaluator::GetCallArguments`).
- **Text functions count characters, and numbers become text as shown.**
  `LEN`, `LEFT`, `RIGHT` and `MID` counted bytes, so `=LEN("ขาย")` was 9;
  it is 3. A number joined into text was written with `std::to_string`
  (`12.500000`, with the reader's decimal comma on some desktops); it is
  `12.5`.
