- **The Filer missed a file saved into the open folder and never showed it.**
  A save is several steps - Firefox writes an empty placeholder, then a
  `.part` file, then renames it to the real name. The first step made the
  folder watch ask for a rescan, and every rescan then stopped and restarted
  the native watch (`ReadDirectoryChangesW` / inotify) and cleared the
  "changed" flag. The steps that happened during the scan or during the
  restart were thrown away, so the listing stayed on the half-saved state
  until the user refreshed by hand. A rescan of the folder already watched now
  keeps its watch running, and a change reported while the scan ran is kept
  and triggers one more rescan; only moving to another folder discards
  pending news (`UltraCanvasFilerWidget::WatchFolder`).
