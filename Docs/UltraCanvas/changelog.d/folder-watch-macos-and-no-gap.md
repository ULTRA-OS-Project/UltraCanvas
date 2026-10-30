- **macOS gets a native folder watch (FSEvents).** `UltraCanvasFolderWatcher`
  had backends only for Linux/BSD (inotify) and Windows
  (`ReadDirectoryChangesW`), so on macOS the Filer re-read the open folder on
  a timer and showed a saved file only on the next poll. The new backend
  (`OS/MacOS/UltraCanvasMacOSFolderWatcher.cpp`) runs an FSEvents stream on the
  folder on a private dispatch queue, drops events for subfolders so it answers
  for this folder only like the other backends, counts events FSEvents
  coalesced or dropped as a change, and reports the folder being deleted, moved
  away or unmounted (`RootChanged` / `Unmount`) through the failure callback.
  `Stop()` drains the queue, so no callback arrives after it returns. The
  library and `FolderWatcherTest` link CoreServices for it.
- **A file saved into a folder just opened in the Filer was missed.** The
  widget read the folder first and started the watch afterwards, so whatever
  happened during the read was never reported. It now starts the watch first
  and reads second; a change during the read lists the folder once more. On
  Windows the backend also issued its first `ReadDirectoryChangesW` on its
  thread after `Watch()` had returned, and the system only collects changes
  from that first call on; `Start()` now issues it, so every backend is
  listening by the time `Watch()` returns. `FolderWatcherTest` checks a change
  made right after `Watch()`.
