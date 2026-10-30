- **macOS: a double-click on an application in the folder display did
  nothing.** An `.app` is a folder on disk that the file display launches
  instead of entering, and UltraFiler's double-click handler skipped every
  folder before looking at what it was. The application now starts, as it does
  in the Finder and already did in the preview pane's folder peek, and the
  History lists it under Apps rather than Folders.
