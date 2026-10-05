- **The media viewer's toolbar offers only what the shown file takes.** The
  second row used to show every tool for every file, so a video, a PDF, a 3D
  model, a spreadsheet, a text file or a drawing still offered the gamma /
  brightness / colour sliders, Curves with its histogram, rotate, mirror and
  Save as - buttons that did nothing, or that worked on a picture that was
  not the file. Each load now decides the tool groups
  (`UltraCanvasMediaViewer::GetAvailableTools()`, a `MediaViewerTools`): a
  bitmap gets them all; an SVG or another drawing on the image surface keeps
  zoom, rotate / mirror and Save as, but no colour adjustments and no Curves;
  a `*.ucd` preview, a PDF and an e-book get zoom; text, spreadsheets, fonts,
  3D models, video and audio get only Details, because their views carry
  their own controls. Separators follow the groups, so none stands alone.
- **An open adjustments panel folds away for a file it cannot change** and
  comes back with the next bitmap while its toggle is still on (and when the
  top bars are shown again, where it used to stay closed under a pressed
  toggle). The adjustments still carry from one photo to the next, but they
  no longer reach a drawing or a document preview browsed to in between,
  which used to be drawn tinted by them - and now hides the sliders that
  would take the tint off. An
  open Curves dialog drops the previous picture's histogram for such a file
  instead of showing it as this one's.
- **Zoom works on the vector drawing view.** DXF, DWG and the other drawings
  the Vector plugin reads were shown with the zoom buttons up and no effect;
  zoom in / out, Fit and the zoom levels now drive the drawing.
- `UltraCanvasMediaSurface::ShowImage` has an overload that takes the
  adjustments for the new image, so it is colour-processed once rather than
  with the previous image's settings first. `Tests/MediaViewerToolsTest`
  checks the tool set and the toolbar for a photo, an SVG, text, a
  spreadsheet, an STL model, video, audio and a PDF, and the panel and the
  adjustments across them.
