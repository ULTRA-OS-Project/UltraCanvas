- **The CDR plugin no longer reads freed memory when it draws a page.**
  `UltraCanvasCDRPainterImpl` records a drawing as draw commands on the
  document's pages while libcdr parses, and `UltraCanvasCDRRenderer` replays
  them when a page is drawn. The commands captured the painter (`this`) and
  called back into it, but the painter is a local of `ParseCDRFile` /
  `ParseCDRMemory` and was gone by then, so every gradient fill looked its
  gradient up through a dead object. The DemoApp's *CDR Images* page crashed
  on `detailed.cdr` (ten gradients) with a segmentation fault in
  `CreateGradientPattern`. The commands now capture the document they belong
  to, which outlives them; `ApplyStyleToContext`, `FillAndStroke` and
  `CreateGradientPattern` are static and take it. `CDRDocument` is no longer
  copyable, since a copy's commands would point at the original.
- **`CDRPluginRenderTest`** (`Tests/VectorFormatsTests.cmake`, so Windows CI
  runs it too) loads every drawing of `media/vector/CDR` through the CDR
  plugin, draws each page after the parse and checks it paints - and that
  `detailed.cdr` shows its gradients. On the code before this fix it crashes
  in `CreateGradientPattern`.
