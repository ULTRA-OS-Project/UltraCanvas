# UltraCanvas Waveform Chart

`UltraCanvasWaveformElement` — an amplitude **waveform display** that draws a
min/max sample envelope, an optional **RMS** band, a zero/silence axis and a
movable **playhead**. It is module-agnostic: it consumes a neutral
`WaveformChannelData` struct and carries no dependency on the audio backend, so
it can visualise any source of summarised sample data.

## Status

Fully implemented. Pure UI element (`UltraCanvasUIElement`), rendered through the
standard `IRenderContext`. No external dependency.

## Data model

`WaveformChannelData` holds one entry per "block" of samples:

| Field            | Meaning                                             |
|------------------|-----------------------------------------------------|
| `minValues`      | Minimum sample per block, normalised to `[-1, 1]`   |
| `maxValues`      | Maximum sample per block, normalised to `[-1, 1]`   |
| `rmsValues`      | RMS per block, `[0, 1]` (optional)                  |
| `samplesPerBlock`| Samples summarised per block                        |
| `totalBlocks`    | Number of blocks                                    |
| `duration`       | Total duration in seconds (drives time<->x mapping) |

The element re-buckets blocks to one column per device pixel at draw time, so it
stays crisp and cheap regardless of how many blocks the data carries.

## Public API surface

```cpp
auto wf = CreateWaveformElement("wave", x, y, w, h);

wf->SetData(channelData);                 // feed min/max/rms envelope
wf->SetRenderStyle(WaveformRenderStyle::Filled);  // Filled | Outline | Bars
wf->SetOverlay(WaveformOverlay::RMSAndZeroAxis);  // None | RMS | ZeroAxis | RMSAndZeroAxis

wf->SetPlayheadTime(seconds);             // move the cursor (clamped to duration)
wf->SetInteractiveSeek(true);             // click/drag to seek

wf->SetVisibleWindowSeconds(10.0);        // show only the last 10s, scrolling
wf->SetVisibleWindowSeconds(0.0);         // 0 (or >= duration) shows the whole track

wf->onSeek         = [](double s){ /* user clicked/dragged */ };
wf->onPlayheadMove = [](double s){ /* playhead position changed */ };
```

`SetVisibleWindowSeconds()` restricts the rendered and seekable view to a
trailing window that ends at the playhead and scrolls with it (e.g. "last 10
seconds"). The window keeps a constant length, clamped to the track bounds; a
value `<= 0` — or one at least as long as the track — shows the whole waveform.

Colours are configurable: `SetWaveColor`, `SetRMSColor`, `SetBackgroundColor`,
`SetPlayheadColor`, `SetZeroAxisColor`.

### AudioFX adapter

`SetFromAudioFX(fx)` is a templated convenience that copies an
`AudioFXWaveformData` (matching field names) into the neutral struct, so the
core UI never compile-depends on the AudioFX module.

## Wiring to the audio player

The waveform pairs naturally with `UltraCanvasAudioPlayerElement`: feed the
player's `onPositionChanged` into `SetPlayheadTime()`, and feed the waveform's
`onSeek` into the player's `Seek()`. See
`Apps/DemoApp/UltraCanvasWaveformExamples.cpp` for a complete demo that decodes a
bundled MP3 into an envelope and keeps both views in sync.
