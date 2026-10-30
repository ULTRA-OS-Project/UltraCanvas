- **The attachments the media viewer opens no longer pile up.** Each was
  written to a folder of its own in the shared temporary folder and never
  removed. They are now kept in the per-user cache
  (`opened-attachments` under `~/.cache/UltraCanvas`,
  `~/Library/Caches/UltraCanvas` or `%LOCALAPPDATA%\UltraCanvas`), where other
  users of the machine cannot read them, and a folder is swept a day after it
  was made - once a process, when the first viewer is made and when the first
  attachment is opened; one whose file another program still holds open goes
  on a later sweep.
- **`DiskCache::SweepFolders` (UltraCanvasDiskCache 1.1.0):** the retention
  sweep for a cache that keeps a folder per entry: every subfolder older than
  the age given goes with what it holds; files beside them, links and
  folders stamped in the future are left alone.
