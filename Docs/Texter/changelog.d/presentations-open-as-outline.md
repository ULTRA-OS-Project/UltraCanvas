- **A PowerPoint deck or an OpenDocument presentation opens as the outline of
  its slides, not as a hex dump.** The Open dialog's "Documents" offered
  `.pptx` since FileLoader read decks, but Texter sent anything but `.odt`,
  `.docx` and `.doc` to its text loader, which showed a deck's bytes. A
  `.pptx`, `.pptm`, `.ppsx`, `.ppsm`, `.potx`, `.potm`, `.odp`, `.otp` or
  `.fodp` now opens in the document editor as each slide's number and title,
  its bullets, tables, pictures' descriptions and speaker notes; a protected
  or damaged deck says why. Saving the outline back under a deck's name is
  refused - it would have written plain text over the slides - and *Save As*
  `.odt` or `.docx` keeps it.
