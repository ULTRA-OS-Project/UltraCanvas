- **The Open and Save dialogs look like the platforms' current dialogs.**
  `UltraCanvasFileDialog` is laid out like the Windows 11 print dialog: a
  plain panel, the name of each field written above it ("File name", "File
  type" side by side, or the name over a "Show" row of toggles), the folder
  tree and the listing in rounded frames, 32-pixel fields and buttons with
  rounded corners, and the buttons at the right of a footer band under a
  hairline. The OK button says what it does - **Open**, **Save** or **Select
  Folder** - in the theme's accent. Its colours come from the theme
  (`UltraCanvasTheme::Current()`, or `ApplyTheme`), so it follows a dark
  theme too.
