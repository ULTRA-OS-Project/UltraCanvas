# UltraCanvas Media Codec Registry

`UltraCanvasMediaCodecRegistry.h` — the one place that knows which audio and
video formats a build can handle, and the extension point for adding more.
Video decoding is registered through its companion,
`libspecific/Video/VideoCodecPlugin.h`.

## Why it exists

Two questions get confused constantly, and answering them from the same list is
what breaks:

| Question | Answer from | Used for |
|---|---|---|
| Is this file audio/video **at all**? | `IsMediaFileOfKind` | classification: which viewer, which error |
| Can this build **decode** it? | `CanDecodeMediaFile` | the format inventory, dialog filters, whether to attempt a load |

Before the registry, the media viewer carried hardcoded extension lists that had
to be kept in step by hand with the codecs CMake actually linked — and were not.
That is how an `.m4a` came to be classified as audio by a build with no AAC
decoder in it: the viewer built a player, the decode failed, and the transport
bar sat there silently with nothing to say.

Keeping the two answers apart is the point. A format that is **recognised but
unsupported** stays registered with `canDecode = false`, so the viewer still
shows an audio player and names what is missing, instead of falling through and
mistaking the file for a picture.

## Reading the registry

```cpp
#include "UltraCanvasMediaCodecRegistry.h"

if (IsMediaFileOfKind(MediaCodecKind::Audio, path)) {
    if (!CanDecodeMediaFile(MediaCodecKind::Audio, path)) {
        auto codec = FindMediaCodecForFile(MediaCodecKind::Audio, path);
        // codec->description  "MPEG-4 audio (AAC)"
        // codec->notes        "needs FAAD2, fdk-aac, or the GStreamer plugins"
    }
}
```

| Call | Returns |
|---|---|
| `IsMediaFileOfKind(kind, path)` | the file is that kind, decodable or not |
| `CanDecodeMediaFile(kind, path)` | a decoder for it exists in this build |
| `CanEncodeMediaExtension(kind, ext)` | an encoder for that extension exists |
| `FindMediaCodecForFile(kind, path)` | the registration claiming the file, honouring its content probe |
| `FindMediaCodecByExtension(kind, ext)` | the registration for an extension, ignoring probes — for a file about to be written |
| `GetRegisteredMediaCodecs(kind)` | everything registered, in registration order |

`UltraCanvasSupportedFormats` is built from this: its Audio and Video categories
are the registry's entries filtered down to the ones that decode or encode, so
the inventory, the Filer's categories and every file dialog follow the registry
automatically. Nothing else needs to hold a list.

## Registering a codec

Registration is how an application or plugin adds a format. Do it once at
start-up, before the first folder listing:

```cpp
MediaCodecRegistration codec;
codec.extension   = "ape";                 // leading dot and case are normalised
codec.aliases     = { "apl" };
codec.description = "Monkey's Audio";
codec.kind        = MediaCodecKind::Audio;
codec.canDecode   = true;
codec.provider    = "MyApp (libMAC)";
codec.decodeAudio = [](const std::string& path) -> std::shared_ptr<UCAudio> {
    return MyApp::DecodeMonkeysAudio(path);   // null if it turns out not to be one
};
RegisterMediaCodec(codec);
```

That is all. The media viewer now classifies `.ape` as audio, the Filer files it
under Audio, `UltraCanvasSupportedFormats` lists it, `UltraCanvasFileLoader::OpenAudio`
offers it in the dialog, and `UCAudio::LoadFromFile` returns its PCM.

**Where the callbacks run.** `decodeAudio` and `encodeAudio` are tried *after*
the built-in backend and codec libraries have declined the file. Running last
means a plugin is reachable without displacing anything — and it means a
plugin's decoder must never call back into `UCAudio::LoadFromFile`, which would
recurse. `encodeAudio` is found by the target path's extension, because
`AudioFormat` cannot name a format a plugin brought with it:

```cpp
audio->SaveToFile("out.ape", AudioFormat::Unknown);   // reaches encodeAudio
```

**Re-registering upgrades, it does not duplicate.** A second registration for
the same extension and kind OR-s the capabilities, unions the aliases, and takes
every other field the incoming registration carries. So adding an encoder for a
format that already had a decoder keeps the decoder. To replace an entry
outright, `UnregisterMediaCodec` first.

**Declaring without implementing is useful.** A registration with neither
`canDecode` nor `canEncode` — and no callbacks — teaches the framework that the
extension is audio or video. It stays out of the format inventory, but files
with it are classified correctly and get an honest error. That is exactly how
the framework registers `.m4a` on a build with no AAC decoder.

### Content probes: when the extension is not enough

`.ts` is TypeScript source far more often than it is an MPEG transport stream,
and file classification runs the video test before the text one — so claiming
the extension by name turned every `.ts` in a source tree into a video the
player could not open. A registration can settle it by content instead:

```cpp
codec.probeFile = [](const std::string& path) {
    return LooksLikeTransportStream(path);   // 0x47 at every 188-byte boundary
};
```

`FindMediaCodecForFile` and `IsMediaFileOfKind` honour the probe; it runs
outside the registry's lock, so a probe may touch the filesystem freely. An
entry carrying a probe is **left out of `UltraCanvasSupportedFormats`**, which
is keyed on extensions alone and could not honour it — which is why the built-in
transport-stream entry in the inventory is the unambiguous `m2ts`/`mts`, and the
bare `.ts` lives only in the registry.

## What the framework registers

Built-ins register on first query (or explicitly via `RegisterBuiltinMediaCodecs`,
which an application only needs in order to force them in before replacing one).
They are compile-gated on what CMake found, so the registry describes *this*
build:

- **Audio** — WAV/MP3/FLAC from miniaudio; Ogg Vorbis, Opus, FLAC and MP3
  encoding from the optional system codec libraries; AAC (`m4a`/`m4b`, `aac`)
  from FAAD2, fdk-aac or the GStreamer plugins; `wma`, `aiff`, `mka` from the
  platform media plugins. See [UltraCanvasAudio.md](UltraCanvasAudio.md).
- **Video** — the platform backend's demuxer/muxer matrix (GStreamer, Media
  Foundation, AVFoundation). `canEncode` is true only for the containers the
  capture session can actually mux; everything else the backend demuxes is
  registered decode-only. Decoding for these goes through `IVideoBackend`;
  a codec an application adds brings its own — see below.

## Registering a video codec

A video registration carries its decoding in a companion header,
`libspecific/Video/VideoCodecPlugin.h`, rather than on `MediaCodecRegistration`
itself: it deals in `IVideoDecodeSession`, and this header has to stay
includable from anywhere — including a build with no video backend at all.

```cpp
#include "../libspecific/Video/VideoCodecPlugin.h"

MediaCodecRegistration codec;
codec.extension   = "ivf";
codec.description = "Indexed Video Format";
codec.kind        = MediaCodecKind::Video;
codec.provider    = "MyApp";

RegisterVideoCodecPlugin(codec,
    [](const std::string& source, const VideoDecodeOptions& opts)
            -> std::unique_ptr<IVideoDecodeSession> {
        return MyApp::OpenIvf(source, opts);      // null if it is not one
    });
```

Supplying a decoder implies `canDecode`, so a format cannot be registered with a
working decoder and then advertised as unplayable. `UltraCanvasVideoPlayer`
opens the session, and `CaptureVideoThumbnail` drives the same factory to
produce a poster frame — so a plugin gets thumbnails for free. Pass an optional
third argument only when the codec has something cheaper than opening a session
and waiting for a frame:

```cpp
RegisterVideoCodecPlugin(codec, openDecoder,
    [](const std::string& source, const VideoThumbnailRequest& req) {
        return MyApp::GrabIvfPoster(source, req);
    });
```

`UnregisterVideoCodecPlugin` drops the callbacks but leaves the registry entry,
so a format can stay *recognised* after its decoder goes away;
`UnregisterMediaCodec` removes it entirely.

### Precedence: video plugins run first, audio plugins run last

An audio plugin is tried **after** the built-in backend and codec libraries have
declined a file. A video plugin is tried **before** the platform backend, for
the sources it claims. The asymmetry is deliberate and not a matter of taste:

`IVideoBackend::OpenDecoder` does not decline a source it cannot decode. The
GStreamer backend builds a `playbin` and reports an undecodable file
asynchronously on its bus; Media Foundation and AVFoundation behave similarly.
A "backend first, plugin fallback" ordering would therefore hand every source to
the backend and leave a registered codec permanently unreachable.

Precedence is safe because the lookup is keyed on extensions a plugin
*explicitly registered*. A source no plugin claimed goes straight to the
backend, untouched — that is what `Tests/VideoCodecPluginTest.cpp` asserts, so
the rule cannot quietly become interception.

## Threading

Registration and every query take an internal mutex, so codecs may be registered
from any thread at any time; a query sees the state at the moment it is made.
Entries are copy-on-write, so a `FindMediaCodecForFile` result stays valid
however the registry changes afterwards.

## Tests

`BUILD_TESTS=ON` builds two, both headless and both needing no media assets:

- `Tests/MediaCodecRegistryTest.cpp` — the built-ins, the
  recognised-versus-decodable split, inventory agreement, probe gating, the
  upgrade-not-duplicate rule, and a registered codec being reached through
  `UCAudio::LoadFromFile` and `SaveToFile`.
- `Tests/VideoCodecPluginTest.cpp` — a synthetic in-process video codec driven
  through `UltraCanvasVideoPlayer` and `CaptureVideoThumbnail`, including the
  generic thumbnail fallback, the precedence rule, and that a source no plugin
  claimed is left alone. It passes with `ULTRACANVAS_ENABLE_VIDEO=OFF` too,
  which is the case that shows a plugin working on a build with no platform
  backend at all.
