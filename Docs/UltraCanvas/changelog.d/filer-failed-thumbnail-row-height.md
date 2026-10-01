- **A picture whose thumbnail could not be made no longer shortens its row.**
  The thumbnail grid shortens a row of landscape pictures to the height they
  are drawn at, measured from the file headers. A file whose decode was given
  up on is drawn as its type glyph instead, and that glyph was squeezed into
  the shortened row - so a folder whose thumbnails failed showed its glyphs at
  two sizes, row by row, and the sizes changed whenever the Display >
  Thumbnails switches were touched. A failed file now counts as full height,
  and the row relays out when the failure is known.
- **`GetThumbnailCacheStats()` counts the thumbnails that are not shown** -
  `pendingEntries` (waiting, including those being made), `inFlightEntries`
  (being made now) and `failedEntries` (given up on) - so a host can tell
  "still on its way" from "the workers are stuck" from "the files would not
  decode".
