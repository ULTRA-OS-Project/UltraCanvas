- **A saved spreadsheet lost digits.** The .xlsx and .ods writers wrote each
  cell's number with the stream's six significant digits, so 3.14159265 came
  back as 3.14159 and 123456789.123 as 1.23457e+08 after every save. A
  number is now written with the fewest digits (15 to 17) that read back as
  exactly the same value, through the new `FormatFloatRoundTrip`
  (`UltraCanvasTextUtils.h`): 0.1 stays "0.1". Covered in
  `SpreadsheetWorkbookRoundTripTest`, also through LibreOffice's conversions.
