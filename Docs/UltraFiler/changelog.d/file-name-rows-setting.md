- **Settings > Display > File names.** How much of a long name a thumbnail
  tile shows is now a choice: *Full name* (as many rows as the name needs),
  or at most *2*, *3* or *4 rows*. With a limit, a name that needs more rows
  keeps its end - the start of the last row gives way to "…" so the extension
  stays readable - and hovering the tile shows the whole name. Every tile of a
  grid row is as tall as its longest caption, so the rows grow and shrink with
  the choice; the Details, List and Size bars views keep one line per file. The
  treemap's cells follow the same limit within the room a cell has. The
  default stays at 2 rows, the caption earlier releases drew; the choice is
  saved to `config.ini` (`display.file.names.lines` = `full`, `2`, `3` or `4`)
  and applied to every file display of the window. Needs the framework's
  `UltraCanvasFilerWidget::SetCaptionMaxLines` (see
  `Docs/UltraCanvas/changelog.d/filer-caption-max-lines.md`).
