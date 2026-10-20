- **A list of what was opened last, for every start window:
  `UltraCanvasRecentList.h`.** `LoadRecentList(file)`, `RememberRecent(file,
  entry, keep)` and `ForgetRecent(file, entry)` keep one entry per line in a
  file of the application's settings folder, the newest first, eight by
  default. Header-only, like `UltraCanvasDataFolder.h`, so a start window
  uses it without linking anything and a test runs it on a temp folder.
  UltraPress's start window (its site folders) and UltraFIBU's (its
  bookkeeping files) read theirs through it.
