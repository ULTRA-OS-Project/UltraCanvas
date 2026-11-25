- **Opened again from another virtual desktop, UltraClipboard comes to the
  desktop you are on (Linux, X11).** With its window on desktop 1, the
  desktop's clipboard button, *Search* or *Edit* from the quick panel, or a
  second start did nothing on desktop 2: the window was only marked "wants
  attention" where it was. It now moves to the current desktop and comes to
  the front (the framework's `RaiseAndFocus`); while one of its dialogs is
  open, the window comes over behind that dialog.
