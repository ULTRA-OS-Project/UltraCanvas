- **A .docx that opened as hex opens as a document.** A file whose main part
  is not `word/document.xml` (`word/document2.xml`, as files that went through
  Word for the web can have) and a file written with its own XML prefixes were
  not recognised as Word documents, so Texter showed their bytes (framework
  Word reader fix).
