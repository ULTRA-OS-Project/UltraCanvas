// Plugins/Charts/UltraCanvasSTFT.cpp
// Short-Time Fourier Transform engine (UI-free) backed by vendored KissFFT.
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasSTFT.h"
#include "../../libspecific/FFT/kiss_fftr.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// WINDOW FUNCTIONS
// =============================================================================

    static void BuildWindow(SpectrogramWindow type, std::vector<float>& win) {
        int n = static_cast<int>(win.size());
        if (n <= 0) return;
        double denom = (n > 1) ? (n - 1) : 1;
        for (int i = 0; i < n; ++i) {
            double w;
            switch (type) {
                case SpectrogramWindow::Rectangular:
                    w = 1.0;
                    break;
                case SpectrogramWindow::Hamming:
                    w = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / denom);
                    break;
                case SpectrogramWindow::Blackman:
                    w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / denom)
                        + 0.08 * std::cos(4.0 * M_PI * i / denom);
                    break;
                case SpectrogramWindow::Hann:
                default:
                    w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / denom));
                    break;
            }
            win[i] = static_cast<float>(w);
        }
    }

// =============================================================================
// RESULT ACCESSORS
// =============================================================================

    double SpectrogramResult::TimeAtFrame(int frame) const {
        if (sampleRate <= 0.0) return 0.0;
        // Centre of the analysis window for this frame.
        return (static_cast<double>(frame) * hopSize + fftSize / 2.0) / sampleRate;
    }

    double SpectrogramResult::FrequencyAtBin(int bin) const {
        if (fftSize <= 0) return 0.0;
        return static_cast<double>(bin) * sampleRate / fftSize;
    }

// =============================================================================
// STFT
// =============================================================================

    bool ComputeSpectrogram(const float* signal, size_t sampleCount, double sampleRate,
                            const SpectrogramParams& params, SpectrogramResult& out) {
        out = SpectrogramResult{};

        const int nfft = params.fftSize;
        if (!signal || nfft < 2 || (nfft % 2) != 0 || sampleRate <= 0.0) return false;

        const int hop = (params.hopSize > 0) ? params.hopSize : std::max(1, nfft / 4);
        if (sampleCount < static_cast<size_t>(nfft)) return false;

        const int fullBins = nfft / 2 + 1;
        int bins = fullBins;
        if (params.maxFrequency > 0.0) {
            int limit = static_cast<int>(std::floor(params.maxFrequency / sampleRate * nfft)) + 1;
            bins = std::clamp(limit, 1, fullBins);
        }

        const int frames = 1 + static_cast<int>((sampleCount - nfft) / hop);
        if (frames <= 0) return false;

        std::vector<float> win(nfft);
        BuildWindow(params.window, win);

        kiss_fftr_cfg cfg = kiss_fftr_alloc(nfft, 0, nullptr, nullptr);
        if (!cfg) return false;

        std::vector<kiss_fft_scalar> frame(nfft);
        std::vector<kiss_fft_cpx> spec(fullBins);
        std::vector<double> mags(static_cast<size_t>(bins) * frames, 0.0);

        double globalMax = 0.0;
        for (int f = 0; f < frames; ++f) {
            const size_t base = static_cast<size_t>(f) * hop;
            for (int i = 0; i < nfft; ++i) {
                frame[i] = signal[base + i] * win[i];
            }
            kiss_fftr(cfg, frame.data(), spec.data());
            for (int b = 0; b < bins; ++b) {
                double re = spec[b].r;
                double im = spec[b].i;
                double m = std::sqrt(re * re + im * im);
                mags[static_cast<size_t>(b) * frames + f] = m;
                if (m > globalMax) globalMax = m;
            }
        }
        free(cfg);

        if (params.magnitude == SpectrogramMagnitude::Decibels) {
            const double ref = (globalMax > 0.0) ? globalMax : 1.0;
            const double floorDb = -std::abs(params.dynamicRangeDb);
            for (double& v : mags) {
                double db = 20.0 * std::log10((v / ref) + 1e-12);
                v = std::clamp(db, floorDb, 0.0);
            }
            out.minValue = floorDb;
            out.maxValue = 0.0;
        } else {
            double lo = std::numeric_limits<double>::infinity();
            double hi = 0.0;
            for (double v : mags) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            out.minValue = std::isfinite(lo) ? lo : 0.0;
            out.maxValue = hi;
        }

        out.frames = frames;
        out.bins = bins;
        out.magnitudes = std::move(mags);
        out.sampleRate = sampleRate;
        out.fftSize = nfft;
        out.hopSize = hop;
        return true;
    }

// =============================================================================
// PCM -> MONO FLOAT
// =============================================================================

    std::vector<float> AudioToMonoFloat(const UCAudio& audio) {
        std::vector<float> mono;
        if (!audio.IsValid()) return mono;

        const AudioBufferInfo& info = audio.GetInfo();
        const int channels = std::max(1, info.channels);
        const size_t frames = info.frameCount;
        const uint8_t* data = audio.GetData();
        if (!data || frames == 0) return mono;

        mono.resize(frames, 0.0f);

        auto sampleAt = [&](size_t frameIdx, int ch) -> double {
            switch (info.sampleType) {
                case AudioSampleType::PCM_S16: {
                    const int16_t* p = reinterpret_cast<const int16_t*>(data);
                    return p[frameIdx * channels + ch] / 32768.0;
                }
                case AudioSampleType::PCM_S24: {
                    const uint8_t* p = data + (frameIdx * channels + ch) * 3;
                    int32_t v = (static_cast<int32_t>(p[0])) |
                                (static_cast<int32_t>(p[1]) << 8) |
                                (static_cast<int32_t>(p[2]) << 16);
                    if (v & 0x800000) v |= ~0xFFFFFF; // sign-extend 24 -> 32 bit
                    return v / 8388608.0;
                }
                case AudioSampleType::PCM_S32: {
                    const int32_t* p = reinterpret_cast<const int32_t*>(data);
                    return p[frameIdx * channels + ch] / 2147483648.0;
                }
                case AudioSampleType::PCM_F32:
                default: {
                    const float* p = reinterpret_cast<const float*>(data);
                    return p[frameIdx * channels + ch];
                }
            }
        };

        for (size_t i = 0; i < frames; ++i) {
            double acc = 0.0;
            for (int ch = 0; ch < channels; ++ch) {
                acc += sampleAt(i, ch);
            }
            mono[i] = static_cast<float>(acc / channels);
        }
        return mono;
    }

} // namespace UltraCanvas
