- **FilerWidget: `onTransferFinished`.** A paste, a drop or a Move to / Copy
  to now reports when it has ended - walked to its end or cancelled from the
  progress window - with a new `FilerTransferResult`: how many of the chosen
  entries arrived (files and folders apart, a folder counting once), how many
  were skipped, the one entry's name, the destination, and a ready sentence
  for a status line (`"report.pdf" moved to "alt"`, `3 files copied to
  "alt"`, `Copy cancelled - 2 of 5 items copied to "alt"`). UltraFiler shows
  it on its status line (UltraFiler changelog 1.77.0).
