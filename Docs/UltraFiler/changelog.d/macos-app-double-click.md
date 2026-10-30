- **macOS: a double-click on an application in the folder display did
  nothing.** An `.app` is a folder on disk that the file display launches
  instead of entering, and UltraFiler's double-click handler skipped every
  folder before looking at what it was. The application now starts, as it does
  in the Finder and already did in the preview pane's folder peek, and the
  History lists it under Apps rather than Folders.
- **macOS: a pinned application went to the Favorites' Folders tab.** Pinning
  now files an `.app` under Apps, beside the other applications, and its heart
  mark and Unpin find it there. Applications pinned earlier are moved from
  Folders to Apps at the next start, keeping the date they were pinned - left
  where they were, they could no longer have been unpinned
  (`UltraFilerFavorites::Reclassify`).
