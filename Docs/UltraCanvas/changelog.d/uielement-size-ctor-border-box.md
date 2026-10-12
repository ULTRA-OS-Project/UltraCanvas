- **A widget's size includes its padding and border whichever constructor
  built it.** `UltraCanvasUIElement`'s size-only constructor `(id, w, h)`, and
  the default one, kept the layout engine's content-box default. The full-rect
  constructor `(id, x, y, w, h)` set border-box. So the same widget with the
  same size came out larger by its padding and border when it was built
  without a position: an `UltraCanvasTreeView(id, 200, 150)`, with its 1px
  border, was 202 x 152. Both constructors now set border-box. This affects
  the tree view's and `UltraCanvasImageElement`'s size-only and id-only
  constructors and the `UltraCanvasGLSurface` copy, and only where such an
  element has padding or a border. The HTML reader sets its images' box
  sizing itself and is unchanged. `Tests/UIElementBoxSizingTest.cpp`.
