// Tests/AudioCodecTests.cpp
// Headless roundtrip test for the audio encode/decode matrix: builds a sine
// test signal, saves it in every save-capable format the runtime inventory
// reports (WAV always; FLAC/OGG/Opus/MP3 when their codec libraries were
// compiled in), re-decodes each file and sanity-checks channels, duration
// and signal energy. Exits non-zero on any failure.
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "UltraCanvasAudio.h"
#include "UltraCanvasSupportedFormats.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr double kSeconds = 2.0;
constexpr float kAmplitude = 0.5f;

// Interleaved sample as float for the layouts the decoders produce.
float SampleAsFloat(const uint8_t* data, AudioSampleType t, size_t idx) {
    switch (t) {
        case AudioSampleType::PCM_S16:
            return reinterpret_cast<const int16_t*>(data)[idx] / 32768.0f;
        case AudioSampleType::PCM_S24: {
            const uint8_t* p = data + idx * 3;
            int32_t v = (static_cast<int32_t>(p[0])) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;
            return static_cast<float>(v / 8388608.0);
        }
        case AudioSampleType::PCM_S32:
            return static_cast<float>(
                reinterpret_cast<const int32_t*>(data)[idx] / 2147483648.0);
        case AudioSampleType::PCM_F32:
        default:
            return reinterpret_cast<const float*>(data)[idx];
    }
}

double RmsOf(const UCAudio& audio) {
    const AudioBufferInfo& info = audio.GetInfo();
    const size_t total = info.frameCount * info.channels;
    if (total == 0) return 0.0;
    double sumSq = 0.0;
    for (size_t i = 0; i < total; ++i) {
        double s = SampleAsFloat(audio.GetData(), info.sampleType, i);
        sumSq += s * s;
    }
    return std::sqrt(sumSq / total);
}

std::shared_ptr<UCAudio> MakeTestSignal() {
    AudioBufferInfo info;
    info.sampleRate = kRate;
    info.channels = kChannels;
    info.sampleType = AudioSampleType::PCM_S16;
    info.frameCount = static_cast<size_t>(kRate * kSeconds);
    info.durationSeconds = kSeconds;

    std::vector<int16_t> pcm(info.frameCount * kChannels);
    for (size_t i = 0; i < info.frameCount; ++i) {
        double t = static_cast<double>(i) / kRate;
        pcm[i * 2] = static_cast<int16_t>(
            kAmplitude * 32767.0 * std::sin(2.0 * M_PI * 440.0 * t));
        pcm[i * 2 + 1] = static_cast<int16_t>(
            kAmplitude * 32767.0 * std::sin(2.0 * M_PI * 660.0 * t));
    }
    return UCAudio::FromRawPCM(pcm.data(), pcm.size() * sizeof(int16_t), info);
}

} // namespace

int main() {
    auto src = MakeTestSignal();
    if (!src || !src->IsValid()) {
        std::printf("FAIL: could not build the test signal\n");
        return 1;
    }
    const double srcRms = RmsOf(*src);

    int failures = 0;
    int tested = 0;

    auto formats = UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory::Audio);
    for (const auto& f : formats) {
        if (!f.canSave) continue;
        ++tested;
        const AudioFormat fmt = AudioFormatFromExtension(f.extension);
        const std::string path = "audio_roundtrip_test." + f.extension;

        if (!src->SaveToFile(path, fmt)) {
            std::printf("FAIL [%s] SaveToFile returned false (provider: %s)\n",
                        f.extension.c_str(), f.provider.c_str());
            ++failures;
            continue;
        }

        if (!f.canLoad) {
            std::printf("PASS [%s] encoded (no decoder in this build to verify)\n",
                        f.extension.c_str());
            std::remove(path.c_str());
            continue;
        }

        auto back = UCAudio::LoadFromFile(path);
        std::remove(path.c_str());
        if (!back || !back->IsValid()) {
            std::printf("FAIL [%s] re-decode produced no audio\n", f.extension.c_str());
            ++failures;
            continue;
        }

        const AudioBufferInfo& info = back->GetInfo();
        bool ok = true;
        if (info.channels != kChannels) {
            std::printf("FAIL [%s] channels: got %d, want %d\n",
                        f.extension.c_str(), info.channels, kChannels);
            ok = false;
        }
        // Lossy codecs pad with encoder delay; Opus resamples to 48 kHz.
        if (std::fabs(info.durationSeconds - kSeconds) > 0.2) {
            std::printf("FAIL [%s] duration: got %.3f s, want %.1f s\n",
                        f.extension.c_str(), info.durationSeconds, kSeconds);
            ok = false;
        }
        const double rms = RmsOf(*back);
        if (rms < srcRms * 0.7 || rms > srcRms * 1.3) {
            std::printf("FAIL [%s] RMS: got %.4f, want ~%.4f\n",
                        f.extension.c_str(), rms, srcRms);
            ok = false;
        }
        if (ok) {
            std::printf("PASS [%s] %d ch, %d Hz, %.3f s, RMS %.4f (src %.4f)\n",
                        f.extension.c_str(), info.channels, info.sampleRate,
                        info.durationSeconds, rms, srcRms);
        } else {
            ++failures;
        }
    }

    if (tested == 0) {
        std::printf("SKIP: no save-capable audio formats in this build "
                    "(audio backend off?)\n");
        return 0;
    }
    std::printf("%d format(s) tested, %d failure(s)\n", tested, failures);
    return failures == 0 ? 0 : 1;
}
