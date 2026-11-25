- **Text drawn on several threads at once no longer crashes.** Every render
  context in the process shared one cache of Pango layouts, so a layout made
  on one thread was measured or drawn on another while the first still used
  it. UltraFiler crashed this way on Windows (an access violation in cairo's
  glyph lookup under `pango_renderer_draw_layout_line`) while its thumbnail
  workers drew document pages and slides. `RenderContextCairo` now keeps the
  layouts per thread: a layout is only handed back to the thread that made
  it, and the UI thread's are still shared by all its windows.
  `Tests/TextRenderThreadsTest.cpp` draws the same words on five threads at
  once; before the fix it crashed on every run, now it passes.
