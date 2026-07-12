// include/Plugins/Charts/UltraCanvasSTFT.h
// Short-Time Fourier Transform (STFT) engine: converts a 1D audio signal into a
// 2D time x frequency magnitude matrix. UI-free and reusable on its own; the
// spectrogram element (UltraCanvasSpectrogram.h) renders the result as a heatmap.
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasAudio.h"
#include <vector>
#include <cstddef>

namespace UltraCanvas {

// =============================================================================
// STFT PARAMETERS / RESULT
// =============================================================================

    enum class SpectrogramWindow {
        Rectangular,
        Hann,        // good general-purpose default
        Hamming,
        Blackman
    };

    enum class SpectrogramMagnitude {
        Linear,      // raw magnitude (pair with a logarithmic colour scale)
        Decibels     // 20*log10(mag / peak), clamped to [-dynamicRange, 0]
    };

    struct SpectrogramParams {
        int fftSize = 1024;                 // window length / FFT size (even; ideally a power of two)
        int hopSize = 256;                  // samples advanced between frames (<= 0 => fftSize/4)
        SpectrogramWindow window = SpectrogramWindow::Hann;
        SpectrogramMagnitude magnitude = SpectrogramMagnitude::Decibels;
        double dynamicRangeDb = 80.0;       // dB floor (values clamped to [-range, 0]) in Decibels mode
        double maxFrequency = 0.0;          // 0 => Nyquist; otherwise cap the displayed frequency bins
    };

    struct SpectrogramResult {
        int frames = 0;                     // columns (time)
        int bins = 0;                       // rows (frequency)
        std::vector<double> magnitudes;     // row-major: magnitudes[bin * frames + frame]
        double sampleRate = 0.0;
        int fftSize = 0;
        int hopSize = 0;
        double minValue = 0.0;
        double maxValue = 0.0;

        bool IsValid() const { return frames > 0 && bins > 0 && !magnitudes.empty(); }
        double TimeAtFrame(int frame) const;        // seconds, window centre
        double FrequencyAtBin(int bin) const;       // Hz
    };

// =============================================================================
// STANDALONE STFT HELPERS (no UI dependency)
// =============================================================================

// Compute a magnitude spectrogram from a mono float signal in [-1, 1].
// Returns false if parameters/signal are invalid (e.g. signal shorter than one
// window, fftSize not even, sampleRate <= 0).
    bool ComputeSpectrogram(const float* signal, size_t sampleCount, double sampleRate,
                            const SpectrogramParams& params, SpectrogramResult& out);

// Downmix decoded PCM (any supported sample type / channel count) to a mono
// float buffer in [-1, 1].
    std::vector<float> AudioToMonoFloat(const UCAudio& audio);

} // namespace UltraCanvas
