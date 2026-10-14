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

> **Status: stages 1 and 2 implemented, plus photos and music.** Probe,
> frames, the segment timeline with 26 effects, speed, joins, 30 transitions
> between segments, text, image and video overlays (picture-in-picture),
> still images with smooth pan and
> zoom ("Ken Burns"), one-call slideshows, background music that dips under
> speech, project files, waveforms for timelines and players, a preview
> that renders any frame of a timeline without an export, proxy media for
> smooth editing of large clips, hardware encoders (NVENC, Quick Sync,
> VA-API, AMF, VideoToolbox, Media Foundation) by choice, voice-overs and
> sound effects placed at any time, GIF and
> audio-only output, lossless cut, the background job and the `videofx`
> command-line tool work today, on FFmpeg 4.4 to 8.x.
> Keyframed parameters are planned (see *Roadmap*).

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
| `VideoFX_ExtractWaveform(path, waveform, blocksPerSecond, start, end)` | The sound's loudness envelope, block by block — what a timeline or a player draws as the waveform |

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

**Waveforms.** `VideoFX_ExtractWaveform` mixes a file's sound — a song, or a
video's soundtrack — to mono and cuts it into blocks of equal length, giving
the lowest sample, the highest sample and the RMS of each (`VideoFXWaveform`:
`minValues`, `maxValues`, `rmsValues`, `samplesPerBlock`, `sampleRate`,
`start`, `duration`). Fifty blocks a second, the default, draws a clip at
any zoom a timeline shows and holds a whole three-minute song in some 100 KB.
Its member names match `UltraCanvasWaveformElement`'s `WaveformChannelData`,
so an UltraCanvas application feeds it straight in:

```cpp
VideoFXWaveform envelope;
if (VideoFX_ExtractWaveform("holiday.mp4", envelope) == VideoFXResult::Ok)
    waveformElement->SetFromAudioFX(envelope);          // the element's field-by-field adapter

VideoFX_ExtractWaveform("song.mp3", envelope, 50.0, 12.0, 20.0);   // seconds 12..20 only
int block = envelope.BlockAt(15.0);                   // which block holds 15 s; -1 outside
float peak = envelope.Peak();                         // the loudest sample, 0..1
```

The file is read from its start, so a range saves memory, not time. A file
without sound fails with `NoMediaStreams`; a range beginning after the sound
ends with `InvalidArgument`. `videofx waveform <file> [blocksPerSecond] [start [end]]`
prints the same, one block a line.

**Which files.** `VideoFX_GetFileFormats()` lists the files this build reads
and writes, asking the FFmpeg it is linked against rather than a list kept
by hand:

- Films and sound (`VideoFXFileKind::Video` / `Audio`) are the containers it
  knows by name: MP4, MOV, MKV, WebM, AVI, MPEG, transport streams, WMV, FLV,
  Ogg, 3GP, MP3, M4A, AAC, WAV, FLAC, Opus, WMA, AIFF and others. Each is
  `canRead` when FFmpeg has its demuxer, and `canWrite` when an export writes
  it: the muxer and the container's usual encoders are there. `container`
  then names the `VideoFXContainer` an export to it uses.
- Still pictures (`Picture`) are read when a decoder opens them. AVIF needs
  FFmpeg 6. FFmpeg is not asked to read HEIC: from FFmpeg 7.0 its demuxer
  opens it, but a phone's HEIC is a grid of tiles in a stream group, which
  VideoFX does not put together. A host application's picture reader does
  (below). GIF is also written, as the animation.

The descriptions match the UltraCanvas codec registry's, so an application
that brings VideoFX hands the films and sound to `RegisterMediaCodec`, and
FileLoader's lists, and the dialogs built from them, then offer exactly what
the engine handles. UltraVideo does this (`UltraVideoFormats.cpp`). The
module built without FFmpeg returns an empty list.

```cpp
for (const VideoFXFileFormat& f : VideoFX_GetFileFormats())
    if (f.kind == VideoFXFileKind::Video && f.canWrite)
        std::printf("%s (%s) - an export writes it\n", f.description.c_str(), f.extension.c_str());
```

**Pictures the application reads.** A host with an image library of its own
hands VideoFX a `VideoFXPictureReader`: the picture types it takes over, a
cheap `size` (the upright size, from the header) and `decode` (the upright
picture as RGBA). `VideoFX_Probe`, `VideoFX_ExtractFrame` and
`VideoFX_ExtractThumbnails` read files of those types through it instead of
FFmpeg, and so does everything built on them: image segments, picture
overlays, slideshows. `VideoFX_GetFileFormats` then reports the types read.
Both calls come from the engine's worker threads. UltraVideo gives VideoFX
the UltraCanvas image reader (libvips, libheif) this way, for every picture
type FileLoader reads and FFmpeg does not: a phone's HEIC comes in whole and
upright, and so does a camera's raw file.

```cpp
VideoFXPictureReader reader;
reader.extensions = {"heic", "heif"};
reader.size = [](const std::string& path, int& w, int& h) { return MyImageSize(path, w, h); };
reader.decode = [](const std::string& path, VideoFXFrame& frame) { return MyImageRgba(path, frame); };
VideoFX_SetPictureReader(reader);             // a reader with no extensions takes it away
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

## Previewing a timeline

An editor's viewer needs the *output* frame at a time of the timeline - the
segment playing then, trimmed, sped, filtered and fitted to the output size,
its overlays drawn, and inside a transition's overlap the two segments
blended by that transition - without exporting anything. `VideoFXPreview`
does that: `Open` plans the timeline once, as an export does (every file
probed, the output size and rate decided from the settings), and
`RenderFrame` then costs one frame - a seek and a decode of the one source
playing, and the segment's filter chain over that single frame. Music is
ignored; the preview is picture only.

```cpp
VideoFXPreview preview;
if (preview.Open(timeline, VideoFXExportSettings::WebMP4(1080)) != VideoFXResult::Ok)
    Show(VideoFX_GetLastError());

double duration = preview.GetDuration();          // the transitions' overlaps subtracted
double into = 0.0;
int segment = preview.SegmentAt(12.5, &into);     // which segment plays at 12.5 s, and how far into it

VideoFXFrame frame;                               // RGBA, no larger than the viewer
if (preview.RenderFrame(12.5, frame, viewerWidth, viewerHeight) == VideoFXResult::Ok)
    viewer->SetImage(frame.pixels, frame.width, frame.height);
```

A viewer scrubbing keeps one `VideoFXPreview` open and calls `RenderFrame`
for every position; after an edit it calls `Open` again with the new
timeline. `GetWidth` / `GetHeight` / `GetFrameRate` are what the export
would produce. `RenderFrame` fails with `InvalidArgument` past the end or
before the start. The frame's `timestamp` is the timeline time asked for.

For a single picture - a thumbnail of a project, a poster frame -
`VideoFX_RenderFrame(segments, seconds, frame, settings, maxW, maxH)` does
the three steps in one call. `VideoFX_PlanSlideshow(imagePaths, options,
settings, segments, settingsOut)` gives the segments and settings a
slideshow would be exported from - one image segment per photo with its
seconds, motion, framing, regions kept in view, transition and caption, the
fades at the ends, and the size, rate and music in `settingsOut` - so a
slideshow can be previewed, or opened on a timeline to be changed, before
`VideoFX_CreateSlideshow` renders it.

```cpp
std::vector<VideoFXSegment> show;
VideoFXExportSettings showSettings;
VideoFX_PlanSlideshow(photos, options, VideoFXExportSettings::WebMP4(1080), show, showSettings);
VideoFX_RenderFrame(show, 3.0, frame, showSettings, 320, 180);   // the show at 3 s, viewer sized
```

`videofx preview <project.vfxproj> <seconds> <out.png|out.jpg> [maxWidth maxHeight]`
writes the frame of a saved project.

---

## Proxy media

A 4K phone clip is slow to scrub: every seek decodes a large frame, often
from a keyframe seconds back. A **proxy** is a small stand-in for it - the
same picture and sound, timed the same, at 540 lines, as H.264 with a
keyframe every half second. The editor previews and plays the timeline from
the proxies, and the export reads the originals.

```cpp
VideoFXProxySettings proxies;                     // 540 lines, the user's cache folder

// once, in the background (a job of the app's own, or a worker thread)
VideoFX_CreateProxies(timeline, proxies, [&](double f) { gauge->SetValue(f * 100.0); return !cancelled; });

// the viewer plays the proxies...
preview.Open(VideoFX_UseProxies(timeline, proxies), settings);

// ...the export reads the originals
VideoFX_Export(timeline, "final.mp4", settings);
```

- **Where they live.** In a cache folder: `VideoFXProxySettings::cacheDirectory`,
  or by default `VideoFX_GetDefaultProxyDirectory()` - `$XDG_CACHE_HOME`
  (else `~/.cache`) on Linux, `~/Library/Caches` on macOS and
  `%LOCALAPPDATA%` on Windows, each followed by `VideoFX/Proxies`.
- **Named after the clip.** A proxy's name is the clip's own name plus a hash
  of its full path, size and modification time and of the proxy settings,
  for example `Trip_2026-3f9a0c41d27be805.mp4`. A clip that is edited or replaced
  gets a new proxy, and the stale one is simply never used again.
  `VideoFX_ProxyPath(clip)` says where a clip's proxy is, and
  `VideoFX_HasProxy(clip)` whether it has been made.
- **Made once.** `VideoFX_CreateProxy(clip, settings, progress, &editPath)`
  skips a proxy that exists. A clip with no picture, or one no taller than
  a proxy, gets none: `editPath` is then the clip itself.
  `VideoFX_CreateProxies(timeline)` makes them for every file segment and
  every sized picture-in-picture video, each clip once, with one progress
  for the lot.
- **Never half made.** A proxy is written under a temporary name and renamed
  when complete. A crash, a full disk or a cancel leaves nothing that could
  be taken for a whole proxy.
- **Switching.** `VideoFX_UseProxies(timeline)` returns the timeline with
  every clip that has a proxy replaced by it; trims, speed, effects and
  overlays are kept. A `Crop` effect, which is in the clip's pixels, is
  scaled to the proxy's. A picture-in-picture video at its own pixel size
  (`imageHeight` 0) keeps its original, since a proxy would shrink it.
- **Give the output size.** With `width` and `height` 0 an export or preview
  takes its size from the first segment, which over proxies is the proxy.
  Set the size in the settings, as a project does, so the preview frame is
  the export's.
- **Clearing.** `VideoFX_ClearProxies(settings, &files, &bytes)` deletes
  every proxy in the cache folder, and any half-made one, and touches
  nothing else there.

Proxies keep the clip's timing to the frame, so a trim made on a proxy is
the same trim on the original. Keyframes come from the new export setting
`VideoFXExportSettings::keyframeInterval` (seconds, 0 = the encoder's
default), which any export can use.

From the command line:

```
videofx proxy clip1.mov clip2.mov                  # make proxies; prints each one's path
videofx proxy trip.vfxproj                         # for every clip of a project
videofx proxy clip.mov --proxy-height 360 --proxy-dir ~/proxies
videofx preview trip.vfxproj 12.5 shot.png --proxies   # the frame, from the proxies
videofx render trip.vfxproj draft.mp4 --proxies    # a quick draft from the proxies
videofx proxy --clear                              # delete them all
```

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

**Keeping faces in shot.** A pan or a zoom can carry a face out of frame —
a zoom into the middle of a group photo, a pan down a portrait that crops
heads off. `keepInView` lists the regions that must stay in shot, as
fractions of the photo as it is shown (after its EXIF rotation):

```cpp
VideoFXSegment photo = VideoFXSegment::FromImage("family.jpg", 5.0);
photo.keepInView = { VideoFXRect::FromPixels(1210, 380, 240, 300, 4000, 3000),   // a face, in pixels
                     VideoFXRect::Make(0.62, 0.15, 0.06, 0.09) };               // or as fractions
```

The move keeps its character but is fitted around them. The zoom goes no
closer than the view that holds every region, with 15 % headroom round each,
and the pan is narrowed so it stays around them. Every frame is then slid,
never resized, to hold them, so the faces stay in shot through the whole move
and not just at its ends. A `Still` photo stays still but looks at them —
the band of a tall photo a `Cover` crop shows is the one with the faces.

**Faces found for you.** A slideshow finds the faces itself
(`keepFacesInView`, on by default), and a photo segment does when its
`keepFacesInView` is set. `VideoFX_DetectFaces(image, faces)` gives the
detector on its own — it works without FFmpeg too:

```cpp
std::vector<VideoFXRect> faces;
VideoFX_DetectFaces(photo, faces);              // fractions of the photo, largest first
```

It is a Viola-Jones detector running OpenCV's trained frontal-face model
(`haarcascade_frontalface_alt`, compiled in; VideoFX's own code evaluates it,
no OpenCV is linked) and finds what OpenCV's `CascadeClassifier` finds:
faces looking roughly towards the camera, from about 1/40 of the photo's
longer side upwards, in some 0.1–0.2 s a photo. Profiles, heads tilted far
over and faces behind sunglasses or masks are often missed. The model's
stray hits on clothing and textures are thinned out: in a colour photo a box
needs skin colour in its middle, and one that too few overlapping windows
agree on is dropped. A black and white photo cannot use the skin test, so an
occasional stray box survives there; it only makes the move frame a little
more of the picture. Set `keepFacesInView = false` for plain motion.

A **video clip** keeps its people the same way when the export's `fitMode`
is `Fill` (a portrait phone clip filling a 16:9 frame loses its sides, a
landscape clip in a portrait frame its top and bottom): `keepInView` on a
file segment places the crop window around the regions instead of the
picture's centre, and `keepFacesInView` finds them itself in one frame from
the middle of the clip's used range (found once per clip, whatever the
preview asks later). A clip with no face found keeps the centred crop. The
regions are fractions of the picture as the file shows it, before the
segment's effects. Under `Letterbox` and `Stretch` nothing is cropped and
both are ignored.

An application with a better detector plugs it in instead: UltraAI's vision
analyser (`VisionTask::FaceDetection`), the operating system's (Vision on
macOS, `Windows.Media.FaceAnalysis`), or a tap in its own UI. It is asked
once per photo that has no regions given, and wins over the built-in one:

```cpp
VideoFXSlideshowOptions options;
options.findKeepInView = [&](const VideoFXFrame& photo, size_t index) {
    std::vector<VideoFXRect> faces;
    for (const auto& box : myDetector.Faces(photo.pixels.data(), photo.width, photo.height))
        faces.push_back(VideoFXRect::FromPixels(box.x, box.y, box.w, box.h, photo.width, photo.height));
    return faces;
};
// or, known in advance: options.keepInView = { {faceA}, {}, {faceB, faceC} };
```

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
Length: `n x secondsPerImage - (n - 1) x transition` — or, with
`matchMusicLength`, the length of the music (see below).

---

## Background music

`VideoFXExportSettings::music` lays a song under the whole export — any
timeline, not only slideshows. It is mixed in last, so it plays straight
through joins and transitions, and where the segments have sound of their own
it steps back:

```cpp
VideoFXExportSettings settings = VideoFXExportSettings::WebMP4(1080);
settings.music = VideoFXMusic::FromFile("summer.mp3", 0.8);   // path, volume
settings.music.start = 12.0;          // begin 12 s into the song
settings.music.fadeIn = 1.0;          // seconds, at the start of the export
settings.music.fadeOut = 3.0;         // and at its end
settings.music.duckingLevel = 0.25;   // under the clips' own sound
VideoFX_Export(timeline, "holiday.mp4", settings);
```

| Field | Default | Meaning |
|---|---|---|
| `path` | — | Any file with sound: MP3, M4A/AAC, WAV, FLAC, OGG, Opus, or a video (its soundtrack) |
| `playlist` | — | Further songs, played after `path` in order (see *Song lists* below) |
| `crossfade` | 3 s | Seconds each song blends into the next, 0..30; 0 = back to back |
| `volume` | 0.8 | Linear gain, 0..4 |
| `start` | 0 | Seconds into the first song to begin at |
| `loop` | true | Repeat music shorter than the video (a list from its first song); `false` = silence after it ends |
| `fadeIn`, `fadeOut` | 1 s, 2 s | Over the start and end of the export |
| `duckingLevel` | 0.3 | Gain while the segments have sound of their own, 0..1; 1 = never dip |
| `duckingThresholdDb` | −36.5 | The segments' sound counts as present above this RMS level, dBFS, −90..0 |
| `duckingAttack` | 0.12 s | How fast the music goes down (time constant), 0.001..10 |
| `duckingHold` | 0.6 s | Quiet needed before it comes back up, 0..30 |
| `duckingRelease` | 0.8 s | How slowly it comes back (time constant), 0.001..30 |

**Ducking.** VideoFX listens to the segments' own sound block by block. While
it is present — someone speaking in a clip, a waterfall — the music goes down
to `duckingLevel` within about a tenth of a second; it waits for a pause of
over half a second before coming back up, slowly, so speech does not make it
pump between words. Photos and colour cards have no sound, so under a
slideshow the music plays at full `volume`.

Those defaults are tuned for **speech**. A clip that is loud the whole way
through — a concert, a street, the waterfall — stays above −36.5 dBFS, so the
music sits at `duckingLevel` for its entire length. Raise the threshold above
the clip's own background so only its louder moments duck, and shorten the
hold so the music returns between them:

```cpp
settings.music.duckingThresholdDb = -15.0;   // ignore a steady background below -15 dBFS
settings.music.duckingHold = 0.2;            // back up after a fifth of a second of calm
settings.music.duckingRelease = 0.4;
```

or set `duckingLevel = 1` to never duck at all. `videofx` takes the same as
`--duck-threshold DB --duck-attack S --duck-hold S --duck-release S`.

**Presets.** An app can offer one choice — the kind of footage — instead of
four numbers. `SetDuckingPreset` fills in the threshold and the times and
leaves `duckingLevel` alone; single fields set afterwards refine it:

```cpp
settings.music.SetDuckingPreset(VideoFXDuckingPreset::LoudEvent);
```

| `VideoFXDuckingPreset` | For | Threshold | Attack | Hold | Release |
|---|---|---|---|---|---|
| `Speech` (the defaults) | talking in quiet rooms | −36.5 dBFS | 0.12 s | 0.6 s | 0.8 s |
| `Outdoor` | talking over wind and traffic | −28 dBFS | 0.12 s | 0.5 s | 0.7 s |
| `LoudEvent` | crowds, engines, concerts: only the louder moments dip | −15 dBFS | 0.12 s | 0.2 s | 0.4 s |

`videofx` takes `--duck-preset speech|outdoor|loud`; the single `--duck-*`
values refine it whichever comes first on the command line.

**Song lists.** Several songs play one after another, each **crossfading**
into the next: over the last `crossfade` seconds of a song it fades out while
the next one fades in, so there is no hard cut and no gap of silence.

```
Song A  ██████████████▓▓▒▒░░
Song B                ░░▒▒▓▓██████████████
                      └─ 3 s ─┘
```

```cpp
settings.music = VideoFXMusic::FromFiles({"summer.mp3", "beach.mp3", "sunset.mp3"});
settings.music.crossfade = 4.0;       // seconds of overlap between songs
```

The blend is equal-power (cosine / sine gains), so the loudness holds steady
through it. A song shorter than twice the crossfade gets a shorter one — at
most half of either song — so no song is swallowed by its neighbours. A
looping list crossfades from its last song back into its first, and a single
looping song crossfades into its own start; set `crossfade = 0` for the old
back-to-back joins. Fades, volume and ducking apply to the whole bed, as for
one song. `videofx` takes a list as repeated `--music FILE` and the overlap
as `--music-crossfade S`.

**Slideshows to a song.** `VideoFXSlideshowOptions::music` sets the song for
`VideoFX_CreateSlideshow`, and `matchMusicLength` chooses the seconds per
photo so the show ends with it:

```cpp
VideoFXSlideshowOptions options;
options.music = VideoFXMusic::FromFile("song.mp3");
options.matchMusicLength = true;      // 3:20 of music, 40 photos: about 5.9 s each
VideoFX_CreateSlideshow(photos, "show.mp4", options);
```

With a song list the show lasts as long as the list plays once: the songs'
lengths minus the crossfades between them.

**On the beat.** `beatSync` moves every change — the cut, or the middle of a
transition — onto a beat of the music: the beat nearest `secondsPerImage`
after the change before, never less than a second (or two transitions)
later. `beatsPerImage` instead gives every image exactly that many beats;
4 is one bar of a song in 4/4. With a song list the beats come from each
song where it plays, handing over in the middle of each crossfade.

```cpp
VideoFXSlideshowOptions options;
options.music = VideoFXMusic::FromFile("song.mp3");
options.beatSync = true;              // changes on the beat, about every 4 s
options.beatsPerImage = 8;            // or: exactly two bars per photo
VideoFX_CreateSlideshow(photos, "show.mp4", options);
```

The beat finder is VideoFX's own and needs nothing beyond FFmpeg to decode.
It reads the sound's spectral flux (how sharply the spectrum rises — drum
hits, plucked notes), takes the tempo from the strongest repetition between
40 and 240 BPM (weighted towards 120 so a song is not read at half or double
speed), and picks the chain of onsets that best fits that tempo. It is tuned
for music with a steady pulse; for speech, noise or a held chord it reports
no beat, and the slideshow falls back to plain `secondsPerImage`.

`VideoFX_DetectBeats` gives the analysis itself:

```cpp
VideoFXBeatInfo beats;
if (VideoFX_DetectBeats("song.mp3", beats) == VideoFXResult::Ok && beats.HasBeat())
    std::printf("%.1f BPM, first beat at %.2f s\n", beats.bpm, beats.beats.front());
```

| `VideoFXBeatInfo` | Meaning |
|---|---|
| `bpm` | Tempo in beats per minute; 0 = no steady beat |
| `confidence` | 0..1, how clearly the sound repeats at that tempo |
| `beats` | Beat times in seconds from the start of the file |

Sound-only outputs work too: photos plus music into `.mp3` is a valid,
correctly timed (if unusual) export, and so is music under silent clips.

---

## Voice-overs and sound effects

Besides the segments' own sound and the music, an export can hold any
number of **free audio clips**: a voice-over, a sound effect, a sting. Each
is a sound file placed at a time on the timeline, independent of the cuts,
in `VideoFXExportSettings::audioClips`.

```cpp
VideoFXExportSettings s = VideoFXExportSettings::WebMP4(1080);
s.music = VideoFXMusic::FromFile("song.mp3");

VideoFXAudioClip voice = VideoFXAudioClip::VoiceOver("narration.wav", 2.0);   // at 2 s
voice.fadeOut = 0.5;
s.audioClips.push_back(voice);                                     // the music dips under it

VideoFXAudioClip whoosh = VideoFXAudioClip::Sound("whoosh.wav", 7.5, 0.8);    // at 7.5 s, 80 %
whoosh.start = 0.1;                                                // skip the file's first 0.1 s
whoosh.end = 0.9;                                                  // and stop at 0.9 s of it
s.audioClips.push_back(whoosh);                                    // over the music, not ducking it

VideoFX_Export(timeline, "story.mp4", s);
```

| Field | Default | Notes |
|---|---|---|
| `path` | - | A sound file, or a video whose sound is wanted |
| `at` | 0 | Seconds into the export where it starts |
| `start`, `end` | 0, 0 | The part of the file played; `end` 0 = to the file's end |
| `volume` | 1 | 0..4 |
| `fadeIn`, `fadeOut` | 0 | Seconds at the clip's own ends; fades longer than the clip share it |
| `ducksMusic` | false | The music dips under it as under the segments' sound (`VoiceOver` sets it) |

- **Mixing.** The clips are mixed with the segments' sound before the
  music is added. The ducker hears the voice-overs, so the music dips under
  narration exactly as it does under speech in a clip. Sound effects come
  in after the ducker, so a whoosh does not pull the music down.
- **Placement.** Clips may overlap each other and the cuts. A clip that
  runs past the end of the export is cut off there: the export is not
  lengthened.
- **Sound in a silent export.** A timeline of photos or silent clips with a
  voice-over gets a sound track.
- **Resources.** Each clip's file is opened when the timeline reaches it and
  let go when it has played, so a hundred sound effects do not keep a
  hundred files open.
- **Refused before anything is written:** a missing file (`FileNotFound`),
  one without sound (`NoMediaStreams`), and a start past the file's end or
  a value out of range (`InvalidArgument`).
- **Elsewhere.** The clips are saved in project files, play in slideshows
  (through the settings) and in drafts. The preview is picture only, and
  ignores them like the music.

From the command line, `--sound FILE@SECONDS[:VOLUME]` places a sound
effect and `--voice FILE@SECONDS[:VOLUME]` a voice-over. Both can be
repeated:

```
videofx concat story.mp4 a.mp4 b.mp4 --music song.mp3 --voice intro.wav@1.5 --sound whoosh.wav@8:0.8
```

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

## Overlays: titles, logos and picture-in-picture

`VideoFXSegment::overlays` draws text, images and other videos over a
segment, after its effects, on the **output** frame — a title sits in the
same place whatever the source's size. Sizes and margins are fractions of
the output height, so a layout looks the same at 480p and 4K.

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

VideoFXOverlay cam = VideoFXOverlay::Video("speaker.mp4", VideoFXAnchor::BottomRight, 0.3, 12.0);
cam.start = 5.0;                     // shown from 5 s into the segment
cam.fadeIn = 0.5;                    // playing speaker.mp4 from its 12 s on

clip.overlays = { title, logo, cam };
```

| Field | Meaning |
|---|---|
| `anchor`, `margin` | One of nine positions, `margin` from the edge (fraction of height); `Custom` uses `x`, `y` (fractions of width / height) |
| `start`, `end` | Segment seconds after speed; `end` 0 = until the segment ends |
| `fadeIn`, `fadeOut`, `opacity` | Seconds; 0..1 |
| **Text:** `text`, `fontSize`, `textColor`, `fontPath`, `shadow`, `box`, `boxColor`, `boxOpacity` | UTF-8, `\n` for lines; size as a fraction of height; empty `fontPath` = the default font (below) |
| **Image:** `imagePath` or `image`, `imageHeight` | A PNG keeps its transparency; `image` takes RGBA pixels from memory (for example a picture UltraCanvas rendered); `imageHeight` 0 = its own pixel size |
| **Video:** `videoPath`, `videoStart`, `imageHeight` | Picture-in-picture: the file's picture plays from `videoStart` seconds into it from the overlay's `start` on, at its own speed, upright, without its sound; when the file ends before the overlay does its last frame holds. `imageHeight` sizes it like an image (0 = the file's own pixels). `VideoFXOverlay::Video` makes one from a path, anchor, height and start second. The file is checked when the export is planned, so a missing one fails before anything is written. |

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
| `fitMode` | `Letterbox` | `Fill` crops to cover (around a segment's `keepInView` / its faces), `Stretch` distorts |
| `quality` | codec default | 0..100 → CRF / qscale per encoder |
| `videoBitRate`, `audioBitRate` | 0 | Set to use a bit rate instead of quality |
| `encoderPreset` | — | e.g. `"veryfast"` for x264 / x265 |
| `keyframeInterval` | encoder default | Seconds between keyframes; short = quick to seek (proxies use 0.5) |
| `acceleration` | `Auto` | Software, any hardware encoder (`Hardware`), or one family; see *Hardware encoders* |
| `sampleRate`, `channels` | first audio source / stereo | Mono only when every source is mono |
| `music` | none | Background music; see *Background music* |
| `audioClips` | none | Voice-overs and sound effects at given times; see *Voice-overs and sound effects* |

Presets: `WebMP4(height)`, `WebM(height)`, `AnimatedGif(width, fps)`,
`MasterProRes()`, `AudioOnlyMP3(bitRate)`, `AudioOnlyWAV()`, and the draft
preset `VideoFXExportSettings::Draft` - a 360p MP4 with the fastest H.264
preset, a low quality and a small AAC, for playing a timeline back with
sound while it is edited: seconds to render, never the file to deliver.

Which encoders exist depends on the FFmpeg build — ask before offering a
choice in the UI:

```cpp
if (!VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::H265))
    h265Option->SetDisabled(true);
```

H.264 uses libx264, then the platform encoder (VideoToolbox on macOS, Media
Foundation on Windows), then OpenH264; H.265, AV1 and ProRes likewise try the
software encoder first.

### Hardware encoders

The graphics hardware can encode too: several times faster than software,
with the processor left free, at some cost in quality for the same file
size. `VideoFXExportSettings::acceleration` chooses:

| Value | Encoder |
|---|---|
| `Auto` (default) | As before: software, then the platform's encoder (VideoToolbox, Media Foundation) |
| `Software` | Software only: libx264, libx265, libvpx, SVT-AV1, ... |
| `Hardware` | The first hardware encoder that works on this machine; software when none does |
| `NVENC` | NVIDIA (`h264_nvenc`, `hevc_nvenc`, `av1_nvenc`) |
| `QuickSync` | Intel (`h264_qsv`, `hevc_qsv`, `av1_qsv`, `vp9_qsv`) |
| `VAAPI` | VA-API on Linux, Intel and AMD graphics (`h264_vaapi`, `hevc_vaapi`, `av1_vaapi`, `vp9_vaapi`) |
| `AMF` | AMD on Windows (`h264_amf`, `hevc_amf`, `av1_amf`) |
| `VideoToolbox` | macOS (`h264_videotoolbox`, `hevc_videotoolbox`, `prores_videotoolbox`) |
| `MediaFoundation` | Windows (`h264_mf`, `hevc_mf`, `av1_mf`) |

An FFmpeg build often carries hardware encoders the machine cannot run:
Ubuntu's has `h264_nvenc` whether or not the computer has an NVIDIA card.
So VideoFX tries each hardware encoder once, by encoding one small frame,
before it chooses one, and keeps the answer for the rest of the process.
`Hardware` never lands on an encoder that does not run here. A named family
that cannot run fails with `EncoderNotAvailable`, and the error says
whether FFmpeg lacks the encoder or the machine lacks the hardware.

```cpp
VideoFXExportSettings s = VideoFXExportSettings::WebMP4(1080);
s.acceleration = VideoFXAcceleration::Hardware;           // fast where it can be

// a settings dialog: what this machine can do
for (const VideoFXEncoderInfo& e : VideoFX_ListVideoEncoders(VideoFXVideoCodec::H264))
    if (e.works) encoderChoice->AddItem(VideoFX_AccelerationName(e.acceleration));

std::string name = VideoFX_ChooseVideoEncoder(VideoFXVideoCodec::H264, VideoFXAcceleration::Hardware);
// "h264_nvenc" on an NVIDIA machine, "libx264" on one without hardware encoding
```

`VideoFX_IsVideoEncoderAvailable(codec, acceleration)` asks the same for one
choice; with `Hardware` it means a hardware encoder, not the software one
the export would fall back to. The first list takes a moment, typically
under a second, because each hardware encoder is tried once; later lists
answer at once. VA-API takes frames only in graphics memory, so each frame
is uploaded before it is encoded. The other families take ordinary frames.
Hardware encoders are driven by bit rate (`videoBitRate`, or one derived from
`quality` and the picture size); `encoderPreset` and `keyframeInterval`
reach them too. Proxies can be made on the hardware as well
(`VideoFXProxySettings::acceleration`).

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

## Project files

A whole edit can be saved as one small JSON file, a `.vfxproj`. It holds the
timeline (or a slideshow's photos and options), the music and the export
settings. It refers to media by path and holds no media itself, so it stays
a few kilobytes and an app can save it on every change.

```cpp
VideoFXProject project = VideoFXProject::FromSlideshow(photos, options, VideoFXExportSettings::WebMP4(1080));
project.title = "Summer trip";
project.outputPath = "trip.mp4";
VideoFX_SaveProject(project, "Trip/trip.vfxproj");

// later, perhaps on another machine
VideoFXProject loaded;
std::vector<std::string> missing;
if (VideoFX_LoadProject("Trip/trip.vfxproj", loaded, &missing) == VideoFXResult::Ok) {
    if (!missing.empty()) AskUserToFind(missing);   // the project still loaded
    VideoFX_RenderProject(loaded);                   // to loaded.outputPath
}
```

`VideoFXProject::FromTimeline(segments, settings)` makes a timeline project;
`VideoFX_RenderProject` renders either kind, with `VideoFX_Export` or
`VideoFX_CreateSlideshow`.

The file looks like this (shortened):

```json
{
  "format": "VideoFX project",
  "version": 1,
  "kind": "slideshow",
  "title": "Summer trip",
  "output": "trip.mp4",
  "settings": {
    "container": "auto",
    "width": 1920,
    "height": 1080,
    "music": { "path": "music/song.mp3", "volume": 0.8, "duckingLevel": 0.3 }
  },
  "slideshow": {
    "images": ["photos/a.jpg", "photos/b.jpg"],
    "options": {
      "secondsPerImage": 4.0,
      "transition": { "type": "crossfade", "duration": 1.0 },
      "captions": ["Arrival", ""],
      "keepFacesInView": true
    }
  }
}
```

The rules:

- **Paths.** Media inside the project's folder are stored relative to it,
  with `/` between folders on every system. Media elsewhere are stored
  absolute. So a folder holding the project and its media can be moved or
  copied as a whole, and moving the project file alone still finds media
  stored elsewhere. Loaded paths come back absolute. Paths are UTF-8.
- **Missing media.** `VideoFX_LoadProject` still loads a project whose
  clips, photos, songs, LUTs, overlay images or fonts have gone. It lists
  them in `missingMedia`, so the app can offer to find them.
- **Versions.** `"version"` is the format version (`kVideoFXProjectFormatVersion`,
  now 1). A file from a newer VideoFX is refused with `InvalidArgument`
  rather than half-read. Keys the reader does not know are ignored, and a
  key that is not there keeps its default, so older files keep loading.
- **Every field is written**, defaults included, so a later change of a
  default does not change how an old project renders.
- **Errors name the place**, for example
  `segments[2].transitionIn.type: unknown value "spin"`.
- **Saving is atomic.** The file is written beside the old one and moved
  over it only when it is complete, so a crash or a full disk never leaves
  half a project behind.
- **Not saved.** Media given only as pixels in memory
  (`VideoFXSegment::FromImageFrame`, `VideoFXOverlay::ImageFromFrame`) are
  refused with `InvalidArgument`, because a file cannot point to them. The
  function `VideoFXSlideshowOptions::findKeepInView` is not saved either.

`VideoFX_ProjectToJson` and `VideoFX_ProjectFromJson` give the same text
without a file, for an app that keeps projects in a database or an undo
history. Their `baseDirectory` says what relative paths are relative to.
Project files work in the build without FFmpeg too: only rendering needs the
engine.

From the command line, any edit command saves a project instead of rendering
when given `--save-project`. `videofx project` shows what a project holds and
which media are missing; `videofx render` renders it:

```
videofx slideshow trip.mp4 *.jpg --music song.mp3 --beat-sync --save-project trip.vfxproj
videofx project trip.vfxproj            # summary; exit status 1 if media are missing
videofx render trip.vfxproj             # to the output it was saved with
videofx render trip.vfxproj draft.mp4   # somewhere else
```

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
videofx slideshow trip.mp4 *.jpg --music song.mp3 --fit-music
videofx slideshow trip.mp4 *.jpg --music a.mp3 --music b.mp3 --music-crossfade 4 --fit-music
videofx slideshow trip.mp4 *.jpg --music song.mp3 --beat-sync --seconds 3
videofx slideshow trip.mp4 *.jpg --music song.mp3 --beats-per-image 8 --transition cut
videofx beats song.mp3                                   # tempo and beat times
videofx waveform song.mp3 10                             # loudness envelope, 10 blocks a second
videofx preview trip.vfxproj 12.5 shot.png 640 360        # the project's frame at 12.5 s, no export
videofx slideshow trip.mp4 a.jpg b.jpg --keep 1:0.40,0.10,0.12,0.16 --keep 2:0.7,0.3,0.1,0.14
videofx slideshow trip.mp4 *.jpg --no-faces                # plain motion, no face finding
videofx faces family.jpg                                 # the faces the detector finds
videofx concat holiday.mp4 a.mp4 b.mp4 --music song.mp3 --music-volume 0.6 --duck 0.2
videofx concat gig.mp4 live1.mp4 live2.mp4 --music song.mp3 --duck 0.4 --duck-threshold -15 --duck-hold 0.2
videofx concat gig.mp4 live1.mp4 live2.mp4 --music song.mp3 --duck 0.4 --duck-preset loud
videofx concat story.mp4 a.mp4 b.mp4 --music song.mp3 --voice intro.wav@1.5 --sound boom.wav@8
videofx slideshow trip.mp4 *.jpg --music song.mp3 --save-project trip.vfxproj
videofx project trip.vfxproj                             # what it holds, missing media
videofx render trip.vfxproj                              # render a saved project
videofx proxy trip.vfxproj                               # proxies of its clips, for editing
videofx render trip.vfxproj draft.mp4 --proxies          # a quick draft from the proxies
videofx encoders h264                                    # the H.264 encoders, and which work here
videofx transcode talk.mov talk.mp4 --hw hardware        # on the graphics hardware where there is one
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

VideoFX is a **shared library of its own**: `libVideoFX.so` on Linux,
`libVideoFX.dylib` on macOS and `libVideoFX.dll` / `VideoFX.dll` on Windows,
next to `videofx` and `VideoFXTest` in `bin/`. It is not built into the
UltraCanvas core, so an application that does no video never loads it or
FFmpeg, and every application that does shares one copy. While VideoFX is
0.x, every minor version may change the binary interface, so the minor
version is part of the library's name (`libVideoFX.so.0.7`).
`-DVIDEOFX_BUILD_SHARED=OFF` builds a static library instead (the default
on Android).
The test suite is `Tests/VideoFXTest.cpp` (`VideoFXTest` under ctest); it
generates its own clips, so it needs no media files.

---

## Roadmap

| Stage | Contents | State |
|---|---|---|
| 1 | Probe, frames, segment timeline, 26 effects, speed, joins, GIF / audio outputs, lossless cut, job, CLI | **Done** |
| 2 | Transitions between segments (30 types, with audio cross-fade), text and image overlays | **Done** |
| 2b | Still images with pan and zoom, EXIF orientation, one-call slideshows | **Done** |
| 3a | Background music with fades, looping and ducking under speech; slideshows fitted to a song | **Done** |
| 3b | Song lists, each song crossfading into the next | **Done** |
| 3c | Beat detection; slideshows changing on the beat | **Done** |
| 3d | Faces (any region) kept in shot through pan and zoom; a hook for the app's face detector | **Done** |
| 3e | Built-in face detector (OpenCV's frontal-face model, own evaluator); slideshows frame faces by default | **Done** |
| 3f | Picture-in-picture: video overlays with position, size, opacity, fades and a time range, in the export and the preview | **Done** |
| 3g | Free audio clips: voice-overs and sound effects at given times, trimmed, faded, the music ducking under voice-overs | **Done** |
| 3 | Keyframed effect and overlay parameters | Planned |
| 4a | Project files (`.vfxproj`, JSON): timelines and slideshows with music and settings, relative media paths, missing media reported | **Done** |
| 4b | Waveforms: the loudness envelope of a file's sound, block by block, in the shape `UltraCanvasWaveformElement` draws | **Done** |
| 4c | Preview: any frame of a timeline rendered as the export would make it, without an export (`VideoFXPreview`); a slideshow's plan as segments | **Done** |
| 4d | Proxy media: small stand-ins for large clips, made once into a cache folder, switched in for previews and drafts, the originals exported | **Done** |
| 4e | Hardware encoders by choice: software, any that works, or NVENC / Quick Sync / VA-API / AMF / VideoToolbox / Media Foundation, each tried on the machine before it is used | **Done** |
| 5 | A timeline editor element in UltraCanvas on top of the engine (`UltraCanvasVideoTimeline`, in the core) | **Done** |

*VideoFX — part of the UltraCanvas framework.*
