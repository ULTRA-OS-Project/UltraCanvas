# UltraCanvasSpectrogram

An audio **spectrogram** element: a Short-Time Fourier Transform (STFT) in front
of [`UltraCanvasHeatmapChart`](UltraCanvasHeatmapChart.md). It turns a 1D audio
signal into a 2D time × frequency magnitude matrix and renders it as a heatmap
(time on X, frequency on Y, colour = energy).

- STFT engine (UI-free): `include/Plugins/Charts/UltraCanvasSTFT.h` / `Plugins/Charts/UltraCanvasSTFT.cpp`
- Element: `include/Plugins/Charts/UltraCanvasSpectrogram.h` / `Plugins/Charts/UltraCanvasSpectrogram.cpp`
- FFT backend: vendored [KissFFT](https://github.com/mborgerding/kissfft) (BSD-3-Clause) under `libspecific/FFT/`

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasSpectrogram.h"
#include "UltraCanvasAudio.h"

auto spec = UltraCanvas::CreateSpectrogramElement("spec1", 20, 20, 800, 400);

// From a decoded audio file (downmixed to mono automatically):
auto audio = UltraCanvas::UCAudio::LoadFromFile("speech.wav");
spec->SetAudio(audio);

// ...or from a raw mono float signal in [-1, 1]:
// spec->SetSignal(samples, sampleRateHz);

spec->SetChartTitle("Spectrogram");
container->AddChild(spec);
```

The transform runs lazily on the next render after any input/parameter change,
so configuration calls are cheap. Call `Recompute()` to force it immediately.

## Defaults

The element is preconfigured for a typical spectrogram look:

- Colour map `Inferno`
- Row order `BottomUp` (low frequencies at the bottom)
- Render mode `Image` (scaled pixmap blit — efficient for large grids)
- Colour bar enabled; axis titles "Time (s)" / "Frequency (Hz)"
- Decibel magnitude with an 80 dB dynamic range

## STFT parameters

```cpp
spec->SetFFTSize(2048);          // window length / FFT size (even; ideally a power of two)
spec->SetHopSize(512);           // samples between frames (<= 0 => fftSize/4)
spec->SetWindow(UltraCanvas::SpectrogramWindow::Hann);
spec->SetMagnitudeMode(UltraCanvas::SpectrogramMagnitude::Decibels);
spec->SetDynamicRangeDb(90.0);   // dB floor in Decibels mode
spec->SetMaxFrequency(8000.0);   // cap displayed frequency (0 => Nyquist)
```

| Window | Notes |
|---|---|
| `Hann` | Good general-purpose default. |
| `Hamming` | Slightly different side-lobe trade-off. |
| `Blackman` | Lower side lobes, wider main lobe. |
| `Rectangular` | No windowing (more spectral leakage). |

- **Decibels** mode normalizes to the peak magnitude and clamps to
  `[-dynamicRangeDb, 0]`; it uses a linear colour scale over that dB range.
- **Linear** mode keeps raw magnitudes and uses a logarithmic colour scale.

`fftSize` controls the frequency/time resolution trade-off: larger windows give
finer frequency bins but coarser time resolution.

## Using the STFT engine directly

The transform is available without any UI dependency:

```cpp
#include "Plugins/Charts/UltraCanvasSTFT.h"

UltraCanvas::SpectrogramParams params;
params.fftSize = 1024;
params.hopSize = 256;

UltraCanvas::SpectrogramResult result;
if (UltraCanvas::ComputeSpectrogram(signal, sampleCount, sampleRate, params, result)) {
    // result.magnitudes is row-major: [bin * result.frames + frame]
    double t  = result.TimeAtFrame(frame);   // seconds
    double hz = result.FrequencyAtBin(bin);  // Hz
}
```

`AudioToMonoFloat(const UCAudio&)` downmixes decoded PCM (S16/S24/S32/F32, any
channel count) to a mono float buffer suitable for `ComputeSpectrogram`.

## Notes

- The frequency axis (rows) and time axis (columns) are labelled automatically;
  labels are thinned to avoid overlap.
- Hovering a cell shows its time/frequency label and value (dB or magnitude).
