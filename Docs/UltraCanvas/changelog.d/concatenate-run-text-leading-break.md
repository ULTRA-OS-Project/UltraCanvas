- **A block that starts with an empty line keeps it when written out.**
  `UCRichDocument::ConcatenateRunText` skipped the line break of a block's
  first run, while the editor - which shows that line and counts its `\n` in
  every position (`UCRichDocumentEditor::RunsText`) - did not. A code block
  whose first line is empty therefore lost that line in HTML and Markdown
  and in the ODT and DOCX writers, and a table cell starting with an empty
  line was copied as plain text without it. The function now counts every
  run's line break, so the serializers read the text the editor shows;
  `RunsText` is the same function.
