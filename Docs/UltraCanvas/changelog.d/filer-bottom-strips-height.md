- **`UltraCanvasFilerWidget::GetBottomStripsHeight()`** says how much of the
  display's bottom edge its own strips take - the selection info bar and the
  hidden-items notice above it - so a host that floats an element over the
  display's bottom corner keeps clear of them. Both strips come and go (with
  the folder, with what it hides, with Display > Info-Bar) without a
  callback, so the host asks each time it places the element. UltraFiler's
  connection log button, which now floats in the display's bottom-left
  corner, is the first user; documented in `UltraCanvasFilerWidget.md`.
