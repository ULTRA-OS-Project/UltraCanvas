# Audio Analysis Charts — Spectrum, Envelope & Correlogram

The DemoApp page **Charts → Audio Analysis Charts**
(`Apps/DemoApp/UltraCanvasAudioAnalysisExamples.cpp`) demonstrates that the
three classic audio-derived plots need no dedicated chart type — they are
ordinary data reductions rendered with the generic chart elements from
`Plugins/Charts/UltraCanvasSpecificChartElements.h`.

Each tab has a signal picker (the bundled sample track plus three synthetic
signals: chirp + tones, a 220 Hz tone with harmonics, and white noise), so the
characteristic shapes of each plot can be compared across signal types.

## 1. Spectrum plot (area chart)

Frequency content of the whole signal:

- The signal is run through the UI-free STFT engine
  (`ComputeSpectrogram`, `UltraCanvasSTFT.h`) with a Hann window and 50 %
  overlap, in *linear* magnitude mode.
- Magnitudes are averaged per frequency bin across all frames, normalized to
  the loudest bin, and converted to dB (floor −120 dB).
- One `ChartDataPoint` per bin (decimated to ≤ 512 points, x = frequency in
  Hz, sparse "N kHz" labels) rendered by `UltraCanvasAreaChartElement` with a
  gradient fill.

Steady tones appear as sharp peaks, the chirp as a raised plateau, white noise
as a flat floor.

## 2. Amplitude envelope (area chart)

Loudness over time — the same reduction the waveform element uses for its RMS
band, as a standalone chart:

- The signal is sliced into ~240 equal blocks; each block contributes one
  point with y = block RMS (`[0, 1]`), x = block start time in seconds.
- Rendered by `UltraCanvasAreaChartElement` with smoothing and a gradient
  fill.

## 3. Correlogram (bar chart)

Self-similarity of the signal at increasing lags — the classic pitch-detection
view:

- Normalized autocorrelation `r(lag) / r(0)` computed over the first second of
  the signal for lags up to 20 ms, decimated to ≤ 160 bars.
- Rendered by `UltraCanvasBarChartElement`, x = lag in milliseconds.

A pitched signal shows repeating peaks at multiples of its period (the 220 Hz
preset peaks every ~4.5 ms); white noise collapses to ~0 after lag 0.

## Related pages

- **Charts → Waveform Chart** — amplitude min/max envelope with playback.
- **Charts → Spectrogram Chart** — full time × frequency STFT heatmap with
  every transform option adjustable live.
- **Audio Elements → Level Meter / VU Strip** — live three-zone VU meter and
  scrolling waveform strip.
