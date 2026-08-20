# UltraCanvas Audio

Cross-platform audio **playback** and **recording** for UltraCanvas.

## Status

Implemented. `ULTRACANVAS_ENABLE_AUDIO=ON` (the default) builds the
**miniaudio** backend (single-header, MIT-0, vendored at
`libspecific/Audio/miniaudio.h`): device enumeration, playback and capture
streams, WAV/MP3/FLAC decode and WAV encode. The optional system codec
libraries below extend the format matrix with FLAC/OGG/Opus/MP3 encoding and
OGG/Opus decoding. With the option OFF a null backend keeps the API surface
compiling (see Build below).

## Format support

| Format | Load | Save | Provided by |
|---|---|---|---|
| WAV  | always | always | miniaudio (dr_wav) |
| MP3  | always | with **LAME** | miniaudio (dr_mp3) + libmp3lame |
| FLAC | always | with **libFLAC** | miniaudio (dr_flac) + libFLAC |
| OGG Vorbis (`ogg`, `oga`) | with **libvorbis** | with **libvorbis** | vorbisfile + vorbisenc |
| Opus | with **opusfile** | with **libopusenc** | opusfile + libopusenc |

"Always" means whenever the audio backend is compiled in. The optional codec
libraries are system packages detected via pkg-config at configure time
(`libflac-dev`, `libvorbis-dev` + `libogg-dev`, `libopusenc-dev`,
`libopusfile-dev`, `libmp3lame-dev` on Debian/Ubuntu); each one found unlocks
its column independently. **The authoritative answer at runtime** is the
supported-format inventory — never hardcode the matrix:

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
`onBufferAvailable(samples, frames, channels)`, `onSilenceDetected`,
`onClipping`, `onMaxDurationReached`, `onError`, `onPermissionChanged`.

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
- Decoders/encoders beyond miniaudio's built-ins live in
  `libspecific/Audio/AudioCodecsExtra.cpp`, compile-gated on the
  `ULTRACANVAS_HAS_*` defines set by CMake codec detection. The backend
  delegates `EncodeFile` (non-WAV) and undecodable files to it.
