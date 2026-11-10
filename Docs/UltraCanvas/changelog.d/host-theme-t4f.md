- **The media elements and the viewers follow the theme (T4 of the host
  theme proposal, sixth and last part).** `FromTheme` on `AudioPlayerStyle`,
  `AudioRecorderStyle`, `VideoPlayerStyle`, `VideoRecorderStyle`,
  `VideoTileStyle`, `SlideshowConfig` and `SlideshowIndicatorStyle`,
  `FontViewerStyle` and `CurveEditorStyle` (without a base `Dark()` or
  `Default()` by the theme's flag), and `ApplyTheme` on the eight elements,
  with the constructor tail and the `SetStyle` rule. The audio bars take the
  panel, the accent and ink; the video player, recorder and tile stay a dark
  stage with a translucent bar and take the accent, danger and the caption
  size; the slideshow's info panel and indicators take the roles and its
  display sizes stay; the font viewer and the curve editor take the panel,
  the well, ink and the accent. Element roles with their fallbacks:
  `audio-recorder.pulse`, `video-player.stage`, `video-player.control-bar`,
  `video-recorder.stage`, `video-recorder.control-bar`, `video-tile.stage`,
  `video-tile.caption`, `slideshow.stage`, `curve-editor.diagonal`. With
  this every style struct with colours reads the theme; `UltraCanvasTheme.md`
  and the proposal mark T4 done. `UltraCanvasThemeTest` covers every mapping
  and the following by default.
