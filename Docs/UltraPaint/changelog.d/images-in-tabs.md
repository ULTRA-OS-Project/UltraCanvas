- **Images in tabs.** A window holds several images, one tab each, in a strip
  over the canvas (`UltraCanvasTabbedContainer`); each tab has its own paint
  surface and keeps its zoom and scroll. *File ▸ New*, *Open*, *Paste as New
  Image*, the strip's **+** and *Open in new tab* on a dropped image (in place
  of *Open new window*) add a tab instead of replacing the image or opening a
  window, so none of them asks to discard changes any more; the blank canvas a
  window starts with is replaced by the first image opened while nobody has
  touched it. Several files on the command line, and the files a second start
  hands over, open as tabs of the window used last. A dot marks a tab with
  unsaved changes; *File ▸ Close Tab* (`Ctrl+W`) and the tab's **×** ask before
  closing one, closing the window asks once about all of them, and
  `Ctrl+Tab` / `Ctrl+Shift+Tab` move between tabs. *File ▸ New Window* still
  opens a second window.
