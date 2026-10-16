- **A button grows to fit its text.** Buttons were given fixed widths sized
  for their English labels, and a longer label - a translation, a larger
  font - ran past the button's edges. A width given in the constructor or
  `SetSize()` is now the least a button with text takes: it grows when the
  text needs more and returns to the given width when the text is short
  again, and a flex row too narrow for its buttons no longer shrinks one
  below its text (`min-width: auto`). `SetGrowToFitText(false)` keeps a
  fixed width; buttons without text are not affected.
- **DemoApp: button labels without "...".** The demo pages' buttons dropped
  the trailing "..." / "…" ("Open Spreadsheet File", "Import CSV", "Save",
  "Open video", "Upload image", ...); the hints that quote a label match.
- **Button: an icon above or below the text is measured by height.** The
  measured width added the icon's width beside the text for every icon
  position, so a button with its icon on top (or a Center icon, which hides
  the text) measured far wider than it draws.
