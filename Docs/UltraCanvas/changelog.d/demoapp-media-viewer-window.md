- DemoApp: clicking a sample to open it "full size" opens it in
  `UltraCanvasMediaViewerWindow`, over the demo's main window. Before, each
  page built its own full-screen window. This covers the bitmap format pages,
  the image performance test, SVG, CDR, XAR, EPS, DWG / DXF and STL, which
  now all go through `ShowFullSizeImageViewer(path)`. The viewer walks the
  rest of the sample's folder with the arrow keys, and Escape closes it.
- DemoApp: every sample that can be clicked open now shows the hand cursor. On
  the SVG, CDR, XAR, EPS and DWG / DXF tiles the pointer did not change before.
  On the bitmap format pages it used `LookingGlass`, which Windows draws as a
  crosshair.
