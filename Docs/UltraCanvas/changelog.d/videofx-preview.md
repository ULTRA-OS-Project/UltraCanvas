- **VideoFX 0.9.0: preview.** `VideoFXPreview` renders any frame of a
  timeline as the export would make it, without an export: `Open` plans the
  segments once, as `VideoFX_Export` does, and `RenderFrame(seconds, frame,
  maxWidth, maxHeight)` gives the output frame at that time - the segment
  playing then, trimmed, sped, filtered and fitted to the output size, its
  overlays drawn, and inside a transition's overlap the two segments
  blended by that transition (music is ignored). `SegmentAt` says which
  segment plays at a time and how far into it; `GetDuration`, `GetWidth`,
  `GetHeight` and `GetFrameRate` are what the export would produce. An
  editor's viewer keeps one open while scrubbing. `VideoFX_RenderFrame`
  does it in one call for a single picture, and `VideoFX_PlanSlideshow`
  gives the segments and settings a slideshow would be exported from, so a
  slideshow can be previewed or opened on a timeline before it is rendered.
  `videofx preview <project.vfxproj> <seconds> <out.png>` writes a saved
  project's frame. The single-frame decoder behind `VideoFX_ExtractFrame`
  is shared with the preview.
