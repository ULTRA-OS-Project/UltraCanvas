// Apps/DemoApp/UltraCanvasAudioFXExamples.cpp
// AudioFX module demo page. A horizontal tab bar splits the module into
// three views (same structure as the PixelFX / FileLoader demos):
//   * Overview  – the short intro, the architecture diagram and a Markdown
//                 table of the effect categories exposed by the playground.
//   * Details   – the full README documentation.
//   * Examples  – a live playground: pick a sample track (the_mountain-
//                 cinematic-489998.mp3 by default) or upload a custom one,
//                 choose an effect from the category tree in the middle, tune
//                 its options on the right and hear / see the processed result
//                 in the waveform + player on the left.
// Version: 1.0.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWaveformElement.h"
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // ===== WORKING AUDIO BUFFER =====
    // The playground processes audio as interleaved 32-bit float samples in
    // [-1, 1] regardless of the source PCM type — every effect below is a pure
    // FloatAudio -> FloatAudio function.
    struct FloatAudio {
        int sampleRate = 44100;
        int channels = 2;
        std::vector<float> samples;   // interleaved, Frames() * channels entries

        size_t Frames() const { return channels > 0 ? samples.size() / channels : 0; }
        double Duration() const { return sampleRate > 0 ? static_cast<double>(Frames()) / sampleRate : 0.0; }
        bool IsValid() const { return sampleRate > 0 && channels > 0 && !samples.empty(); }
    };

    // Decode any UCAudio PCM type into the float working buffer.
    FloatAudio ToFloatAudio(const std::shared_ptr<UCAudio>& audio) {
        FloatAudio out;
        if (!audio || !audio->IsValid()) return out;

        const AudioBufferInfo& info = audio->GetInfo();
        out.sampleRate = info.sampleRate;
        out.channels   = std::max(1, info.channels);
        const size_t total = info.frameCount * out.channels;
        out.samples.resize(total);

        const uint8_t* bytes = audio->GetData();
        switch (info.sampleType) {
            case AudioSampleType::PCM_S16: {
                const int16_t* s = reinterpret_cast<const int16_t*>(bytes);
                for (size_t i = 0; i < total; ++i) out.samples[i] = s[i] / 32768.0f;
                break;
            }
            case AudioSampleType::PCM_S32: {
                const int32_t* s = reinterpret_cast<const int32_t*>(bytes);
                for (size_t i = 0; i < total; ++i)
                    out.samples[i] = static_cast<float>(s[i] / 2147483648.0);
                break;
            }
            case AudioSampleType::PCM_F32: {
                const float* s = reinterpret_cast<const float*>(bytes);
                for (size_t i = 0; i < total; ++i) out.samples[i] = s[i];
                break;
            }
            case AudioSampleType::PCM_S24: {
                for (size_t i = 0; i < total; ++i) {
                    const uint8_t* p = bytes + i * 3;
                    int32_t v = (p[0]) | (p[1] << 8) | (p[2] << 16);
                    if (v & 0x800000) v |= ~0xFFFFFF;   // sign-extend
                    out.samples[i] = static_cast<float>(v / 8388608.0);
                }
                break;
            }
        }
        return out;
    }

    // Pack the float buffer back into a 16-bit UCAudio the player can consume.
    std::shared_ptr<UCAudio> ToUCAudio(const FloatAudio& audio) {
        std::vector<int16_t> pcm(audio.samples.size());
        for (size_t i = 0; i < audio.samples.size(); ++i) {
            float v = std::clamp(audio.samples[i], -1.0f, 1.0f);
            pcm[i] = static_cast<int16_t>(std::lround(v * 32767.0f));
        }
        AudioBufferInfo info;
        info.sampleRate = audio.sampleRate;
        info.channels = audio.channels;
        info.sampleType = AudioSampleType::PCM_S16;
        info.frameCount = audio.Frames();
        info.durationSeconds = audio.Duration();
        return UCAudio::FromRawPCM(pcm.data(), pcm.size() * sizeof(int16_t), info);
    }

    // Summarise the buffer as a min/max/RMS envelope for the waveform chart
    // (channels down-mixed to mono, same scheme as the Waveform chart demo).
    WaveformChannelData BuildWaveform(const FloatAudio& audio, int targetBlocks = 2000) {
        WaveformChannelData wf;
        const size_t frames = audio.Frames();
        if (frames == 0) return wf;

        const int blocks = std::min<int>(targetBlocks, static_cast<int>(frames));
        const size_t framesPerBlock = std::max<size_t>(1, frames / blocks);
        wf.minValues.reserve(blocks);
        wf.maxValues.reserve(blocks);
        wf.rmsValues.reserve(blocks);

        for (int b = 0; b < blocks; ++b) {
            size_t start = static_cast<size_t>(b) * framesPerBlock;
            size_t end   = (b == blocks - 1) ? frames
                                             : std::min(frames, start + framesPerBlock);
            float mn = 1.0f, mx = -1.0f;
            double sumSq = 0.0;
            size_t count = 0;
            for (size_t f = start; f < end; ++f) {
                float mono = 0.0f;
                for (int c = 0; c < audio.channels; ++c)
                    mono += audio.samples[f * audio.channels + c];
                mono /= audio.channels;
                mn = std::min(mn, mono);
                mx = std::max(mx, mono);
                sumSq += static_cast<double>(mono) * mono;
                ++count;
            }
            if (count == 0) { mn = 0.0f; mx = 0.0f; }
            wf.minValues.push_back(mn);
            wf.maxValues.push_back(mx);
            wf.rmsValues.push_back(static_cast<float>(count ? std::sqrt(sumSq / count) : 0.0));
        }

        wf.samplesPerBlock = static_cast<int>(framesPerBlock);
        wf.totalBlocks = static_cast<int>(wf.maxValues.size());
        wf.duration = audio.Duration();
        return wf;
    }

    // ===== DSP HELPERS =====
    float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

    float Peak(const FloatAudio& audio) {
        float peak = 0.0f;
        for (float s : audio.samples) peak = std::max(peak, std::fabs(s));
        return peak;
    }

    void Scale(FloatAudio& audio, float factor) {
        for (float& s : audio.samples) s *= factor;
    }

    // RBJ audio-EQ-cookbook biquad, one instance per channel.
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        float Process(float x) {
            double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return static_cast<float>(y);
        }

        static Biquad LowPass(int sr, double freq, double q) {
            double w = 2.0 * M_PI * freq / sr, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
            Biquad f;
            double a0 = 1 + alpha;
            f.b0 = (1 - cw) / 2 / a0; f.b1 = (1 - cw) / a0; f.b2 = f.b0;
            f.a1 = -2 * cw / a0;      f.a2 = (1 - alpha) / a0;
            return f;
        }
        static Biquad HighPass(int sr, double freq, double q) {
            double w = 2.0 * M_PI * freq / sr, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
            Biquad f;
            double a0 = 1 + alpha;
            f.b0 = (1 + cw) / 2 / a0; f.b1 = -(1 + cw) / a0; f.b2 = f.b0;
            f.a1 = -2 * cw / a0;      f.a2 = (1 - alpha) / a0;
            return f;
        }
        static Biquad BandPass(int sr, double freq, double q) {
            double w = 2.0 * M_PI * freq / sr, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
            Biquad f;   // constant 0 dB peak gain
            double a0 = 1 + alpha;
            f.b0 = alpha / a0; f.b1 = 0; f.b2 = -alpha / a0;
            f.a1 = -2 * cw / a0; f.a2 = (1 - alpha) / a0;
            return f;
        }
        static Biquad LowShelf(int sr, double freq, double gainDb) {
            double A = std::pow(10.0, gainDb / 40.0);
            double w = 2.0 * M_PI * freq / sr, cw = std::cos(w);
            double alpha = std::sin(w) / 2.0 * std::sqrt(2.0);   // S = 1
            double sq = 2.0 * std::sqrt(A) * alpha;
            Biquad f;
            double a0 = (A + 1) + (A - 1) * cw + sq;
            f.b0 = A * ((A + 1) - (A - 1) * cw + sq) / a0;
            f.b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
            f.b2 = A * ((A + 1) - (A - 1) * cw - sq) / a0;
            f.a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
            f.a2 = ((A + 1) + (A - 1) * cw - sq) / a0;
            return f;
        }
    };

    FloatAudio ApplyBiquad(const FloatAudio& src, const std::function<Biquad()>& make) {
        FloatAudio out = src;
        for (int c = 0; c < src.channels; ++c) {
            Biquad f = make();
            for (size_t i = c; i < out.samples.size(); i += src.channels)
                out.samples[i] = f.Process(out.samples[i]);
        }
        return out;
    }

    // ===== EFFECT IMPLEMENTATIONS =====
    FloatAudio FxGain(const FloatAudio& src, float db) {
        FloatAudio out = src;
        Scale(out, DbToLin(db));
        return out;
    }

    FloatAudio FxNormalize(const FloatAudio& src, float targetDb) {
        FloatAudio out = src;
        float peak = Peak(out);
        if (peak > 1e-6f) Scale(out, DbToLin(targetDb) / peak);
        return out;
    }

    FloatAudio FxFade(const FloatAudio& src, float seconds, bool fadeIn) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const size_t fadeFrames = std::min<size_t>(
            frames, static_cast<size_t>(seconds * out.sampleRate));
        for (size_t f = 0; f < fadeFrames; ++f) {
            float g = fadeFrames ? static_cast<float>(f) / fadeFrames : 1.0f;
            size_t frame = fadeIn ? f : frames - 1 - f;
            for (int c = 0; c < out.channels; ++c)
                out.samples[frame * out.channels + c] *= g;
        }
        return out;
    }

    // Stereo-linked feed-forward compressor with fixed 5 ms attack / 150 ms
    // release and automatic makeup gain.
    FloatAudio FxCompress(const FloatAudio& src, float thresholdDb, float ratio) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const float attack  = std::exp(-1.0f / (0.005f * out.sampleRate));
        const float release = std::exp(-1.0f / (0.150f * out.sampleRate));
        const float makeup  = DbToLin(-thresholdDb * (1.0f - 1.0f / ratio) * 0.5f);
        float env = 0.0f;
        for (size_t f = 0; f < frames; ++f) {
            float level = 0.0f;
            for (int c = 0; c < out.channels; ++c)
                level = std::max(level, std::fabs(out.samples[f * out.channels + c]));
            float coef = level > env ? attack : release;
            env = coef * env + (1.0f - coef) * level;
            float levelDb = 20.0f * std::log10(std::max(env, 1e-6f));
            float over = levelDb - thresholdDb;
            float gain = over > 0.0f ? DbToLin(-over * (1.0f - 1.0f / ratio)) : 1.0f;
            gain *= makeup;
            for (int c = 0; c < out.channels; ++c)
                out.samples[f * out.channels + c] *= gain;
        }
        return out;
    }

    // Feedback delay: wet[n] = x[n-d] + feedback * wet[n-d], mixed onto the dry
    // signal.
    FloatAudio FxEcho(const FloatAudio& src, float delayMs, float feedback, float mix) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const size_t d = std::max<size_t>(1, static_cast<size_t>(delayMs * 0.001f * out.sampleRate));
        for (int c = 0; c < out.channels; ++c) {
            std::vector<float> wet(frames, 0.0f);
            for (size_t f = d; f < frames; ++f) {
                wet[f] = src.samples[(f - d) * src.channels + c] + feedback * wet[f - d];
                out.samples[f * out.channels + c] =
                    src.samples[f * src.channels + c] + mix * wet[f];
            }
        }
        return out;
    }

    // Schroeder/freeverb-style reverb: four damped feedback combs in parallel
    // followed by two all-pass diffusers, per channel (right channel delays are
    // offset for stereo spread).
    FloatAudio FxReverb(const FloatAudio& src, float roomSize, float mix) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const double srScale = out.sampleRate / 44100.0;
        const float feedback = 0.75f + 0.23f * std::clamp(roomSize, 0.0f, 1.0f);
        const float damp = 0.25f;

        struct Comb { std::vector<float> buf; size_t idx = 0; float store = 0.0f; };
        struct AllPass { std::vector<float> buf; size_t idx = 0; };
        static const int combBase[4] = { 1116, 1188, 1277, 1356 };
        static const int apBase[2]   = { 556, 441 };

        for (int c = 0; c < out.channels; ++c) {
            const int spread = c * 23;   // decorrelate channels
            Comb combs[4];
            for (int k = 0; k < 4; ++k)
                combs[k].buf.assign(static_cast<size_t>((combBase[k] + spread) * srScale) + 1, 0.0f);
            AllPass aps[2];
            for (int k = 0; k < 2; ++k)
                aps[k].buf.assign(static_cast<size_t>((apBase[k] + spread) * srScale) + 1, 0.0f);

            for (size_t f = 0; f < frames; ++f) {
                float input = src.samples[f * src.channels + c];
                float wet = 0.0f;
                for (auto& cb : combs) {
                    float y = cb.buf[cb.idx];
                    cb.store = y * (1.0f - damp) + cb.store * damp;
                    cb.buf[cb.idx] = input + cb.store * feedback;
                    cb.idx = (cb.idx + 1) % cb.buf.size();
                    wet += y;
                }
                wet *= 0.25f;
                for (auto& ap : aps) {
                    float buffered = ap.buf[ap.idx];
                    float y = -wet + buffered;
                    ap.buf[ap.idx] = wet + buffered * 0.5f;
                    ap.idx = (ap.idx + 1) % ap.buf.size();
                    wet = y;
                }
                out.samples[f * out.channels + c] = input * (1.0f - mix) + wet * mix;
            }
        }
        return out;
    }

    FloatAudio FxTremolo(const FloatAudio& src, float rateHz, float depth) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        for (size_t f = 0; f < frames; ++f) {
            float lfo = 0.5f + 0.5f * std::sin(2.0 * M_PI * rateHz * f / out.sampleRate);
            float gain = 1.0f - depth * lfo;
            for (int c = 0; c < out.channels; ++c)
                out.samples[f * out.channels + c] *= gain;
        }
        return out;
    }

    // Read a sample `delay` (fractional) frames back with linear interpolation.
    float DelayedSample(const FloatAudio& a, size_t frame, int ch, double delay) {
        double pos = static_cast<double>(frame) - delay;
        if (pos < 0.0) return 0.0f;
        size_t i0 = static_cast<size_t>(pos);
        size_t i1 = std::min(i0 + 1, a.Frames() - 1);
        float t = static_cast<float>(pos - i0);
        float s0 = a.samples[i0 * a.channels + ch];
        float s1 = a.samples[i1 * a.channels + ch];
        return s0 + (s1 - s0) * t;
    }

    FloatAudio FxFlanger(const FloatAudio& src, float rateHz, float depthMs) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const double depth = depthMs * 0.001 * out.sampleRate;
        for (size_t f = 0; f < frames; ++f) {
            double lfo = 0.5 + 0.5 * std::sin(2.0 * M_PI * rateHz * f / out.sampleRate);
            double d = 1.0 + lfo * depth;
            for (int c = 0; c < out.channels; ++c) {
                float dry = src.samples[f * src.channels + c];
                out.samples[f * out.channels + c] =
                    0.7f * dry + 0.7f * DelayedSample(src, f, c, d);
            }
        }
        return out;
    }

    FloatAudio FxVibrato(const FloatAudio& src, float rateHz, float depthMs) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const double depth = depthMs * 0.001 * out.sampleRate;
        for (size_t f = 0; f < frames; ++f) {
            double lfo = 0.5 + 0.5 * std::sin(2.0 * M_PI * rateHz * f / out.sampleRate);
            double d = 1.0 + lfo * depth;
            for (int c = 0; c < out.channels; ++c)
                out.samples[f * out.channels + c] = DelayedSample(src, f, c, d);
        }
        return out;
    }

    // tanh waveshaper; the output is re-normalised to the input peak so the
    // drive slider changes character, not loudness.
    FloatAudio FxOverdrive(const FloatAudio& src, float drive) {
        FloatAudio out = src;
        for (float& s : out.samples) s = std::tanh(drive * s);
        float inPeak = Peak(src), outPeak = Peak(out);
        if (outPeak > 1e-6f) Scale(out, inPeak / outPeak);
        return out;
    }

    FloatAudio FxBitCrush(const FloatAudio& src, float bits, float downsample) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        const float levels = std::pow(2.0f, std::round(bits)) - 1.0f;
        const size_t hold = std::max<size_t>(1, static_cast<size_t>(downsample));
        std::vector<float> held(out.channels, 0.0f);
        for (size_t f = 0; f < frames; ++f) {
            for (int c = 0; c < out.channels; ++c) {
                if (f % hold == 0) {
                    float v = std::clamp(src.samples[f * src.channels + c], -1.0f, 1.0f);
                    held[c] = std::round((v + 1.0f) * 0.5f * levels) / levels * 2.0f - 1.0f;
                }
                out.samples[f * out.channels + c] = held[c];
            }
        }
        return out;
    }

    // Linear-interpolation resample: both tempo and pitch change together,
    // like a tape machine.
    FloatAudio FxSpeed(const FloatAudio& src, float factor) {
        FloatAudio out;
        out.sampleRate = src.sampleRate;
        out.channels = src.channels;
        const size_t frames = src.Frames();
        const size_t outFrames = std::max<size_t>(1, static_cast<size_t>(frames / factor));
        out.samples.resize(outFrames * out.channels);
        for (size_t f = 0; f < outFrames; ++f) {
            double pos = f * static_cast<double>(factor);
            size_t i0 = std::min(static_cast<size_t>(pos), frames - 1);
            size_t i1 = std::min(i0 + 1, frames - 1);
            float t = static_cast<float>(pos - i0);
            for (int c = 0; c < out.channels; ++c) {
                float s0 = src.samples[i0 * src.channels + c];
                float s1 = src.samples[i1 * src.channels + c];
                out.samples[f * out.channels + c] = s0 + (s1 - s0) * t;
            }
        }
        return out;
    }

    FloatAudio FxReverse(const FloatAudio& src) {
        FloatAudio out = src;
        const size_t frames = out.Frames();
        for (size_t f = 0; f < frames; ++f)
            for (int c = 0; c < out.channels; ++c)
                out.samples[f * out.channels + c] =
                    src.samples[(frames - 1 - f) * src.channels + c];
        return out;
    }

    // ===== EFFECT MODEL =====
    // One adjustable parameter of an effect, rendered as a slider.
    struct EffectParam {
        std::string label;
        float minValue;
        float maxValue;
        float defValue;
        float step;
    };

    // A single AudioFX effect: its parameters plus the DSP call. `apply`
    // receives the source buffer and the current slider values (same order as
    // `params`).
    struct Effect {
        std::string id;
        std::string name;
        std::string description;
        std::vector<EffectParam> params;
        std::function<FloatAudio(const FloatAudio&, const std::vector<float>&)> apply;
    };

    struct EffectCategory {
        std::string id;
        std::string name;
        std::vector<Effect> effects;
    };

    // The effects offered by the playground, grouped the same way as the
    // AudioFX operation families (Dynamics, Filters, Delay, Modulation, …).
    const std::vector<EffectCategory>& EffectCategories() {
        static const std::vector<EffectCategory> categories = {
            { "dynamics", "Volume & dynamics", {
                { "gain", "Gain", "Amplifies or attenuates the signal by the chosen amount (AudioFX_Gain).",
                  { { "Gain dB", -24.0f, 24.0f, 6.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxGain(src, p[0]); } },
                { "normalize", "Normalize", "Scales the track so its peak hits the target level (AudioFX_Normalize).",
                  { { "Peak dBFS", -12.0f, 0.0f, -3.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxNormalize(src, p[0]); } },
                { "compress", "Compressor", "Reduces dynamic range above the threshold with auto makeup gain (AudioFX_Compress).",
                  { { "Threshold dB", -60.0f, 0.0f, -24.0f, 1.0f },
                    { "Ratio", 1.0f, 20.0f, 4.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxCompress(src, p[0], p[1]); } },
                { "fadein", "Fade in", "Ramps the volume up from silence at the start (AudioFX_FadeIn).",
                  { { "Seconds", 0.5f, 15.0f, 4.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxFade(src, p[0], true); } },
                { "fadeout", "Fade out", "Ramps the volume down to silence at the end (AudioFX_FadeOut).",
                  { { "Seconds", 0.5f, 15.0f, 4.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxFade(src, p[0], false); } },
            } },
            { "filters", "Filters & EQ", {
                { "lowpass", "Low-pass", "Keeps frequencies below the cutoff — muffles the highs (AudioFX_LowPass).",
                  { { "Cutoff Hz", 200.0f, 15000.0f, 1200.0f, 50.0f } },
                  [](const FloatAudio& src, const std::vector<float>& p) {
                      int sr = src.sampleRate; float freq = p[0];
                      return ApplyBiquad(src, [sr, freq]() { return Biquad::LowPass(sr, freq, 0.7071); }); } },
                { "highpass", "High-pass", "Keeps frequencies above the cutoff — thins out the lows (AudioFX_HighPass).",
                  { { "Cutoff Hz", 40.0f, 8000.0f, 800.0f, 20.0f } },
                  [](const FloatAudio& src, const std::vector<float>& p) {
                      int sr = src.sampleRate; float freq = p[0];
                      return ApplyBiquad(src, [sr, freq]() { return Biquad::HighPass(sr, freq, 0.7071); }); } },
                { "bandpass", "Band-pass", "Isolates a frequency band — classic telephone / radio voicing (AudioFX_BandPass).",
                  { { "Centre Hz", 100.0f, 8000.0f, 1000.0f, 50.0f },
                    { "Q", 0.5f, 8.0f, 2.0f, 0.1f } },
                  [](const FloatAudio& src, const std::vector<float>& p) {
                      int sr = src.sampleRate; float freq = p[0], q = p[1];
                      return ApplyBiquad(src, [sr, freq, q]() { return Biquad::BandPass(sr, freq, q); }); } },
                { "bassboost", "Bass boost", "Low-shelf filter at 200 Hz that lifts the low end (AudioFX_ShelvingEQ).",
                  { { "Boost dB", 0.0f, 18.0f, 9.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) {
                      int sr = src.sampleRate; float gain = p[0];
                      return ApplyBiquad(src, [sr, gain]() { return Biquad::LowShelf(sr, 200.0, gain); }); } },
            } },
            { "delay", "Delay & reverb", {
                { "echo", "Echo", "Repeats the signal after a delay with adjustable feedback (AudioFX_Delay).",
                  { { "Delay ms", 50.0f, 1000.0f, 350.0f, 10.0f },
                    { "Feedback", 0.0f, 0.9f, 0.45f, 0.05f },
                    { "Mix", 0.0f, 1.0f, 0.5f, 0.05f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxEcho(src, p[0], p[1], p[2]); } },
                { "reverb", "Reverb", "Schroeder comb/all-pass network that adds room ambience (AudioFX_Reverb).",
                  { { "Room size", 0.0f, 1.0f, 0.6f, 0.05f },
                    { "Mix", 0.0f, 1.0f, 0.35f, 0.05f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxReverb(src, p[0], p[1]); } },
            } },
            { "modulation", "Modulation", {
                { "tremolo", "Tremolo", "Modulates the volume with a low-frequency oscillator (AudioFX_Tremolo).",
                  { { "Rate Hz", 0.5f, 20.0f, 5.0f, 0.5f },
                    { "Depth", 0.0f, 1.0f, 0.8f, 0.05f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxTremolo(src, p[0], p[1]); } },
                { "flanger", "Flanger", "Mixes in a copy through a slowly sweeping short delay (AudioFX_Flanger).",
                  { { "Rate Hz", 0.1f, 5.0f, 0.5f, 0.1f },
                    { "Depth ms", 0.5f, 5.0f, 2.0f, 0.25f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxFlanger(src, p[0], p[1]); } },
                { "vibrato", "Vibrato", "Periodically bends the pitch via a modulated delay line (AudioFX_Vibrato).",
                  { { "Rate Hz", 0.5f, 10.0f, 5.0f, 0.5f },
                    { "Depth ms", 0.5f, 8.0f, 3.0f, 0.25f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxVibrato(src, p[0], p[1]); } },
            } },
            { "distortion", "Distortion & lo-fi", {
                { "overdrive", "Overdrive", "Soft-clips the waveform with a tanh curve for warm saturation (AudioFX_Saturation).",
                  { { "Drive", 1.0f, 20.0f, 6.0f, 0.5f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxOverdrive(src, p[0]); } },
                { "bitcrush", "Bit crush", "Reduces bit depth and sample rate for retro digital grit (AudioFX_BitCrush).",
                  { { "Bits", 2.0f, 16.0f, 6.0f, 1.0f },
                    { "Downsample", 1.0f, 16.0f, 4.0f, 1.0f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxBitCrush(src, p[0], p[1]); } },
            } },
            { "timepitch", "Time & pitch", {
                { "speed", "Speed", "Tape-style resampling — tempo and pitch shift together (AudioFX_TimeStretch).",
                  { { "Factor", 0.25f, 3.0f, 1.5f, 0.05f } },
                  [](const FloatAudio& src, const std::vector<float>& p) { return FxSpeed(src, p[0]); } },
                { "reverse", "Reverse", "Plays the track backwards (AudioFX_Reverse).", {},
                  [](const FloatAudio& src, const std::vector<float>&) { return FxReverse(src); } },
            } },
        };
        return categories;
    }

    const Effect* FindEffect(const std::string& id) {
        for (const auto& cat : EffectCategories())
            for (const auto& fx : cat.effects)
                if (fx.id == id) return &fx;
        return nullptr;
    }

    // ===== PLAYGROUND STATE =====
    // Shared by every callback of the Examples tab.
    struct PlaygroundState {
        std::string audioPath;
        FloatAudio original;
        const Effect* effect = nullptr;
        std::vector<float> params;
    };

    // Longest stretch of audio the playground processes. Every slider tick
    // re-runs the effect over the whole buffer, so very long uploads are
    // truncated to keep the sliders responsive.
    constexpr double kMaxProcessingSeconds = 240.0;

    std::string FormatParamValue(float value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        std::string s(buf);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    std::string FormatDuration(double seconds) {
        int total = static_cast<int>(seconds + 0.5);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
        return buf;
    }

    // Audio extensions offered in the sample dropdown.
    bool IsSampleAudioExtension(std::string ext) {
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const std::vector<std::string> exts =
            { "mp3", "wav", "flac", "ogg", "opus", "aac", "m4a", "aiff", "aif" };
        return std::find(exts.begin(), exts.end(), ext) != exts.end();
    }

    // All audio files in media/audios, sorted by name — the dropdown content.
    std::vector<std::string> CollectSampleTracks(const std::string& dir) {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (!ext.empty()) ext.erase(0, 1);
            if (IsSampleAudioExtension(ext)) names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    // ---- Overview tab: intro + diagram + playground category summary. ----
    std::shared_ptr<UltraCanvasUIElement> BuildOverviewTab() {
        const std::string base    = NormalizePath(GetResourcesDir() + "Docs/Modules/AudioFX/");
        const std::string svgName = "AudioFX.svg";

        std::string combined;
        std::string intro = LoadFile(base + "intro.md");
        if (intro.find_first_not_of(" \t\r\n") != std::string::npos) {
            combined += intro + "\n\n";
        }
        if (std::ifstream(base + svgName).good()) {
            combined += "![AudioFX architecture](" + svgName + ")\n\n";
        }

        combined += "## Playground effects\n\n"
                    "| Category | Effects |\n|---|---|\n";
        for (const auto& cat : EffectCategories()) {
            combined += "| **" + cat.name + "** | ";
            for (size_t i = 0; i < cat.effects.size(); ++i) {
                if (i) combined += ", ";
                combined += cat.effects[i].name;
            }
            combined += " |\n";
        }
        combined += "\nSwitch to the *Examples* tab to hear them on a sample or uploaded track.\n";

        auto text = std::make_shared<UltraCanvasTextArea>("AudioFXOverview");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(combined);
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Details tab: the full README documentation. ----
    std::shared_ptr<UltraCanvasUIElement> BuildDetailsTab() {
        const std::string base = NormalizePath(GetResourcesDir() + "Docs/Modules/AudioFX/");

        auto text = std::make_shared<UltraCanvasTextArea>("AudioFXDetails");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(LoadFile(base + "README.md"));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Examples tab: the interactive audio playground. ----
    std::shared_ptr<UltraCanvasUIElement> BuildExamplesTab() {
        const std::string audiosDir = NormalizePath(GetResourcesDir() + "media/audios/");
        const std::string iconsDir  = NormalizePath(GetResourcesDir() + "media/icons/");

        auto state = std::make_shared<PlaygroundState>();

        auto root = std::make_shared<UltraCanvasContainer>("AudioFXDemo", 0, 0, 1000, 700);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);
        root->SetBackgroundColor(Colors::White);

        auto title = std::make_shared<UltraCanvasLabel>("AudioFXDemoTitle", 0, 0, 0, 26);
        title->SetText("AudioFX playground");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        // ===== Three columns: waveform + player | effect tree | options =====
        auto columns = std::make_shared<UltraCanvasContainer>("AudioFXColumns", 0, 0, 0, 0);
        columns->layout.SetFlexRow().SetFlexGap(12)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        columns->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root->AddChild(columns);

        // ----- Left column: track selector + waveform + player -----
        auto leftCol = std::make_shared<UltraCanvasContainer>("AudioFXLeft", 0, 0, 0, 0);
        leftCol->layout.SetFlexColumn().SetFlexGap(8)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        leftCol->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        columns->AddChild(leftCol);

        auto sourceRow = std::make_shared<UltraCanvasContainer>("AudioFXSourceRow", 0, 0, 0, 34);
        sourceRow->layout.SetFlexRow().SetFlexGap(8)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        sourceRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        leftCol->AddChild(sourceRow);

        auto sourceLabel = std::make_shared<UltraCanvasLabel>("AudioFXSourceLabel", 0, 0, 92, 24);
        sourceLabel->SetText("Sample track");
        sourceLabel->SetFontSize(12);
        sourceLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sourceRow->AddChild(sourceLabel);

        auto sampleDropdown = std::make_shared<UltraCanvasDropdown>("AudioFXSample", 0, 0, 260, 30);
        sampleDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sourceRow->AddChild(sampleDropdown);

        auto uploadBtn = std::make_shared<UltraCanvasButton>("AudioFXUpload", 0, 0, 130, 30, "Upload audio…");
        uploadBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sourceRow->AddChild(uploadBtn);

        auto waveFrame = std::make_shared<UltraCanvasContainer>("AudioFXWaveFrame", 0, 0, 0, 0);
        waveFrame->layout.SetFlexColumn()
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        waveFrame->SetBorders(1.0f, Color(200, 200, 205, 255), 4.0f);
        waveFrame->SetBackgroundColor(Color(246, 246, 248, 255));
        waveFrame->SetPadding(6);
        waveFrame->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        leftCol->AddChild(waveFrame);

        auto waveform = CreateWaveformElement("AudioFXWave", 0, 0, 0, 0);
        waveform->SetOverlay(WaveformOverlay::RMSAndZeroAxis);
        waveform->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        waveFrame->AddChild(waveform);

        auto player = CreateAudioPlayer("AudioFXPlayer", 0, 0, 0, 56);
        player->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        leftCol->AddChild(player);

        auto status = std::make_shared<UltraCanvasLabel>("AudioFXStatus", 0, 0, 0, 32);
        status->SetFontSize(11);
        status->SetTextColor(Color(90, 90, 90, 255));
        status->SetWrap(TextWrap::WrapWord);
        status->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        leftCol->AddChild(status);

        // ----- Middle column: the effect tree -----
        auto treeCol = std::make_shared<UltraCanvasContainer>("AudioFXTreeCol", 0, 0, 250, 0);
        treeCol->layout.SetFlexColumn().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        treeCol->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        columns->AddChild(treeCol);

        auto treeTitle = std::make_shared<UltraCanvasLabel>("AudioFXTreeTitle", 0, 0, 0, 22);
        treeTitle->SetText("AudioFX effects");
        treeTitle->SetFontSize(13);
        treeTitle->SetFontWeight(FontWeight::Bold);
        treeTitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        treeCol->AddChild(treeTitle);

        auto tree = std::make_shared<UltraCanvasTreeView>("AudioFXTree", 0, 0, 250, 0);
        tree->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        TreeNodeData rootData("audiofx_root", "AudioFX effects");
        rootData.leftIcon = TreeNodeIcon(iconsDir + "audio.png", 16, 16);
        tree->SetRootNode(rootData);
        for (const auto& cat : EffectCategories()) {
            TreeNodeData catData("cat_" + cat.id, cat.name);
            catData.leftIcon = TreeNodeIcon(iconsDir + "folder.png", 16, 16);
            tree->AddNode("audiofx_root", catData);
            for (const auto& fx : cat.effects) {
                TreeNodeData fxData(fx.id, fx.name);
                fxData.leftIcon = TreeNodeIcon(iconsDir + "text.png", 16, 16);
                tree->AddNode("cat_" + cat.id, fxData);
            }
        }
        tree->ExpandAll();
        treeCol->AddChild(tree);

        // ----- Right column: options for the selected effect -----
        auto optionsCol = std::make_shared<UltraCanvasContainer>("AudioFXOptionsCol", 0, 0, 280, 0);
        optionsCol->layout.SetFlexColumn().SetFlexGap(6)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        optionsCol->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        columns->AddChild(optionsCol);

        auto optionsTitle = std::make_shared<UltraCanvasLabel>("AudioFXOptionsTitle", 0, 0, 0, 22);
        optionsTitle->SetText("Options");
        optionsTitle->SetFontSize(13);
        optionsTitle->SetFontWeight(FontWeight::Bold);
        optionsTitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        optionsCol->AddChild(optionsTitle);

        auto optionsBox = std::make_shared<UltraCanvasContainer>("AudioFXOptionsBox", 0, 0, 0, 0);
        optionsBox->layout.SetFlexColumn().SetFlexGap(8)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        optionsBox->SetBorders(1.0f, Color(200, 200, 205, 255), 4.0f);
        optionsBox->SetPadding(10);
        optionsBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        optionsCol->AddChild(optionsBox);

        // ===== Behaviour =====

        auto setStatus = [status](const std::string& text) {
            status->SetText(text);
            status->RequestRedraw();
        };

        // Hand a processed buffer to the player + waveform, preserving the
        // transport state so slider drags don't restart playback.
        auto showAudio = [state, player, waveform, setStatus](const FloatAudio& audio) -> bool {
            if (!audio.IsValid()) {
                setStatus("Processing produced an empty buffer.");
                return false;
            }
            auto raw = player->GetPlayer();
            double pos = raw ? raw->GetPosition() : 0.0;
            bool wasPlaying = raw && raw->IsPlaying();

            player->LoadFromAudio(ToUCAudio(audio));
            waveform->SetData(BuildWaveform(audio));
            waveform->SetPlayheadTime(0.0);

            if (pos > 0.0 && pos < audio.Duration()) player->Seek(pos);
            if (wasPlaying) player->Play();
            return true;
        };

        // Run the selected effect (or none) on the loaded source and present it.
        auto applyCurrent = [state, showAudio, setStatus]() {
            if (!state->original.IsValid()) return;
            if (!state->effect) {
                if (showAudio(state->original)) {
                    setStatus("Original track — pick an effect from the tree to process it.");
                }
                return;
            }
            FloatAudio result = state->effect->apply(state->original, state->params);
            if (!showAudio(result)) return;
            std::string text = state->effect->name;
            if (!state->effect->params.empty()) {
                text += " (";
                for (size_t i = 0; i < state->effect->params.size(); ++i) {
                    if (i) text += ", ";
                    text += state->effect->params[i].label + " = " +
                            FormatParamValue(state->params[i]);
                }
                text += ")";
            }
            setStatus(text + " — " + state->effect->description);
        };

        // Decode a source track, truncated to a length that keeps slider drags
        // fluid, and run the current effect on it.
        auto loadSource = [state, player, applyCurrent, setStatus](const std::string& path) {
            auto audio = UCAudio::LoadFromFile(path);
            if (!audio || !audio->IsValid()) {
                state->original = FloatAudio();
                setStatus("Could not load " + path +
                          (UltraCanvasAudioDevices::IsAvailable()
                               ? ""
                               : "\nAudio backend not compiled in — rebuild with "
                                 "-DULTRACANVAS_ENABLE_AUDIO=ON."));
                return;
            }
            FloatAudio buffer = ToFloatAudio(audio);
            std::string note;
            if (buffer.Duration() > kMaxProcessingSeconds) {
                buffer.samples.resize(static_cast<size_t>(kMaxProcessingSeconds * buffer.sampleRate)
                                      * buffer.channels);
                note = " (truncated to " + FormatDuration(kMaxProcessingSeconds) +
                       " to keep the sliders interactive)";
            }
            state->original = std::move(buffer);
            state->audioPath = path;

            std::string name = std::filesystem::path(path).filename().string();
            player->SetTrackTitle(name);
            player->Stop();
            applyCurrent();
            if (!state->effect) {
                setStatus(name + " — " + FormatDuration(state->original.Duration()) + ", " +
                          std::to_string(state->original.sampleRate) + " Hz, " +
                          std::to_string(state->original.channels) + " ch" + note +
                          ". Pick an effect from the tree.");
            }
        };

        // Rebuild the options panel for the selected effect: one slider +
        // value label per parameter and a "Play original" reset button.
        auto rebuildOptions = std::make_shared<std::function<void()>>();
        *rebuildOptions = [state, optionsBox, applyCurrent, showAudio, setStatus]() {
            optionsBox->ClearChildren();

            if (!state->effect) {
                auto hint = std::make_shared<UltraCanvasLabel>("AudioFXOptHint", 0, 0, 0, 60);
                hint->SetText("Select an effect in the tree to see its options.");
                hint->SetFontSize(12);
                hint->SetTextColor(Color(120, 120, 120, 255));
                hint->SetWrap(TextWrap::WrapWord);
                hint->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                optionsBox->AddChild(hint);
            } else {
                const Effect& fx = *state->effect;

                auto name = std::make_shared<UltraCanvasLabel>("AudioFXOptName", 0, 0, 0, 22);
                name->SetText(fx.name);
                name->SetFontSize(13);
                name->SetFontWeight(FontWeight::Bold);
                name->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                optionsBox->AddChild(name);

                auto desc = std::make_shared<UltraCanvasLabel>("AudioFXOptDesc", 0, 0, 0, 56);
                desc->SetText(fx.description);
                desc->SetFontSize(11);
                desc->SetTextColor(Color(110, 110, 110, 255));
                desc->SetWrap(TextWrap::WrapWord);
                desc->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                optionsBox->AddChild(desc);

                for (size_t i = 0; i < fx.params.size(); ++i) {
                    const EffectParam& param = fx.params[i];

                    auto valueLabel = std::make_shared<UltraCanvasLabel>(
                        "AudioFXOptLabel" + std::to_string(i), 0, 0, 0, 18);
                    valueLabel->SetText(param.label + ": " + FormatParamValue(state->params[i]));
                    valueLabel->SetFontSize(12);
                    valueLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    optionsBox->AddChild(valueLabel);

                    auto slider = CreateHorizontalSlider(
                        "AudioFXOptSlider" + std::to_string(i), 0, 0, 0, 24,
                        param.minValue, param.maxValue);
                    slider->SetStep(param.step);
                    slider->SetValue(state->params[i]);
                    slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                    slider->onValueChanged = [state, applyCurrent, valueLabel, param, i](float value) {
                        if (i >= state->params.size()) return;
                        state->params[i] = value;
                        valueLabel->SetText(param.label + ": " + FormatParamValue(value));
                        valueLabel->RequestRedraw();
                        applyCurrent();
                    };
                    optionsBox->AddChild(slider);
                }

                if (fx.params.empty()) {
                    auto none = std::make_shared<UltraCanvasLabel>("AudioFXOptNone", 0, 0, 0, 20);
                    none->SetText("This effect has no adjustable options.");
                    none->SetFontSize(11);
                    none->SetTextColor(Color(120, 120, 120, 255));
                    none->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    optionsBox->AddChild(none);
                }

                auto originalBtn = std::make_shared<UltraCanvasButton>(
                    "AudioFXPlayOriginal", 0, 0, 130, 28, "Play original");
                originalBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                originalBtn->onClick = [state, showAudio, setStatus]() {
                    if (!state->original.IsValid()) return;
                    if (showAudio(state->original)) {
                        setStatus("Original track (effect still selected — move a "
                                  "slider or re-select it to process again).");
                    }
                };
                optionsBox->AddChild(originalBtn);
            }

            optionsBox->InvalidateLayout();
            optionsBox->RequestRedraw();
        };

        tree->onNodeSelected = [state, rebuildOptions, applyCurrent](TreeNode* node) {
            if (!node) return;
            const Effect* fx = FindEffect(node->data.nodeId);
            if (!fx) return;   // root / category nodes just expand
            state->effect = fx;
            state->params.clear();
            for (const auto& param : fx->params) state->params.push_back(param.defValue);
            (*rebuildOptions)();
            applyCurrent();
        };

        // ----- Playhead <-> transport sync (same wiring as the Waveform demo) -----
        std::weak_ptr<UltraCanvasWaveformElement> waveformWeak = waveform;
        if (auto rawPlayer = player->GetPlayer()) {
            auto prevPos = rawPlayer->onPositionChanged;
            rawPlayer->onPositionChanged = [prevPos, waveformWeak](double seconds) {
                if (prevPos) prevPos(seconds);
                if (auto w = waveformWeak.lock()) w->SetPlayheadTime(seconds);
            };
        }
        std::weak_ptr<UltraCanvasAudioPlayerElement> playerWeak = player;
        waveform->onSeek = [playerWeak](double seconds) {
            if (auto p = playerWeak.lock()) p->Seek(seconds);
        };

        // ----- Sample track dropdown (value = full path) -----
        const std::vector<std::string> samples = CollectSampleTracks(audiosDir);
        int defaultIndex = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            sampleDropdown->AddItem(DropdownItem(samples[i], audiosDir + samples[i]));
            if (samples[i] == "the_mountain-cinematic-489998.mp3")
                defaultIndex = static_cast<int>(i);
        }
        sampleDropdown->onSelectionChanged =
            [loadSource](int, const DropdownItem& item) { loadSource(item.value); };

        // ----- Upload a custom track through the native file dialog -----
        uploadBtn->onClick = [state, sampleDropdown, loadSource, setStatus]() {
            FileDialogOptions opts;
            opts.SetTitle("Open audio for AudioFX");

            UltraCanvasFileLoader::OpenAudio(opts,
                [state, sampleDropdown, loadSource, setStatus](
                        const FileLoadResult& res, std::shared_ptr<UCAudio> audio) {
                    if (res.dialogResult != DialogResult::OK) {
                        setStatus("Upload cancelled.");
                        return;
                    }
                    if (!res.IsSuccess() || !audio || !audio->IsValid()) {
                        setStatus("Load failed: " + res.loadError);
                        return;
                    }
                    const std::string path = audio->GetSourcePath();
                    std::string name = std::filesystem::path(path).filename().string();
                    sampleDropdown->AddItem(DropdownItem("Custom: " + name, path));
                    sampleDropdown->SetSelectedIndex(sampleDropdown->GetItemCount() - 1, false);
                    loadSource(path);
                });
        };

        // Prime with the mountain-cinematic sample (or the first track found)
        // and an empty options panel.
        (*rebuildOptions)();
        if (!samples.empty()) {
            sampleDropdown->SetSelectedIndex(defaultIndex, false);
            loadSource(audiosDir + samples[defaultIndex]);
        } else {
            setStatus("No sample tracks found in " + audiosDir);
        }

        return root;
    }

} // anonymous namespace

// ===== AUDIOFX MODULE DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAudioFXExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("AudioFXExamples", 0, 0, 1000, 720);
        root->size.width  = CSSLayout::Dimension::Pct(100);
        root->size.height = CSSLayout::Dimension::Pct(100);
        root->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("AudioFXTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(30);
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        tabs->AddTab("Overview", BuildOverviewTab());
        tabs->AddTab("Details",  BuildDetailsTab());
        tabs->AddTab("Examples", BuildExamplesTab());
        tabs->SetActiveTab(0);

        root->AddChild(tabs);
        return root;
    }

} // namespace UltraCanvas
