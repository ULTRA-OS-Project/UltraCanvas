# UltraCanvas Audio

Cross-platform audio **playback** and **recording** for UltraCanvas.

## Status

API scaffolded. Backend is a no-op stub — `ULTRACANVAS_ENABLE_AUDIO=ON` reserves
the hook, but no real playback/capture device is wired yet. The recommended
next step is vendoring **miniaudio** (single-header, MIT, covers Linux, macOS,
Windows, FreeBSD, Android, iOS) and providing a single `AudioBackendMiniaudio.cpp`
implementation of `IAudioBackend`.

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
cmake -DULTRACANVAS_ENABLE_AUDIO=ON ..
```

Default OFF. With the option OFF (or no backend linked), `UltraCanvasAudioDevices::IsAvailable()`
returns `false` and all calls succeed-but-do-nothing — apps that use audio
optionally can compile and run on backend-less systems.

## Architecture notes

- Backend is selected via the `IAudioBackend` interface
  (`libspecific/Audio/IAudioBackend.h`). One real implementation per supported
  platform/library; the null stub ships unconditionally as a fallback.
- Backend audio callbacks fire on the backend's audio thread. The player /
  recorder marshal user-facing callbacks back to the UI thread via the
  existing event/timer infrastructure (TODO during full implementation).
- Decoders/encoders live in `Plugins/Audio/` (WAV built-in; MP3/OGG/FLAC
  optional). They register with `UCAudio::LoadFromFile` dispatch.
