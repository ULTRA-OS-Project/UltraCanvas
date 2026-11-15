- **The media viewer saves a drawing as a drawing, and a recording in
  another audio type.** *Save as* on an SVG, a DXF, an EMF or any file the
  vector view shows offers every vector type this build writes and every
  bitmap type, drawn again at the size the Settings give
  (`AddVectorDocumentSaveTypes` / `SaveVectorDocument`); the vector view
  offers *Save as* too, so an SVG has one where libvips has no SVG loader
  (Windows). A drawing turned or mirrored on the image surface is still saved
  as the picture, and the same type as the file on show is copied as it is.
  A recording the audio player opened is offered every audio type this build
  encodes, through the new `UltraCanvasFileLoader::SaveAudio` /
  `ConvertAudioFile` (the same type copied as it is, another decoded and
  encoded). Tests: `MediaViewerSaveTest`, `MediaViewerToolsTest`.
- **The toolbar icons fill their slot.** `zoom-fit`, `rotate-left/right`,
  `mirror-h/v`, `curves` and `slideshow` drew inside a 3-4 unit margin of
  their 24-unit box, about 11 px of a 16 px slot; their view box is cropped
  to the drawing and the mirror triangles are larger.
- **The Save settings dialog is set in one type size.** Its captions,
  checkboxes and buttons were drawn at a fixed 12 while the size fields and
  lists followed the theme's body size, smaller on Windows; it now takes the
  theme's body size, the Save dialog opens it at its own, and the spinners
  and dropdowns follow (`FileDialogTest`).
