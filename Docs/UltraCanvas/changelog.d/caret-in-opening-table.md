- **A document that opens with a table starts its caret in the first cell.**
  `UCRichDocumentEditor` put a new document's caret at `{0, 0}` - for a table
  that is the table outside its cells, which the rich text editor draws as a
  bar the whole table high, and where nothing could be typed. An ODT letter
  whose address is a table showed that bar down the left of the address.
  `DocumentStart()` is now the top-left cell of such a table, which is where
  a new document's caret starts and where Ctrl+Home goes; Select All still
  starts before the table, so it still takes the table whole.
  `RichTextEditorTest` covers it.
