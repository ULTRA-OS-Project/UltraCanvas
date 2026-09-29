- **VideoFX stage 2: transitions between segments, titles and logos.**
  - `VideoFXSegment::transitionIn` blends the previous segment into this one
    over 0.04-5 s: 30 `VideoFXTransitionType`s (crossfade, dissolve, fade
    through black / white, wipes, slides, smooth pushes, squeezes, circle /
    rect / radial reveals, pixelize, blur), on FFmpeg's `xfade`. The
    segments overlap by the transition's length and their sound is
    cross-faded over the same span, in every output including GIF and
    audio-only files. The exporter holds the last D seconds of a segment back
    until the first D seconds of the next are in; a clip shorter than the
    transition shrinks the overlap instead of failing.
  - `VideoFXSegment::overlays` draws `VideoFXOverlay::Text` (drawtext:
    literal text, font size as a fraction of the frame height, colour,
    shadow, background band, default system font or `fontPath`) and
    `VideoFXOverlay::Image` / `ImageFromFrame` (PNG transparency kept, RGBA
    from memory, scaled to a fraction of the height) on the output frame,
    at one of nine anchors or a custom position, with start / end times,
    fade in / out and opacity. `VideoFX_IsTextOverlayAvailable()` reports
    whether the FFmpeg build can draw text.
  - GIF output now builds its palette once per frame at the very end, so
    transitions and overlays get colours of their own and nothing is
    buffered for the whole file.
  - `videofx` gains `--transition NAME[:SECONDS]`, `--title TEXT` and
    `--watermark IMAGE`; `VideoFXTest` grows to 182 checks.
