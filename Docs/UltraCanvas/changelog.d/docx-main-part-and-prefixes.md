- **A .docx that names its main part differently, or picks its own XML
  prefixes, opens as a document.** The Word reader looked only for
  `word/document.xml`, so a package whose main part has another name
  (`word/document2.xml`, as files that went through Word for the web can
  have) was not recognised as Word at all and an application fell back to
  its raw view - Texter showed it as hex. It now takes the part the package's
  `_rels/.rels` names as the office document (checked against
  `[Content_Types].xml`; a workbook or deck is still not Word), with its
  relationships, styles, numbering, settings, theme, notes and comments
  found from that part's folder. A part that binds the OOXML namespaces to
  other prefixes (`ns0:p` from Python XML libraries, WordprocessingML as the
  default namespace, the strict ISO 29500 URIs) is read as the `w:` / `r:` /
  `a:` one it is, where it used to read as having no body; the spaces between
  differently formatted words survive in it too. `WordFormatsTest` covers all
  three shapes.
