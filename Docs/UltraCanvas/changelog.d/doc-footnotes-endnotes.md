- **Footnotes and endnotes are read from .doc files.** The legacy Word reader
  skipped the reference marks and never read the note stories. It now reads
  where the body refers to each note (`PlcffndRef`, `PlcfendRef`) and each
  note's words (`PlcffndTxt`, `PlcfendTxt`, the stories after the main text)
  into `UCRichDocument::notes`, with numbered reference runs as DOCX and ODT
  have them - so a .doc's notes show at the foot of their page (endnotes
  after the body) and go along when the document is saved as DOCX or ODT.
