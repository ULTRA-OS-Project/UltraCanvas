- **A spreadsheet could not be printed or saved as PDF.** `PrintSettings`
  and `SetPrintArea` existed, but nothing drew pages. The new
  `SpreadsheetPrintPages` (`UltraCanvasSpreadsheetPrint.h`) does:
  - it cuts the print area (or the cells in use) into pages by the sheet's
    `PrintSettings`: paper and orientation, margins, scale or fit to pages,
    manual row and column breaks;
  - it can print gridlines and column and row headings, centre the cells,
    repeat title rows and columns, and print header and footer lines with
    the page (`&P`), the page count (`&N`), the sheet (`&A`) and the date
    (`&D`); the title rows and columns are the sheet's own print titles
    (`PrintSettings::repeatRowsFirst` ...), which .xlsx and .ods now keep;
  - each cell is drawn by `UltraCanvasSpreadsheet::PaintCell`, the grid's own
    painter (lifted out of `RenderCell`), so paper shows what the screen
    shows.

  It writes a PDF (`ExportPdf`, to a file or to memory) and is an
  `IPrintPageSource` for `PrintDocumentWithDialog`: drawn on the printer's
  own context, or as an image where the printer has none, like
  `RichDocumentPrintPages`. `CreateSpreadsheetPrintPages(grid)` gives the
  sheet on show. Covered by `SpreadsheetPrintTest`, which checks the pages
  for default paper, landscape, a print area, a break, fit to width and title
  rows, and writes the PDF.
