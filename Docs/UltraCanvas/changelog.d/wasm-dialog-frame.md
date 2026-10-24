- **WebAssembly: dialogs have a title bar and can be moved.** A browser
  draws no window frames, so every dialog (`WindowType::Dialog`) was a bare
  canvas with no title and no way to move it. The backend now draws a title
  bar above it - the window's title, an outline and a shadow - which moves
  the dialog when dragged and reports the new place to the window
  (`WindowMove`). A dialog's title no longer replaces the page's title.
