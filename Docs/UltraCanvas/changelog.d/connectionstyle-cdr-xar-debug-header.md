- **The block diagram's and the chart renderer's connection styles can be used
  in one program.** Both declared `UltraCanvas::ConnectionStyle` - an enum in
  `UltraCanvasBlockDiagram.h`, a struct in `UltraCanvasConnectionRenderer.h`
  (pulled in by the chord chart and the circular infographic) - so a file
  that included both did not compile. The block diagram's enum is
  `BlockConnectionStyle` now, beside the existing `FlowChartConnectionStyle`;
  code that names `ConnectionStyle::Orthogonal` for a block diagram changes
  to `BlockConnectionStyle::Orthogonal`.
- **DemoApp: a CorelDRAW file saves as XAR.** The CDR page offered "Xara
  drawing (not finished yet)" and called `UltraCanvasCDRPlugin::ExportToXAR`,
  which always failed, saying there was no CorelDRAW reader into the vector
  document model. There is one - the Vector plugin's `CDRConverter` - and its
  `XARConverter` writes that model, so the page saves the first page through
  them. `ExportToXAR` cannot reach them (the Vector plugin builds on the CDR
  plugin, not the other way round); it still fails, and its error now names
  that path.
- `UltraCanvasCairoDebugExtension.h` is gone: nothing included it, it called
  a `GetRenderContext()` that no longer exists, so it did not compile, and it
  defined functions in a header without `inline`.
