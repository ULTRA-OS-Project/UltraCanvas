- **Cells, charts and text could not be pasted from one office kind into
  another, and the spreadsheet grid and the slide editor said nothing to a
  screen reader.**
  - `UltraCanvasOfficeClipboard.h` (new): copy and paste between the grid,
    the document editor and the slide editor through the system
    clipboard's flavours - an HTML table or tab-separated text as cells
    (`ClipboardTableFromHtml`, `ClipboardTableFromTabbedText`,
    `ClipboardHtmlIsTable`), cells as a slide table's
    (`PptxTableCellsFromHtml` keeps bold, italic, colour and alignment,
    `PptxTableCellsFromRows`), a document's selection or plain text as a
    text box's paragraphs (`PptxParagraphsFromClipboardHtml` / `Text`),
    slide shapes as text and HTML for other programs; and the office
    clipboard for what no flavour carries - a chart (`CopyChartToClipboard`,
    `GetClipboardChart`), kept in the process beside a PNG of it (HTML) and
    its data (text) for every other program.
  - `UltraCanvasSlideEdit`: `Copy` / `Cut` / `Paste` (Ctrl+C / X / V) put the
    shapes' text and HTML on the system clipboard, and Paste reads it once
    something else was copied since - a spreadsheet's chart as a chart, cells
    as a table, a picture as a picture, a document's selection or text as a
    text box. `UCSlideDeckEditor::InsertTable(box, cells)`,
    `InsertFormattedTextBox`, `InsertChart`, `CopiedShapes`.
  - `UltraCanvasSpreadsheet`: with a chart selected, `Copy` / `Cut` take the
    chart onto the office clipboard; another program's HTML table is read by
    the shared `ClipboardTableFromHtml`.
  - `UltraCanvasRichTextEdit::Paste` takes a spreadsheet's chart as a chart
    frame in the line (`InsertChart`, `UCRichDocumentEditor::InsertChart`);
    cells already paste as a table through their HTML.
  - Accessibility: parts an element draws itself (`IAccessibleParts`,
    `AccessiblePart`, `UltraCanvasUIElement::GetAccessiblePartsInterface`), a
    grid of cells (`IAccessibleTable`, `GetAccessibleTableInterface`), the
    roles `TableRow`, `TableCell` and `Canvas` (mapped on AT-SPI, UI
    Automation and AppKit) and `AccessibilityEvent::part`. The grid is a
    table named by its sheet - rows and cells as parts, a cell named by its
    address and value ("B3 42"), the active cell focused, its value text,
    and announced when it or the selection moves; the slide editor is a
    canvas named by the slide shown, the strip's slides and the slide's
    shapes (named by their first words, else their kind) as parts, the
    selected shape focused and announced. The bridges do not yet publish
    parts as objects of their own.

  Covered in `OfficeClipboardTest` and `OfficeAccessibilityTest`.
