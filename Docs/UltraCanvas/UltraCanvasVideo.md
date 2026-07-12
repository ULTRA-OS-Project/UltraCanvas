# UltraCanvas Video

Cross-platform video **playback** and **recording** for UltraCanvas. Mirrors the
three-layer design of UltraCanvas Audio: a frame resource, non-visual engines,
and composite UI elements, all behind a pluggable platform backend.

## Status

API + UI implemented. The default build compiles a **null backend** so app code
links and runs unchanged (`UltraCanvasVideoDevices::IsAvailable()` returns
`false`). When `ULTRACANVAS_ENABLE_VIDEO=ON` *and* a platform media framework is
detected at configure time, the real backend is compiled in and replaces it.

| Platform | Native backend | Playback | Recording |
|----------|----------------|----------|-----------|
| Linux    | **GStreamer** (V4L2 cameras / VA-API decode) | video + audio | video + audio |
| Windows  | **Media Foundation** (Media Session + sample-grabber + SAR; aggregate-source SourceReader → SinkWriter) | video + audio | video + audio |
| macOS    | **AVFoundation** (AVPlayer; AVCaptureSession) | video + audio | video + audio |

The backend is selected by platform at configure time. On Linux it is detected
via `pkg-config` (`gstreamer-1.0`, `-app-1.0`, `-video-1.0`, `-pbutils-1.0`); if
the dev packages are absent the null backend is used and a CMake status message
says so. On Windows and macOS the system frameworks are always present, so the
native backend is always compiled in when `ULTRACANVAS_ENABLE_VIDEO=ON`.

## Architecture

```
UCVideoFrame  (UltraCanvasVideo.h)          packed BGRA frame + metadata
   ▲
UltraCanvasVideoPlayer  / UltraCanvasVideoRecorder      non-visual engines
   ▲                          ▲
UltraCanvasVideoPlayerElement / ...RecorderElement      UI controls
   ▲
IVideoBackend (libspecific/Video/)          OpenDecoder / OpenCapture / cameras
   └─ VideoBackendGStreamer.cpp · VideoBackendNull.cpp
```

Frame flow: the backend decodes/captures on its own thread and hands `UCVideoFrame`s
up through session callbacks; the engine caches the latest frame behind a mutex.
Each UI element runs a periodic **main-thread timer** (`Application::StartTimer`,
~30 fps) that pulls the latest frame, uploads it into a Cairo `UCPixmap`, and
calls `RequestRedraw()`. Audio (for playback) is handled by the backend's own
audio sink, so A/V sync is automatic.

## Public API surface

### Frame resource — `UltraCanvasVideo.h`

`UCVideoFrame` holds one decoded frame as packed 32-bit pixels (BGRA from the
GStreamer path, which matches Cairo's `ARGB32` byte layout) plus `VideoFrameInfo`.
`VideoStreamInfo` carries width/height/fps/duration/codec.

### Playback (non-visual) — `UltraCanvasVideoPlayer.h`

```cpp
auto p = std::make_shared<UltraCanvasVideoPlayer>();
p->LoadFromFile("clip.mp4");
p->onEnded = []{ /* ... */ };
p->Play();
// UI thread, each tick:
if (auto f = p->GetCurrentFrame()) { /* upload f to a pixmap */ }
```

Transport: `Play / Pause / Stop / Seek(seconds)`.
Properties: `Volume`, `Mute`, `Loop`, `PlaybackRate`.
Frame access: `GetCurrentFrame()` (thread-safe).
Callbacks: `onLoaded`, `onPlaybackStateChanged`, `onFrameReady`,
`onPositionChanged`, `onEnded`, `onError`.

### Thumbnails / poster frames — `UltraCanvasVideoThumbnail.h`

One-call extraction of a single representative frame from a video file or URL —
for gallery covers (`UltraCanvasAlbum`), list previews, or a "poster" shown
before playback starts.

```cpp
#include "UltraCanvasVideoThumbnail.h"

VideoThumbnailRequest req;
req.timeSeconds = 3.0;   // grab at 3s; omit / set < 0 for an automatic position
req.maxWidth    = 320;   // optional: downscale to fit within 320x240,
req.maxHeight   = 240;   //   preserving aspect ratio

// (a) as a raw decoded frame
if (auto frame = CaptureVideoThumbnail("clip.mp4", req)) { /* upload / inspect */ }

// (b) as a ready-to-draw pixmap
auto pm = CaptureVideoThumbnailPixmap("clip.mp4", req);

// (c) straight to a file on disk — handy for Album thumbnailPath.
//     The encoder follows the extension: ".qoi" -> QOI, else PNG.
SaveVideoThumbnail("clip.mp4", "clip_thumb.png", req);
SaveVideoThumbnail("clip.mp4", "clip_thumb.qoi", req);   // QOI
```

All three are **synchronous** (they open the source and decode one frame, which
can take up to a few seconds), so call them off the UI thread. They are safe
no-ops returning null / `false` when no real backend is compiled in or the
source can't be decoded.

Under the hood the engine prefers the backend's dedicated single-frame grab
(`IVideoBackend::GrabThumbnail` — implemented on Linux/GStreamer as a throwaway
`uridecodebin` pipeline that prerolls to PAUSED, seeks accurately and pulls the
preroll sample, never touching audio). Backends that don't implement it fall
back to a generic decode-session path (open → mute → seek → capture first
frame), so a thumbnail is produced wherever decoding works.

`SaveVideoThumbnail` picks its encoder from the output extension — `.qoi`
writes QOI, anything else writes PNG. Both are always available (PNG via Cairo,
QOI via the bundled encoder) and need no libvips.

Feeding an album:

```cpp
SaveVideoThumbnail("clip.mp4", "covers/clip.png", { /*auto time*/ });

AlbumItem item;
item.mediaType     = AlbumMediaType::Video;
item.mediaPath     = "clip.mp4";
item.thumbnailPath = "covers/clip.png";   // the generated poster frame
album->AddItem(item);
```

### Recording (non-visual) — `UltraCanvasVideoRecorder.h`

```cpp
VideoCaptureConfig cfg;
cfg.outputPath = "out.mp4";
cfg.codec = VideoCodec::H264;
auto r = std::make_shared<UltraCanvasVideoRecorder>(cfg);
r->Open();    // starts the live preview
r->Start();   // begins muxing to outputPath
// ...
r->Stop();    // finalizes the file
```

`Open` starts a preview-only pipeline; `Start/Pause/Resume/Stop` gate a valved
record branch so the file contains only footage between `Start()` and `Stop()`.
Preview frames: `GetPreviewFrame()`.
Callbacks: `onRecordingStateChanged`, `onPreviewFrame`, `onSaved`,
`onMaxDurationReached`, `onError`, `onPermissionChanged`.

### UI elements

`UltraCanvasVideoPlayerElement` — video surface (aspect-fit letterbox) with an
auto-hiding overlay transport bar: play/pause, seek, mute, volume, time labels.
`ShowOpenDialog()` opens the native file picker.

`UltraCanvasVideoRecorderElement` — live camera preview with a REC/stop button,
elapsed-time readout, blinking record indicator, and a camera picker popup.
`OpenCamera()`, `StartRecording()/StopRecording()`, `ShowSaveDialog()`.

```cpp
auto player = CreateVideoPlayer("player", 10, 10, 640, 400);
player->LoadFromFile("clip.mp4");
player->Play();

auto rec = CreateVideoRecorder("rec", 10, 10, 640, 420);
rec->OpenCamera();
rec->ShowSaveDialog();   // pick a path, then records to it
```

### Devices — `UltraCanvasVideoDevices.h`

`ListCameras()`, `GetDefaultCamera()`, `GetCameraPermission()`,
`RequestCameraPermission()`, `GetBackendName()`, `IsAvailable()`.

## Build

```
cmake -DULTRACANVAS_ENABLE_VIDEO=ON ...
```

On Debian/Ubuntu, the GStreamer backend needs:

```
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
                 gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
```

(`-plugins-bad`/`-libav` provide H.264/H.265 encode + MP4 mux for recording.)

## Notes / follow-ups

- **Linux:** recording AAC audio uses `voaacenc`; swap to `avenc_aac`/`faac` if
  that element isn't present in the local GStreamer install. Hardware-accelerated
  decode happens transparently when GStreamer selects a VA-API / NVDEC / d3d11
  decoder.
- **Windows (Media Foundation):** the C++ backend links
  `mf mfplat mfreadwrite mfuuid ole32 shlwapi`. Recording aggregates the camera
  and microphone into one `IMFMediaSource` (`MFCreateAggregateSource`) so a
  single `IMFSourceReader` yields A/V samples on one synchronized timeline,
  which an `IMFSinkWriter` encodes to H.264 video + AAC audio in an MP4. Set
  `VideoCaptureConfig::captureAudio = false` for a video-only file.
- **macOS (AVFoundation):** the backend is Objective-C++ (`.mm`, built with
  `-fobjc-arc`) and links `AVFoundation CoreMedia CoreVideo Foundation`.
  `AVCaptureMovieFileOutput` records video **and** audio. Camera access requires
  an `NSCameraUsageDescription` (and `NSMicrophoneUsageDescription`) entry in the
  app's Info.plist.
- All three backends report frames as `VideoPixelFormat::BGRA32`, matching
  Cairo's `ARGB32` byte layout, so the UI uploads them without a swizzle.
