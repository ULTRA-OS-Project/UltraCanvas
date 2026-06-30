// Plugins/Charts/UltraCanvasSpectrogram.cpp
// Audio spectrogram element (STFT in front of the heatmap element).
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

// =============================================================================
// SPECTROGRAM ELEMENT
// =============================================================================

    UltraCanvasSpectrogramElement::UltraCanvasSpectrogramElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasHeatmapChartElement(id, x, y, width, height) {
        // Spectrogram-friendly heatmap defaults.
        SetColormap(HeatmapColormap::Inferno);
        SetRowOrder(HeatmapRowOrder::BottomUp);   // low frequencies at the bottom
        SetRenderMode(HeatmapRenderMode::Image);  // large grids -> scaled blit
        SetShowColorBar(true);
        SetAxisTitles("Time (s)", "Frequency (Hz)");
    }

    void UltraCanvasSpectrogramElement::MarkDirty() {
        needsRecompute = true;
        RequestRedraw();
    }

    void UltraCanvasSpectrogramElement::SetSignal(const std::vector<float>& samples, double sampleRateHz) {
        signal = samples;
        sampleRate = (sampleRateHz > 0.0) ? sampleRateHz : 44100.0;
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetAudio(const std::shared_ptr<UCAudio>& audio) {
        if (!audio || !audio->IsValid()) {
            signal.clear();
            MarkDirty();
            return;
        }
        signal = AudioToMonoFloat(*audio);
        sampleRate = audio->GetSampleRate() > 0 ? audio->GetSampleRate() : 44100.0;
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetParams(const SpectrogramParams& p) {
        params = p;
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetFFTSize(int n) {
        params.fftSize = std::max(2, n);
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetHopSize(int n) {
        params.hopSize = n;
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetWindow(SpectrogramWindow w) {
        params.window = w;
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetMagnitudeMode(SpectrogramMagnitude m) {
        params.magnitude = m;
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetDynamicRangeDb(double db) {
        params.dynamicRangeDb = std::abs(db);
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::SetMaxFrequency(double hz) {
        params.maxFrequency = std::max(0.0, hz);
        MarkDirty();
    }

    void UltraCanvasSpectrogramElement::ApplyResult(const SpectrogramResult& r) {
        if (!r.IsValid()) {
            ClearData();
            return;
        }

        SetData(r.magnitudes, r.frames, r.bins);

        if (params.magnitude == SpectrogramMagnitude::Decibels) {
            SetScale(HeatmapScale::Linear);
            SetValueRange(r.minValue, r.maxValue);
        } else {
            SetScale(HeatmapScale::Logarithmic);
            SetAutoValueRange(true);
        }

        // Time labels (columns) and frequency labels (rows).
        std::vector<std::string> colLabels(r.frames);
        std::vector<std::string> rowLabels(r.bins);
        char buf[32];
        for (int f = 0; f < r.frames; ++f) {
            std::snprintf(buf, sizeof(buf), "%.2f", r.TimeAtFrame(f));
            colLabels[f] = buf;
        }
        for (int b = 0; b < r.bins; ++b) {
            double hz = r.FrequencyAtBin(b);
            if (hz >= 1000.0) {
                std::snprintf(buf, sizeof(buf), "%.1fk", hz / 1000.0);
            } else {
                std::snprintf(buf, sizeof(buf), "%.0f", hz);
            }
            rowLabels[b] = buf;
        }
        SetColumnLabels(colLabels);
        SetRowLabels(rowLabels);
    }

    void UltraCanvasSpectrogramElement::Recompute() {
        needsRecompute = false;
        SpectrogramResult result;
        if (signal.empty() ||
            !ComputeSpectrogram(signal.data(), signal.size(), sampleRate, params, result)) {
            lastResult = SpectrogramResult{};
            ClearData();
            return;
        }
        lastResult = result;
        ApplyResult(result);
    }

    void UltraCanvasSpectrogramElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (needsRecompute) {
            Recompute();
        }
        UltraCanvasHeatmapChartElement::Render(ctx, dirtyRect);
    }

} // namespace UltraCanvas
