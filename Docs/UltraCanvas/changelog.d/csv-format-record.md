- **`CSVFormatRecord` writes one CSV/TSV record** (`UltraCanvasCSVImport.h`):
  the fields quoted as `CSVExportOptions` says - all of them, or those
  holding the separator, the quote character or a line break, a quote inside
  doubled - and the options' line ending. The spreadsheet's CSV export now
  writes its rows with it, so a writer that streams rows it never holds at
  once - UltraDBAdmin's table export - quotes exactly as the spreadsheet
  does. With *quote all fields*, an empty cell the sheet never set is now
  written `""` like every other field, where it used to be left out between
  two separators.
