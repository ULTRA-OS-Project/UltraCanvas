// include/Plugins/Charts/UltraCanvasSpectrogram.h
// Audio spectrogram element: a Short-Time Fourier Transform (STFT) in front of
// the generic heatmap element. Converts a 1D audio signal into a 2D
// time x frequency magnitude matrix and renders it as a heatmap.
//
// The STFT itself (ComputeSpectrogram / AudioToMonoFloat) is exposed as
// standalone, UI-free helpers so the transform can be reused independently.
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasSTFT.h"
#include "UltraCanvasAudio.h"
#include <vector>
#include <memory>

namespace UltraCanvas {

// SpectrogramWindow / SpectrogramMagnitude / SpectrogramParams / SpectrogramResult
// and the standalone ComputeSpectrogram / AudioToMonoFloat helpers live in
// UltraCanvasSTFT.h (the UI-free STFT engine).

// =============================================================================
// SPECTROGRAM ELEMENT (heatmap + STFT)
// =============================================================================

    class UltraCanvasSpectrogramElement : public UltraCanvasHeatmapChartElement {
    private:
        std::vector<float> signal;
        double sampleRate = 44100.0;
        SpectrogramParams params;
        bool needsRecompute = false;
        SpectrogramResult lastResult;

    public:
        UltraCanvasSpectrogramElement(const std::string& id, int x, int y, int width, int height);

        // ---- Input ----
        void SetSignal(const std::vector<float>& samples, double sampleRateHz);
        void SetAudio(const std::shared_ptr<UCAudio>& audio);

        // ---- STFT configuration (each marks the spectrogram for recompute) ----
        void SetParams(const SpectrogramParams& p);
        void SetFFTSize(int n);
        void SetHopSize(int n);
        void SetWindow(SpectrogramWindow w);
        void SetMagnitudeMode(SpectrogramMagnitude m);
        void SetDynamicRangeDb(double db);
        void SetMaxFrequency(double hz);
        const SpectrogramParams& GetParams() const { return params; }

        // ---- Access to the most recent transform ----
        const SpectrogramResult& GetResult() const { return lastResult; }

        // Run the STFT now and push the matrix into the heatmap. Normally called
        // lazily on the next render, but exposed for explicit control.
        void Recompute();

        // ---- Overrides ----
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        void MarkDirty();
        void ApplyResult(const SpectrogramResult& r);
    };

// =============================================================================
// FACTORY
// =============================================================================

    inline std::shared_ptr<UltraCanvasSpectrogramElement> CreateSpectrogramElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasSpectrogramElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
