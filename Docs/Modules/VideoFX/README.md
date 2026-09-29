# VideoFX

**Video editing and conversion for UltraCanvas applications.**
Sibling of `UltraCanvas` (UI), `VirtualFS` (archives) and `UltraNet`
(networking). Sources in `VideoFX/`, header `<VideoFX/VideoFX.h>`,
`namespace VideoFX`.

VideoFX reads a video, shows you what is in it, and writes a new one: cut a
range, change its speed, grade it, fade it, join it with other clips of any
size, and encode the result as MP4, WebM, MOV, MKV, an animated GIF or an
audio file. One call, `VideoFX_Export`, does all of it from a list of
segments.

> **Status: stages 1 and 2 implemented, plus photos.** Probe, frames, the
> segment timeline with 26 effects, speed, joins, 30 transitions between
> segments, text and image overlays, still images with smooth pan and zoom
> ("Ken Burns") and one-call slideshows, GIF and audio-only output, lossless
> cut, the background job and the `videofx` command-line tool work today, on
> FFmpeg 4.4 to 8.x. Picture-in-picture, keyframes, multi-track mixing and
> project files are planned (see *Roadmap*).

---

## What it is, and what it is not

VideoFX is **headless**: no window, no widget, no UltraCanvas UI dependency.
It is the engine behind an editor, a converter or a batch job.

Playing video in the UI stays in the core — `UltraCanvasVideoPlayer`,
`UltraCanvasVideoRecorder` and `UltraCanvasVideoThumbnail` run on each
platform's own media framework (GStreamer, Media Foundation, AVFoundation);
see `Docs/UltraCanvas/UltraCanvasVideo.md`. VideoFX is for changing video and
writing files.

FFmpeg does the decoding, filtering and encoding, entirely behind VideoFX's
own types: no FFmpeg header reaches your code, so the engine can be replaced
without touching callers.

---

## Quick start

```cpp
#include <VideoFX/VideoFX.h>
using namespace VideoFX;

// What is in the file?
VideoFXMediaInfo info;
if (VideoFX_Probe("holiday.mp4", info) != VideoFXResult::Ok) {
    std::cerr << VideoFX_GetLastError() << "\n";
    return;
}
std::cout << info.width << "x" << info.height << ", " << info.duration << " s\n";

// Seconds 10 to 25, warmer, faded in and out, 720p MP4
VideoFXSegment clip = VideoFXSegment::FromFile("holiday.mp4", 10.0, 25.0);
clip.effects = { VideoFXEffect::Temperature(0.3),
                 VideoFXEffect::FadeIn(1.0),
                 VideoFXEffect::FadeOut(1.0) };

VideoFX_Export({ clip }, "cut.mp4", VideoFXExportSettings::WebMP4(720),
               [](double fraction) {
                   std::cout << int(fraction * 100) << "%\r";
                   return true;                // false cancels
               });
```

Every blocking call returns a `VideoFXResult`; on anything but `Ok`,
`VideoFX_GetLastError()` says why ("No H.265 encoder in this build",
"Brightness must be -1..1", "File not found: x.mp4", ...).

---

## Inspecting media

| Call | Gives you |
|---|---|
| `VideoFX_Probe(path, info)` | Container, duration, size, tags, every stream (codec, size, rate, rotation, sample rate, channels, language) |
| `VideoFX_ExtractFrame(path, seconds, frame, maxW, maxH)` | The frame shown at that time as RGBA, upright, scaled to fit |
| `VideoFX_ExtractThumbnails(path, count, frames, maxW, maxH)` | `count` frames spread over the whole video — a filmstrip |
| `VideoFX_SaveFrameImage(frame, "shot.png")` | The frame as PNG or JPEG |

`VideoFXMediaInfo::width` / `height` are the *displayed* size: a phone video
stored landscape with a 90° rotation flag reports portrait, and frames come
out upright.

```cpp
std::vector<VideoFXFrame> strip;
VideoFX_ExtractThumbnails("holiday.mp4", 10, strip, 160, 90);
for (const VideoFXFrame& f : strip) {
    // f.pixels is width*height*4 bytes of RGBA
}
```

---

## The timeline: segments

An export is a list of `VideoFXSegment`s played one after another.

```cpp
std::vector<VideoFXSegment> timeline;

timeline.push_back(VideoFXSegment::SolidColor(0x000000, 1.0));   // 1 s black

VideoFXSegment intro = VideoFXSegment::FromFile("a.mp4", 0.0, 8.0);
intro.effects = { VideoFXEffect::FadeIn(0.5) };
timeline.push_back(intro);

VideoFXSegment slowmo = VideoFXSegment::FromFile("b.mov", 30.0, 34.0);
slowmo.speed = 0.5;                        // 4 s of source -> 8 s on screen
slowmo.mute = true;                        // silence instead of its sound
timeline.push_back(slowmo);

timeline.push_back(VideoFXSegment::FromFile("c.mp4"));           // whole file

VideoFX_Export(timeline, "story.mp4");
```

| Field | Meaning |
|---|---|
| `start`, `end` | Range of the source in seconds (`end` 0 = to the end) |
| `speed` | 0.25 to 4; audio keeps its pitch |
| `mute` | Replace the segment's sound with silence |
| `effects` | Applied in order, to this segment only |
| `SolidColor(rgb, s)` / `TestPattern(s)` | Generated clips: a colour card, a moving test pattern with a 1 kHz tone |

Segments may differ in everything: size, aspect ratio, frame rate, codec,
sample rate, channel count, with or without sound. Each is fitted to the
output (see `fitMode`) and picture and sound stay in sync across every join —
a stream that runs short is padded before the next segment, video by holding
its last frame, audio with silence. A file without a picture shows black for
its length in a video export.

---

## Photos and slideshows

A still image is a segment too: `VideoFXSegment::FromImage(path, seconds,
motion)` — or `FromImageFrame(rgba, ...)` for pixels in memory, such as a
picture UltraCanvas rendered. It takes transitions, effects and overlays like
any other segment. A photo's EXIF orientation is honoured, so a phone
picture taken upright comes out upright (in `VideoFX_ExtractFrame` too).

```cpp
std::vector<VideoFXSegment> story = {
    VideoFXSegment::FromImage("beach.jpg", 5.0),                         // Auto motion
    VideoFXSegment::FromImage("sunset.jpg", 5.0, VideoFXImageMotion::Make(VideoFXMotionStyle::ZoomOut)),
    VideoFXSegment::FromFile("waves.mp4", 12.0, 20.0),                   // mixed with video
};
story[1].transitionIn = VideoFXTransition::Crossfade(1.0);
VideoFX_Export(story, "holiday.mp4", VideoFXExportSettings::WebMP4(1080));
```

The **motion** (`VideoFXImageMotion`) is a slow camera move across the photo:

| `VideoFXMotionStyle` | Move |
|---|---|
| `Auto` (default) | A different move per segment — zooms in and out, and pans along the direction the frame crops: sideways along a panorama, up and down a portrait photo or a 4:3 photo in a 16:9 frame |
| `ZoomIn`, `ZoomOut` | Centred, 1 to 1.25 and back |
| `PanLeft`, `PanRight`, `PanUp`, `PanDown` | Across the whole photo at zoom 1.2 |
| `Custom` | `VideoFXImageMotion::Custom(startZoom, startX, startY, endZoom, endX, endY)`: zoom 1..4, centres as 0..1 fractions of the photo |
| `Still` | No motion |

**Framing** (`imageFit`, on the segment and in `VideoFXSlideshowOptions`)
decides what happens when a photo's shape differs from the frame's:

| `VideoFXImageFit` | Framing |
|---|---|
| `Auto` (default) | `Cover`, except for a photo much taller than the frame — a portrait in a 16:9 video — which gets `BlurredBackground` instead of losing two thirds of itself. A 4:3 or 3:2 photo still fills the frame, and a panorama still pans |
| `Cover` | Fill the frame, crop the overflow |
| `Contain` | The whole photo, black bars |
| `BlurredBackground` | The whole photo, the bars filled with a blurred, darkened, enlarged copy of it |

With `Cover`, zoom 1 shows the largest part of the photo that fills the frame
and the view never leaves the photo. With `Contain` and `BlurredBackground`
zoom 1 shows the whole photo, and the motion moves the sharp photo in front
of its backdrop. Moves start and end gently (`easeInOut`), and the
zoom runs at a steady-looking speed. Every frame is resampled from the photo
at sub-pixel positions, so the camera glides — without the stepping FFmpeg's
own `zoompan` shows. A large photo is shrunk once to what the closest zoom
needs, and each photo is loaded only when its segment plays.

**A slideshow in one call:**

```cpp
VideoFXSlideshowOptions options;
options.secondsPerImage = 4.0;                                  // transitions included
options.transition = VideoFXTransition::Crossfade(1.0);         // default; Cut for none
options.captions = { "Arrival", "", "The old town" };           // optional, per image
VideoFX_CreateSlideshow({ "a.jpg", "b.jpg", "c.png" }, "trip.mp4", options);
```

It fades in from and out to black (`fadeInOut`), gives each photo its own
`Auto` move, shows portrait photos whole on a blurred background, and without a size in the settings makes 1920x1080 at 30 fps.
Length: `n x secondsPerImage - (n - 1) x transition`.

---

## Effects

Effects are typed values made by the `VideoFXEffect::` factories; VideoFX
turns them into a filter graph and checks every value, so a bad one fails
with `InvalidArgument` before anything is written.

| Colour | Detail | Geometry | Time | Audio |
|---|---|---|---|---|
| `Brightness(-1..1)` | `Blur(radius)` | `Rotate90()` | `FadeIn(seconds)` | `Volume(gain)` |
| `Contrast(-1..1)` | `Sharpen(0..3)` | `Rotate180()` | `FadeOut(seconds)` | `NormalizeAudio(lufs)` |
| `Saturation(-1..1)` | `Denoise(0..20)` | `Rotate270()` | | |
| `Gamma(0.1..10)` | `Vignette(0..1)` | `Rotate(degrees)` | | |
| `Exposure(stops)` | | `FlipHorizontal()` | | |
| `Hue(degrees)` | | `FlipVertical()` | | |
| `Temperature(-1..1)` | | `Crop(x, y, w, h)` | | |
| `Grayscale()`, `Sepia()`, `Invert()` | | | | |
| `LUT(path, strength)` | | | | |

- `FadeIn` / `FadeOut` fade picture *and* sound. `FadeOut` needs the
  segment's length; for a file whose duration cannot be read it is skipped.
- `LUT` takes a `.cube` / `.3dl` 3D LUT; `strength` below 1 mixes the graded
  picture with the original.
- `NormalizeAudio` is EBU R128 loudness: -23 LUFS for broadcast, about -14
  to -16 for web video.
- `Crop` and the quarter turns change the frame shape, and the first
  segment's shape decides the output's when `width` / `height` are not set.

---

## Transitions

A segment's `transitionIn` blends the previous segment into it. The two
overlap by the transition's length, so the export is that much shorter, and
their sound is cross-faded over the same span.

```cpp
VideoFXSegment a = VideoFXSegment::FromFile("a.mp4");
VideoFXSegment b = VideoFXSegment::FromFile("b.mp4");
b.transitionIn = VideoFXTransition::Crossfade(1.0);             // a -> b, 1 s

VideoFXSegment c = VideoFXSegment::FromFile("c.mp4");
c.transitionIn = VideoFXTransition::Make(VideoFXTransitionType::WipeLeft, 0.5);

VideoFX_Export({ a, b, c }, "joined.mp4");   // length: a + b + c - 1.5 s
```

| Family | `VideoFXTransitionType` |
|---|---|
| Blends | `Crossfade`, `Dissolve`, `FadeThroughBlack`, `FadeThroughWhite`, `Blur`, `Distance`, `Pixelize` |
| Wipes | `WipeLeft`, `WipeRight`, `WipeUp`, `WipeDown`, `DiagonalTopLeft`, `DiagonalTopRight`, `DiagonalBottomLeft`, `DiagonalBottomRight` |
| Pushes | `SlideLeft`, `SlideRight`, `SlideUp`, `SlideDown`, `SmoothLeft`, `SmoothRight`, `SmoothUp`, `SmoothDown`, `SqueezeHorizontal`, `SqueezeVertical` |
| Shapes | `CircleOpen`, `CircleClose`, `CircleCrop`, `RectCrop`, `Radial` |

- `Cut` (the default) is no transition. The first segment's `transitionIn`
  is ignored — fade a start in with the `FadeIn` effect instead.
- Length 0.04 to 5 seconds. When either neighbour is shorter than that, the
  overlap shrinks to the shorter one.
- The frames of the overlap are held in memory at output size: a 1 s
  transition at 1080p30 holds about 180 MB for its duration.
- Transitions need FFmpeg 4.3 or newer (its `xfade` filter); they work in
  every output, GIF and audio-only included.

---

## Overlays: titles and logos

`VideoFXSegment::overlays` draws text and images over a segment, after its
effects, on the **output** frame — a title sits in the same place whatever
the source's size. Sizes and margins are fractions of the output height, so
a layout looks the same at 480p and 4K.

```cpp
VideoFXSegment clip = VideoFXSegment::FromFile("talk.mp4");

VideoFXOverlay title = VideoFXOverlay::Text("Opening keynote", VideoFXAnchor::Bottom, 0.07);
title.box = true;                    // a dark band behind it
title.start = 0.5;                   // seconds into the segment
title.end = 5.0;
title.fadeIn = 0.5;
title.fadeOut = 0.5;

VideoFXOverlay logo = VideoFXOverlay::Image("logo.png", VideoFXAnchor::TopRight, 0.10);
logo.opacity = 0.8;

clip.overlays = { title, logo };
```

| Field | Meaning |
|---|---|
| `anchor`, `margin` | One of nine positions, `margin` from the edge (fraction of height); `Custom` uses `x`, `y` (fractions of width / height) |
| `start`, `end` | Segment seconds after speed; `end` 0 = until the segment ends |
| `fadeIn`, `fadeOut`, `opacity` | Seconds; 0..1 |
| **Text:** `text`, `fontSize`, `textColor`, `fontPath`, `shadow`, `box`, `boxColor`, `boxOpacity` | UTF-8, `\n` for lines; size as a fraction of height; empty `fontPath` = the default font (below) |
| **Image:** `imagePath` or `image`, `imageHeight` | A PNG keeps its transparency; `image` takes RGBA pixels from memory (for example a picture UltraCanvas rendered); `imageHeight` 0 = its own pixel size |

Text is taken literally — `:`, quotes, `%` and backslashes need no escaping.

**Which font.** A text overlay with no `fontPath` uses the default font,
chosen in this order:

1. the one the application set with `VideoFX_SetDefaultFontPath(path)`;
2. the framework's bundled **Ubuntu** font (`media/fonts/Ubuntu-R.ttf`),
   found next to the running application wherever UltraCanvas apps ship
   `media/` — `share/media/fonts` (Linux build and package),
   `Resources/media/fonts` (Windows), `Contents/Resources/media/fonts`
   (macOS) — so every UltraCanvas application renders titles the same way on
   every machine;
3. a common system sans font (DejaVu, Liberation, Noto, FreeSans on Linux;
   Helvetica on macOS; Segoe UI on Windows);
4. fontconfig's "Sans" — only if this FFmpeg can actually load it, which is
   checked once.

When none of them works, an export with text fails before it starts, with
`NotAvailable` and a message saying to set a font — never halfway through.
An application that does not ship `media/` should bundle a font of its own
and pass it once:

```cpp
VideoFX_SetDefaultFontPath(appDir + "/fonts/BrandSans.ttf");   // false if missing
std::string font = VideoFX_GetDefaultFontPath();               // what will be used
```
Text overlays need an FFmpeg built with libfreetype (the usual Linux, macOS
and MSYS2 packages are); ask `VideoFX_IsTextOverlayAvailable()`.

---

## Export settings

`VideoFXExportSettings` defaults to "keep the first segment's size and rate,
the container's usual codecs, sensible quality". Set only what matters:

| Field | Default | Notes |
|---|---|---|
| `container` | `Auto` | From the extension: `.mp4 .mov .mkv .webm .avi .gif .mp3 .m4a .wav .flac .ogg` |
| `videoCodec` | `Auto` | H.264 (MP4/MOV/MKV), VP9 (WebM), MPEG-4 (AVI), GIF; `Disabled` drops video |
| `audioCodec` | `Auto` | AAC (MP4/MOV/MKV), Opus (WebM), MP3, PCM (WAV), FLAC; `Disabled` drops sound |
| `width`, `height` | first segment | Give one and the other follows the aspect ratio |
| `frameRate` | first segment | 30 for generated clips, 12 for GIF |
| `fitMode` | `Letterbox` | `Fill` crops to cover, `Stretch` distorts |
| `quality` | codec default | 0..100 → CRF / qscale per encoder |
| `videoBitRate`, `audioBitRate` | 0 | Set to use a bit rate instead of quality |
| `encoderPreset` | — | e.g. `"veryfast"` for x264 / x265 |
| `sampleRate`, `channels` | first audio source / stereo | Mono only when every source is mono |

Presets: `WebMP4(height)`, `WebM(height)`, `AnimatedGif(width, fps)`,
`MasterProRes()`, `AudioOnlyMP3(bitRate)`, `AudioOnlyWAV()`.

Which encoders exist depends on the FFmpeg build — ask before offering a
choice in the UI:

```cpp
if (!VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::H265))
    h265Option->SetDisabled(true);
```

H.264 uses libx264, then the platform encoder (VideoToolbox on macOS, Media
Foundation on Windows), then OpenH264; H.265, AV1 and ProRes likewise try the
software encoder first.

---

## One-line helpers

All of these are single-segment timelines over `VideoFX_Export`:

| Call | Does |
|---|---|
| `VideoFX_Transcode(in, out, settings)` | Re-encode a whole file |
| `VideoFX_Trim(in, out, start, end, settings)` | Frame-accurate cut, re-encoded |
| `VideoFX_ApplyEffects(in, out, effects, settings)` | Effects on a whole file |
| `VideoFX_Concatenate({a, b, c}, out, settings)` | Join files end to end |
| `VideoFX_ExtractAudio(in, "sound.mp3")` | Sound only |
| `VideoFX_GenerateTestClip(out, seconds, w, h, fps)` | Test pattern + tone |

And one that is not:

| Call | Does |
|---|---|
| `VideoFX_TrimLossless(in, out, start, end)` | Cut by copying the compressed streams: no quality loss and very fast, but the cut starts at the keyframe at or before `start` (often a few seconds earlier) |

---

## Running it from a UI

Every call blocks until the file is written. In an application, run it on a
worker thread — `VideoFXExportJob` does that and is polled from a timer:

```cpp
auto job = std::make_shared<VideoFXExportJob>();
job->Start(timeline, "story.mp4", VideoFXExportSettings::WebMP4(1080));

// in a UI timer:
progressGauge->SetValue(job->GetProgress() * 100.0);
if (!job->IsRunning()) {
    if (job->GetResult() == VideoFXResult::Ok) ShowDone();
    else ShowError(job->GetError());
}

// Cancel button:
job->Cancel();
```

A cancelled or failed export deletes its partial output file.

---

## Command-line tool

The build also produces `videofx`, a thin front end for scripts and for
trying the engine:

```
videofx info clip.mp4
videofx frame clip.mp4 12.5 shot.png 640 360
videofx trim clip.mp4 cut.mp4 10 25
videofx trim clip.mp4 cut.mp4 10 25 --lossless
videofx effects clip.mp4 graded.mp4 temperature=0.3 vignette=0.4 fadein=1 fadeout=1
videofx transcode clip.mov clip.webm --height 720 --quality 70
videofx concat all.mp4 a.mp4 b.mov c.mkv
videofx concat all.mp4 a.mp4 b.mov c.mkv --transition crossfade:1
videofx transcode talk.mp4 titled.mp4 --title "Opening keynote" --watermark logo.png
videofx transcode talk.mp4 titled.mp4 --title "Opening keynote" --font BrandSans.ttf
videofx testclip pattern.mp4 5 1280 720 30
videofx slideshow trip.mp4 a.jpg b.jpg c.png --seconds 4 --caption Arrival --caption "" --caption "Old town"
videofx slideshow trip.mp4 *.jpg --motion zoomin --transition dissolve:1.5
videofx slideshow trip.mp4 *.jpg --fit blur              # every photo whole, on its blurred copy
```

---

## Building

VideoFX is always built (`-DULTRACANVAS_USE_VIDEOFX=ON`, the default). When
pkg-config finds FFmpeg's development files the engine is on; otherwise the
same API is built from a stub, `VideoFX_IsAvailable()` returns false and every
call returns `VideoFXResult::NotAvailable` — applications need no `#ifdef`.

| Platform | Install |
|---|---|
| Ubuntu / Debian | `sudo apt install libavformat-dev libavcodec-dev libavfilter-dev libswscale-dev libavutil-dev` |
| macOS | `brew install ffmpeg` |
| Windows (MSYS2) | `pacman -S mingw-w64-clang-x86_64-ffmpeg` |

Link `VideoFX::VideoFX`. FFmpeg stays a private dependency of the library.
The test suite is `Tests/VideoFXTest.cpp` (`VideoFXTest` under ctest); it
generates its own clips, so it needs no media files.

---

## Roadmap

| Stage | Contents | State |
|---|---|---|
| 1 | Probe, frames, segment timeline, 26 effects, speed, joins, GIF / audio outputs, lossless cut, job, CLI | **Done** |
| 2 | Transitions between segments (30 types, with audio cross-fade), text and image overlays | **Done** |
| 2b | Still images with pan and zoom, EXIF orientation, one-call slideshows | **Done** |
| 3 | Picture-in-picture, keyframed effect and overlay parameters, multi-track audio mixing (music bed, ducking) | Planned |
| 4 | Project files, proxy media, explicit hardware encoder choice (NVENC, QuickSync, VAAPI) | Planned |
| 5 | A timeline editor element in UltraCanvas on top of the engine | Planned |

*VideoFX — part of the UltraCanvas framework.*
