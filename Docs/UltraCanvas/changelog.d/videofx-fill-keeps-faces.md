- **VideoFX 0.15.0: a video clip that fills the frame keeps its people.**
  `VideoFXSegment::keepInView` and `keepFacesInView` now mean something on a
  file segment when the export's `fitMode` is `Fill`: the crop window is
  placed around the regions instead of the picture's centre, and with
  `keepFacesInView` the engine finds the faces itself in one frame from the
  middle of the clip's used range (once per clip; the preview plans the same
  segments for every frame). A bad region on a file segment is refused as
  it is on a photo. Nothing changes under `Letterbox` or `Stretch`, or for a
  clip with no region and no face.
