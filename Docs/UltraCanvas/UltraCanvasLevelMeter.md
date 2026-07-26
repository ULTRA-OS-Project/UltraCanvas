# UltraCanvasLevelMeter — Level Meter / VU Strip

`UltraCanvasLevelMeter` is a small, self-contained UI element that visualizes a
live audio level in one of two render modes:

- **Three-zone VU meter** (default): a horizontal bar filled to the current
  *peak* level. The fill crosses three colour zones — low (0–60 %), mid
  (60–85 %) and high/clipping (85–100 %) — and a dark vertical tick marks the
  current *RMS* level inside the bar.
- **Scrolling waveform strip**: a rolling 200-sample history of pushed peak
  values, drawn as mirrored bars around the centre line — the classic
  "recording strip" look.

It is the same element the audio recorder control
(`UltraCanvasAudioRecorderElement`) uses for its input level display, but it is
public and reusable anywhere a compact live meter is needed.

Header: `include/UltraCanvasAudioRecorderElement.h`

## Creating

```cpp
#include "UltraCanvasAudioRecorderElement.h"

auto meter = std::make_shared<UltraCanvasLevelMeter>("meter", 300, 18);
parent->AddChild(meter);
```

The constructor takes `(id, width, height)`; position is left to the parent's
flex layout (like every recorder child, the meter draws inside whatever bounds
the layout assigns it).

## API

| Method | Purpose |
|---|---|
| `SetWaveformMode(bool)` | `false` = VU meter (default), `true` = scrolling waveform strip |
| `SetLevel(float peak, float rms)` | Feed the VU meter. Both values are normalized `[0, 1]` |
| `PushWaveformSample(float peak)` | Append one peak value to the strip's 200-sample history |
| `SetColors(bg, low, mid, high)` | Theme the meter (background + the three zone colours) |
| `Reset()` | Zero the levels and clear the waveform history |

Notes:

- In waveform mode the bars are drawn with the `low` colour and the background
  with `bg`; the `mid`/`high` colours are used by the VU mode's zones.
- The element only repaints when a setter changes something
  (`RequestRedraw()`), so it is cheap to drive from a timer.

## Driving the meter

The meter is passive — you feed it levels. Typical sources:

```cpp
// From a periodic timer (~30 fps), e.g. computed from decoded PCM
// around the playback position:
meter->SetLevel(peak, rms);        // VU mode
strip->PushWaveformSample(peak);   // waveform mode
```

The audio recorder element wires it to the capture callback's live peak/RMS;
the demo page (`Apps/DemoApp/UltraCanvasLevelMeterExamples.cpp`) shows three
other sources: real playback levels of the bundled sample track, synthetic
generators, and manual sliders.

## Theming

```cpp
// Ocean-on-dark theme
meter->SetColors(Color(15, 30, 48),     // background
                 Color(56, 189, 248),   // low zone
                 Color(99, 102, 241),   // mid zone
                 Color(244, 63, 94));   // high / clipping zone
```

Defaults are a light grey background with green / yellow / red zones.

## Demo

The DemoApp page **Audio Elements → Level Meter / VU Strip** shows both render
modes in several themes, a live dBFS readout, `Reset()`, and all three level
sources.
