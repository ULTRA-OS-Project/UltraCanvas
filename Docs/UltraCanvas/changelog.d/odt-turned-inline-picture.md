- **A turned picture in the line keeps its turn in ODT.** The ODT writer
  wrote it upright and the reader ignored a `draw:transform` on an `as-char`
  picture; both now carry `RichTextRun::imageRotationDeg`, as DOCX does. It
  is written with a graphic style that makes it a drawing object for Writer,
  which then shows it turned in its line (as a plain picture Writer clips the
  turned picture to its old box).
