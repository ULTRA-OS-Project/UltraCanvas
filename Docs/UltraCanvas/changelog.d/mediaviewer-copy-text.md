- **Text in the media viewer can be copied.** The text view (text, source,
  Markdown files and notes) and the Details panel are display-only text
  areas: they let the mouse select, but they never take the keyboard, so
  Ctrl+C reached whatever held the focus - in UltraFiler's preview pane the
  file display, which copied the files - and the viewer's own Ctrl+C test
  compared the character, which Ctrl turns into a control code on most
  platforms. A press in that text now gives the viewer the focus, and Ctrl+C
  / Ctrl+Insert (Cmd+C on macOS) copy the selection by key code, the Details
  panel's first; Ctrl+A selects all of the area last pressed in
  (`UltraCanvasMediaViewer` 1.25.0).
- **`UltraCanvasTextArea`: a Copy / Select All menu on read-only text.** A
  right-click on a read-only or display-only area that neither
  `onContextMenu` nor the spell checker takes opens Copy (dimmed without a
  selection) and Select All. `SetReadOnlyContextMenu(false)` turns it off
  (`UltraCanvasTextArea` 3.17.0).
