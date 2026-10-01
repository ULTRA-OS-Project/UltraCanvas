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
- **A stuck thumbnail job no longer stops every thumbnail after it.** The
  Filer widget makes thumbnails on two to four background workers, and a job
  that never finishes - a video in a cloud folder downloaded in full before
  its first frame can be read, a drive that stopped answering, a shell call
  that never returns - kept its worker for good. With every worker on such a
  job, no thumbnail was made again in any folder for the rest of the session,
  not even the ones waiting in the disk cache. A job running past 20 s now
  gets one more worker started beside it (up to eight extra), and the log
  names it. `GetThumbnailCacheStats()` reports `workerCount` and the longest
  running job (`longestJobPath`, `longestJobSeconds`).
- **Three more Filer paths and the image file reader are UTF-8 on Windows.**
  The folder-size walk, `StatEntryForPath`, the folder watcher's directory
  test and `UCImageRaster::LoadFileToMemory` handed a UTF-8 string straight
  to `std::filesystem` / `std::ifstream`, which on Windows goes through the
  ANSI code page and misses a Thai or CJK name. They go through
  `PathFromUtf8` now.
