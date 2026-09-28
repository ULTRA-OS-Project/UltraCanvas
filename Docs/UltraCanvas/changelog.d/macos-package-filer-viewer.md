- `package-macos.sh` now bundles **UltraFiler** and **UltraViewer** (the
  universal media viewer) as `.app` bundles beside Texter, the demo,
  UltraNetMonitor and DeviceExplorer, so the macOS CI artefact
  (`UCDemo-MacOS-*`) and the signed release carry them, as the Linux
  portable bundle already did. UltraViewer declares itself a Viewer
  (`LSHandlerRank` Alternate) for images, SVG, video, audio, PDF, EPUB and
  plain text, so Finder offers it under *Open With*.
