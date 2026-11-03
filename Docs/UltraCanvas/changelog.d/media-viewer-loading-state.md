- **The media viewer no longer says "No media" while a file is on its way.**
  `UltraCanvasMediaViewerWindow` showed its window and then opened the file,
  so the first frame was the empty viewer - "No media" in the middle and in
  the info bar - for as long as the folder took to read and the file to open
  (long on a big folder such as a temp directory). New
  `OpenFileWhenShown` / `OpenFolderWhenShown` / `SetFilesWhenShown` show a
  turning ring (`UltraCanvasBusyIndicator`) in the middle of the display and
  `name · loading` in the info bar at once, and open the file after a frame
  of that has been drawn (or after 400 ms when the viewer is not drawn). Any
  other open or `CloseFile` replaces it; a list with nothing to show ends it;
  without an application the file opens at once. *No media* now means only
  that nothing was asked for. `UltraCanvasMediaViewerWindow::Show` opens this
  way.
- **`UltraCanvasMediaViewer::onCurrentFileChanged`**: the file on show - the
  one loading, each one browsed to, `""` when nothing is - for a window
  title. `UltraCanvasMediaViewerWindow` titles its window with it when no
  title was given, so the title follows the arrow keys.
- `UltraCanvasMediaSurface::SetEmptyText`: what the surface draws with no
  picture (empty draws nothing).
- `Tests/MediaViewerLoadingTest` (Xvfb) covers the loading state, the open
  after a frame, the empty list and the cancel.
