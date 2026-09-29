- **VideoFX: photos on the timeline, "Ken Burns" motion and slideshows.**
  - `VideoFXSegment::FromImage(path, seconds, motion)` and
    `FromImageFrame(rgba, ...)` put a still image on the timeline, with
    transitions, effects and overlays like any segment. `VideoFXImageMotion`
    moves a virtual camera across it: `ZoomIn`, `ZoomOut`, four pans,
    `Custom` start / end zoom and centre, `Still` (fitted like video), and
    `Auto`, which varies the move per segment and pans along the direction
    the frame crops. Moves are eased, zoom runs geometrically, and every frame
    is resampled at sub-pixel positions in VideoFX itself, so there is none
    of the stepping FFmpeg's `zoompan` shows. Large photos are shrunk once to
    what the closest zoom needs and loaded only when their segment plays.
  - `VideoFX_CreateSlideshow(images, output, options)` makes a slideshow in
    one call: seconds per image, a transition (crossfade by default),
    per-image captions, fade from and to black, 1080p30 unless sized.
  - JPEG EXIF orientation is honoured, in exports and in
    `VideoFX_ExtractFrame` / `ExtractThumbnails`. Reading a single image
    twice no longer fails (the image demuxers report end-of-file after any
    seek; a still is now re-read from a fresh open), and JPEG frames could
    not be extracted at all before.
  - `videofx slideshow` with `--seconds`, `--motion` and `--caption`;
    `VideoFXTest` grows to 241 checks.
