- **DemoApp: `ShowFullSizeImageViewer` is now `ShowInMediaViewer`.** It has
  opened far more than bitmaps since it moved onto
  `UltraCanvasMediaViewerWindow` - vector drawings (SVG, EPS, XAR, CDR, DWG)
  and 3D models (STL) go through it too - so the name now says what it does.
  Every caller in the demo was updated.
- **DemoApp: removed the dead `UltraCanvasBitmapExamples.cpp`.** Its eight
  per-format pages (`CreatePNGExamples` ... `CreateBMPExamples`) had not been
  reachable since the Bitmap menu switched to `CreateBitmapFormatDemoPage`,
  and its two helpers (`ExtractImageMetadata`, `CreateImageInfoLabel`) were
  used only by those pages. The declarations and the CMake entry went with it.
