- **A double-click on a picture, an audio file or a video starts UltraViewer
  with it.** It comes first - before the registered program and before the
  preview pane - in every folder display, the split view's right-hand one and
  the folder peek in the preview pane included; a running UltraViewer shows the
  file in place of what it showed. When UltraViewer cannot be found or started,
  or the file is inside an archive or on a remote drive, the file opens the way
  it did before and the status bar says why, so a double-click never comes to
  nothing. *Settings > Handling > Opening files* has the new checkbox "Open
  pictures, audio and video in UltraViewer" (on by default;
  `handling.files.media.ultraviewer` in the settings file). Finding UltraViewer
  inside its own app bundle on macOS is a framework change - see the
  UltraCanvas changelog entry "`FindProgram` / `LaunchProgram` find a sibling
  application on macOS".
