- **A video shown in the media viewer can be saved in another type.**
  *Save as* was there for a picture, a drawing and a recording, never for a
  film: the core has no video encoder, and the one transcoder, VideoFX, is a
  sibling module on FFmpeg. FileLoader gains the seam:
  `UltraCanvasFileLoader::ConvertVideoFile(source, target, error, progress)`
  copies the same type as it is and converts another with the converter
  its codec registration carries (`MediaCodecRegistration::convertVideo`,
  new; it makes the entry `canEncode`), written beside the target and moved
  there when complete - a stopped or failed conversion leaves no half file
  and keeps the one it was to replace. `CanConvertVideoTo`,
  `GetVideoSaveExtensions` and `AddVideoSaveTypes(opts)` list what it writes.
  `VideoFX/ultracanvas/VideoFXUltraCanvasFormats.h` (library
  `VideoFXUltraCanvasFormats`) registers VideoFX's converter for every
  container its FFmpeg writes - MP4, MOV, Matroska, WebM, AVI as the
  encoders allow - with `VideoFX_RegisterWithFileLoader()`. The viewer
  offers *Save as* for a video its player opened once a converter is
  registered (`ShowVideoSaveDialog`), and converts on a worker thread
  behind `UltraCanvasProgressDialog` - the ring follows the encoder, Cancel
  stops it. A build without a converter offers a video no *Save as*, rather
  than one that fails: `canEncode` alone is not enough, since on Linux the
  built-in table marks the GStreamer capture muxers (MP4, MOV, MKV, WebM,
  AVI) `canEncode` with no way to convert a file. Tests:
  `MediaViewerSaveTest` (a converter of the test's own: copy, convert,
  stop, a converter that ignores a stop, failure, the earlier file kept;
  VideoFX's on a generated clip into every type it writes),
  `MediaViewerToolsTest` (a film offers *Save as* only with a converter).
  Docs: *Saving a drawing, a recording or a video* in
  `UltraCanvasMediaViewer.md`, *A Video Saved in Another Type* in
  `Docs/Modules/FileLoader/README.md`, the VideoFX README's *Which files*.
