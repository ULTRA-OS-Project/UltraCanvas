- **VideoFX 0.10.0: picture-in-picture.** `VideoFXOverlayKind::Video`: an
  overlay that plays another file's picture over a segment - placed, sized
  (`imageHeight`), dimmed and faded like an image, shown in its `start`..`end`
  window, playing from `videoStart` seconds into the file at its own speed,
  upright, without its sound; when the file ends first its last frame
  holds. `VideoFXOverlay::Video(path, anchor, height, videoStart)` makes
  one. The export decodes it beside the segment's own picture and the
  preview renders it in the frame; project files carry it as
  `"kind": "video"` with the file relative to the project. A missing
  overlay file fails the export before anything is written.
