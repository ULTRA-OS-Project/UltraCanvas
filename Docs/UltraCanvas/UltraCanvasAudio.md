# UltraCanvas Audio

<!-- doc-check: namespace MyApp { std::shared_ptr<UCAudio> DecodeApe(const std::string& path); } std::shared_ptr<UCAudio> audio; std::string path; -->

Cross-platform audio **playback** and **recording** for UltraCanvas.

## Status

Implemented. `ULTRACANVAS_ENABLE_AUDIO=ON` (the default) builds the
**miniaudio** backend (single-header, MIT-0, vendored at
`libspecific/Audio/miniaudio.h`): device enumeration, playback and capture
streams, WAV/MP3/FLAC decode and WAV encode. The optional system codec
libraries below extend the format matrix with FLAC/OGG/Opus/MP3 encoding and
OGG/Opus/AAC decoding, and the platform media framework covers whatever is
left. With the option OFF a null backend keeps the API surface compiling
(see Build below).

## Format support

| Format | Load | Save | Provided by |
|---|---|---|---|
| WAV  | always | always | miniaudio (dr_wav) |
| MP3  | always | with **LAME** | miniaudio (dr_mp3) + libmp3lame |
| FLAC | always | with **libFLAC** | miniaudio (dr_flac) + libFLAC |
| OGG Vorbis (`ogg`, `oga`) | with **libvorbis** | with **libvorbis** | vorbisfile + vorbisenc |
| Opus | with **opusfile** | with **libopusenc** | opusfile + libopusenc |
| AAC in MPEG-4 (`m4a`, `m4b`) | with **FAAD2**, **fdk-aac** *or* the **GStreamer** plugins | never | in-tree MP4 demuxer + FAAD2 / fdk-aac, else GStreamer |
| Raw AAC (`aac`, ADTS) | with **FAAD2**, **fdk-aac** *or* the **GStreamer** plugins | never | FAAD2 / fdk-aac, else GStreamer |
| ALAC in `m4a`, WMA, AIFF | with the **GStreamer** plugins | never | GStreamer `decodebin` |

The matrix itself lives in the codec registry —
[UltraCanvasMediaCodecRegistry.md](UltraCanvasMediaCodecRegistry.md) — which is
also where an application registers a codec of its own, and where the media
viewer asks whether a file is audio at all. A format the registry recognises
but cannot decode is deliberately absent from the table above and present in
the registry, so a viewer classifies the file correctly and says what is
missing rather than treating it as something else.

"Always" means whenever the audio backend is compiled in. The optional codec
libraries are system packages detected via pkg-config at configure time
(`libflac-dev`, `libvorbis-dev` + `libogg-dev`, `libopusenc-dev`,
`libopusfile-dev`, `libmp3lame-dev`, `libfaad-dev` or `libfdk-aac-dev` on
Debian/Ubuntu); each one found unlocks its column independently. **The
authoritative answer at runtime** is the supported-format inventory — never
hardcode the matrix:

```cpp
auto audio = UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory::Audio);
for (const auto& f : audio) {
    // f.extension, f.canLoad, f.canSave, f.provider
}
```

`UltraCanvasFileLoader::OpenAudio` filters and the audio recorder's save
dialog both follow that inventory automatically. To map a file extension to
the enum used by the save APIs:

```cpp
AudioFormat fmt = AudioFormatFromExtension("flac");   // ".OGG", "oga", ... also fine
if (fmt == AudioFormat::Unknown) fmt = AudioFormat::WAV;
audio->SaveToFile(path, fmt);
```

### AAC and M4A

An `.m4a` is an MPEG-4 container, so playing one is two jobs. The container is
read in-tree by `libspecific/Audio/Mp4AudioDemux.{h,cpp}` — a dependency-free
ISO-BMFF walker that finds the audio track, its `esds` AudioSpecificConfig and
the per-sample byte ranges (`Tests/Mp4AudioDemuxTest.cpp` covers it). The AAC
bitstream itself needs a decoder library, tried in this order:

1. **FAAD2** (`libfaad`) — AAC-LC and HE-AAC, fed raw access units.
2. **fdk-aac** — the alternative, used only when FAAD2 is absent.
3. **The GStreamer plugins** — `uridecodebin` over whatever the system has,
   which is the route that needs no extra package on a desktop that already
   has the plugins installed for video, and the only one that also covers
   **ALAC**, **WMA** and **AIFF**.

A build with none of the three reports M4A as unsupported in the inventory, and
`UltraCanvasAudioPlayer::GetLastError()` says which codec the file turned out
to hold and what would decode it, rather than leaving a silent transport bar.

> **Licensing:** FAAD2 is GPL-2.0 and fdk-aac carries the Fraunhofer FDK AAC
> license; linking either imposes its terms on the resulting binary. Neither is
> bundled and neither is required — see `THIRD_PARTY_LICENSES.md`. The
> GStreamer route (LGPL 2.1, already a dependency of the video backend) changes
> nothing about the framework's licensing.

Fragmented MP4 files (`moof`-based, no complete sample table in the `moov`) are
recognised and reported, not played.

Encoding details: FLAC keeps 16-bit sources bit-exact and writes wider/float
sources as 24-bit; Vorbis encodes VBR (quality 0.4); Opus is Ogg-encapsulated
and resamples internally (decode is always 48 kHz — Opus by design); MP3 is
VBR with a Xing header, mono/stereo only. `Tests/AudioCodecTests.cpp`
(`BUILD_TESTS=ON`) roundtrips every save-capable format.

## Public API surface

### Resource

`UltraCanvasAudio.h` — `UCAudio` holds decoded PCM + `AudioBufferInfo`. Mirrors
`UCImage`. Decoders are pluggable (`Plugins/Audio/`); WAV will be the built-in
default.

### Adding a codec

`UCAudio::LoadFromFile` and `SaveToFile` fall through to the codec registry once
the built-in backend and codec libraries have declined a file, so an application
that brings its own decoder registers it and everything else follows — the
viewer's classification, the Filer's categories, the format inventory and the
open/save dialogs:

```cpp
MediaCodecRegistration codec;
codec.extension   = "ape";
codec.description = "Monkey's Audio";
codec.kind        = MediaCodecKind::Audio;
codec.canDecode   = true;
codec.provider    = "MyApp (libMAC)";
codec.decodeAudio = [](const std::string& p) { return MyApp::DecodeApe(p); };
RegisterMediaCodec(codec);
```

See [UltraCanvasMediaCodecRegistry.md](UltraCanvasMediaCodecRegistry.md) for the
rest — encode callbacks, content probes for an ambiguous extension, and the
upgrade rules when two registrations claim the same format.

### Playback (non-visual)

`UltraCanvasAudioPlayer.h` — `UltraCanvasAudioPlayer`

```cpp
auto p = CreateAudioPlayerFromFile("song.ogg");
p->onPositionChanged = [](double s){ /* ... */ };
p->onEnded = []{ /* ... */ };
p->SetVolume(0.8f);
p->Play();
```

Transport: `Play / Pause / Stop / Seek(seconds)`.
Properties: `Volume`, `Mute`, `Loop`, `PlaybackRate`, `OutputDevice`.
Callbacks: `onLoaded`, `onPlaybackStateChanged`, `onPositionChanged`, `onEnded`,
`onError`.

#### Streaming sink (live audio)

A file or a `UCAudio` is played from a buffer that exists before `Play()`.
Live audio — the far end of a call, synthesised speech, a network stream —
does not exist up front, so the player also offers a **sink**: open it at the
stream's rate and channel count, then push interleaved float PCM as it is
produced. The device clock pulls from a bounded ring (`AudioFrameRing`,
`UltraCanvasAudioStreaming.h`); the caller keeps it topped up.

```cpp
auto p = CreateAudioPlayer();
AudioSinkConfig sink;
sink.sampleRate = 48000;
sink.channels = 1;
sink.bufferMs = 200;                      // ring capacity = the latency ceiling
p->OpenSink(sink);                        // opens the device and starts at once
p->onSinkUnderrun = []{ /* the ring ran dry; silence was played */ };

// From the decoder / network thread (one producer):
size_t queued = p->PushSinkFrames(pcm, frames);   // < frames: ring was full
if (p->GetSinkQueuedSeconds() > 0.12) { /* producer is ahead; slow down */ }

p->SetVolume(0.8f);                       // volume, mute and device apply as usual
p->CloseSink();                           // or Unload()
```

Sink API: `OpenSink(cfg)`, `IsSinkOpen()`, `PushSinkFrames(samples, frames)`
(also takes an `AudioLiveFrame` straight from the recorder),
`GetSinkQueuedFrames()`, `GetSinkQueuedSeconds()`, `ClearSink()`,
`CloseSink()`, `GetSinkUnderrunCount()`, `GetSinkDroppedFrames()`. While a
sink is open `Pause` / `Play` / `Stop` apply to it (`Stop` also empties the
ring), `Seek` returns false, `GetDuration()` is 0 and `GetPosition()` is the
seconds played. Frames pushed beyond `bufferMs` are dropped and counted, never
partially written, so latency cannot creep up; an underrun plays silence and
fires `onSinkUnderrun` once per episode, on the audio thread.

### Recording (non-visual)

`UltraCanvasAudioRecorder.h` — `UltraCanvasAudioRecorder`

```cpp
AudioCaptureConfig cfg;
cfg.sampleRate = 44100;
cfg.channels = 1;
cfg.maxDurationMs = 30000;

auto r = CreateAudioRecorderWithConfig(cfg);
r->onLevelChanged = [](float peak, float rms){ /* update VU */ };
r->Open();
r->Start();
// ...later
r->Stop();
auto buffer = r->TakeBuffer();          // shared_ptr<UCAudio>
r->SaveToFile("clip.wav");              // or persist directly
```

Transport: `Open / Start / Pause / Resume / Stop / Close`.
Output: `TakeBuffer() -> UCAudio`, `SaveToFile(path, format)`, `Discard()`.
Callbacks: `onRecordingStateChanged`, `onLevelChanged(peak, rms)`,
`onLiveFrame(frame)`, `onSilenceDetected`, `onClipping`,
`onMaxDurationReached`, `onError`, `onPermissionChanged`.

#### Live frames and live capture mode

`onLiveFrame` delivers every captured frame as it arrives, in either mode —
a live waveform, an on-the-fly encoder, a call. (It replaces the earlier
`onBufferAvailable`, which only fired for 32-bit float capture and handed
over raw backend bytes; `onLiveFrame` converts any sample type and applies
gain and mute.) `AudioCaptureMode::Record` (the default) also accumulates
for `TakeBuffer()` and `SaveToFile()`. A call, a speech recogniser or a
streaming encoder wants nothing kept, so memory stays flat however long the
session runs: that is `AudioCaptureMode::Live`, where `onLiveFrame` is the
only output:

```cpp
AudioCaptureConfig cfg;
cfg.sampleRate = 48000;
cfg.channels = 1;
cfg.mode = AudioCaptureMode::Live;
cfg.liveFrameMs = 10;                     // Opus / WebRTC frame; 20 for most speech engines

auto r = CreateAudioRecorderWithConfig(cfg);
r->onLiveFrame = [&](const AudioLiveFrame& f) {
    // Audio thread. f.samples: f.frameCount * f.channels floats in [-1, 1],
    // gain and mute applied, valid until this call returns.
    encoder.Encode(f.samples, f.frameCount);          // or player->PushSinkFrames(f)
};
r->Open();
r->Start();
// ... Stop() flushes the partial last frame, zero-padded.
```

`liveFrameMs > 0` repacketises the backend's chunks (whatever period the
device uses) into frames of exactly that duration through
`AudioFramePacketizer`; `0` passes each backend chunk through as one frame.
`AudioLiveFrame::firstFrameIndex` counts frames since `Start()`, which is the
timestamp a codec or a jitter buffer needs. Level metering, `onClipping`,
`onSilenceDetected`, `maxDurationMs` and `onLiveFrame` work in both modes;
`TakeBuffer()` returns an empty buffer in Live mode.

### Devices & permission

`UltraCanvasAudioDevices.h` — static helpers:

```cpp
auto inputs = UltraCanvasAudioDevices::ListInputDevices();
auto def    = UltraCanvasAudioDevices::GetDefaultOutputDevice();
if (UltraCanvasAudioDevices::GetMicrophonePermission() != MicrophonePermission::Granted) {
    UltraCanvasAudioDevices::RequestMicrophonePermission([](bool granted){ /* ... */ });
}
```

Permission states `Undetermined / Granted / Denied / Restricted` cover the
macOS/Windows OS-prompt flow; Linux always reports `Granted`.

## Visual elements

### `UltraCanvasAudioPlayerElement`

Composite based on existing primitives (`UltraCanvasButton`, `UltraCanvasSlider`,
`UltraCanvasLabel`). Default layout:

```
[▶] [■]  ▭▭▭▭▭▭▭▭▭▭▭▭▭▭▭▭  0:32 / 3:21   [🔊 ━●━━]
```

Style flags via `AudioPlayerStyle`: `showVolumeSlider`, `showTimeLabels`,
`showLoopButton`, `showWaveform`, `compact`.

The row adapts to the width it is given (e.g. the UltraFiler preview pane can
go down to ~260px): when the fixed-size controls would overflow, the volume
slider is hidden first, then the time label — the mute button stays so the
sound can still be silenced, and the seek bar always keeps a usable length
instead of being squeezed out or clipping controls at the right edge. The
style flags remain the upper bound: a control disabled by style never
reappears, and everything hidden for width returns as soon as the element is
wide enough again.

Factories: `CreateAudioPlayer`, `CreateAudioPlayerFromFile`,
`CreateCompactAudioPlayer`.

### `UltraCanvasAudioRecorderElement`

```
[●REC] [⏸] 00:42  ▮▮▮▮▮▮▮░░░░░░░  [Mic ▾] [Save] [Discard]
```

Style flags via `AudioRecorderStyle`: `showDeviceSelect`, `showGainSlider`,
`showWaveform`, `showElapsedTime`, `showSaveDiscard`, `showEmbeddedPlayer`,
`compact`.

When `showEmbeddedPlayer` is enabled, an `UltraCanvasAudioPlayerElement` is
mounted below the recorder and fed the just-captured `UCAudio` after `Stop()` —
useful for "record → review → save" flows.

Factories: `CreateAudioRecorder`, `CreateCompactAudioRecorder`,
`CreateAudioRecorderWithPlayback`.

## Build

```
cmake -DULTRACANVAS_ENABLE_AUDIO=ON ..            # default ON
```

With the option OFF (or no backend linked), `UltraCanvasAudioDevices::IsAvailable()`
returns `false` and all calls succeed-but-do-nothing — apps that use audio
optionally can compile and run on backend-less systems.

Optional codec packages (each detected independently at configure time):

```
# Debian/Ubuntu
sudo apt install libflac-dev libvorbis-dev libogg-dev \
                 libopus-dev libopusfile-dev libopusenc-dev libmp3lame-dev
# macOS
brew install flac libvorbis opus opusfile libopusenc lame
```

AAC decode is separate because of its licensing (see above). Add **one** of:

```
# Debian/Ubuntu - FAAD2 (GPL-2.0) or fdk-aac (Fraunhofer FDK AAC license)
sudo apt install libfaad-dev
sudo apt install libfdk-aac-dev
# ...or nothing at all: the GStreamer plugins already installed for video
#    decode M4A, and ALAC/WMA/AIFF with them
sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
```

The configure log says which route was taken — `Audio codec: AAC/M4A decode
(FAAD2 …)`, `(fdk-aac …)`, `Audio codec: platform decode fallback (GStreamer
…)`, or `Audio codec: AAC/M4A decode unavailable`.

## Architecture notes

- Backend is selected via the `IAudioBackend` interface
  (`libspecific/Audio/IAudioBackend.h`). One real implementation per supported
  platform/library; the null stub ships unconditionally as a fallback.
- Backend audio callbacks fire on the backend's audio thread.
  `UltraCanvasAudioPlayer` documents its `onPositionChanged` / `onEnded` /
  state-change callbacks as audio-thread calls; `UltraCanvasAudioPlayerElement`
  marshals all of its UI work back to the UI thread via
  `UltraCanvasApplicationBase::PostToUIThread`, so consumers of the element
  never see a cross-thread callback. Direct users of the player must marshal
  themselves before touching UI.
- End of stream: when a non-looping source plays out, the player emits
  `onEnded` (once), reports `Stopped`, and feeds silence to the still-open
  device until the next transport call (`Play` restarts from the beginning);
  the element additionally stops the device. Earlier the device kept pulling
  frames past the end and the track audibly restarted from 0:00 while the
  state said `Stopped`.
- Live audio: `UltraCanvasAudioStreaming.h` is header-only and backend-free.
  `AudioFrameRing` is a wait-free single-producer / single-consumer ring of
  interleaved float frames (the sink's queue); `AudioFramePacketizer` turns
  arbitrary backend chunks into fixed-duration frames (the recorder's live
  mode); `AudioLiveFrame` is the block both pass around. A call stack or a
  codec can use them without a device, and `Tests/AudioStreamingTest.cpp`
  does.
- Decoders/encoders beyond miniaudio's built-ins live in
  `libspecific/Audio/AudioCodecsExtra.cpp`, compile-gated on the
  `ULTRACANVAS_HAS_*` defines set by CMake codec detection. The backend
  delegates `EncodeFile` (non-WAV) and undecodable files to it.
