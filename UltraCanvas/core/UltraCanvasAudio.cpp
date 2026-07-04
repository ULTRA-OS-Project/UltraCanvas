// core/UltraCanvasAudio.cpp
// UCAudio resource implementation. Decode/encode is delegated to the active
// IAudioBackend (miniaudio when ULTRACANVAS_ENABLE_AUDIO=ON; null otherwise).
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudio.h"
#include "../libspecific/Audio/IAudioBackend.h"
#include <cstring>

namespace UltraCanvas {

size_t AudioBufferInfo::BytesPerSample() const {
    switch (sampleType) {
        case AudioSampleType::PCM_S16: return 2;
        case AudioSampleType::PCM_S24: return 3;
        case AudioSampleType::PCM_S32: return 4;
        case AudioSampleType::PCM_F32: return 4;
    }
    return 2;
}

std::shared_ptr<UCAudio> UCAudio::LoadFromFile(const std::string& filePath,
                                               AudioFormat /*hint*/) {
    auto* backend = GetAudioBackend();
    if (!backend) return std::make_shared<UCAudio>();
    auto audio = backend->DecodeFile(filePath);
    if (!audio) {
        audio = std::make_shared<UCAudio>();
    }
    audio->SetSourcePath(filePath);
    return audio;
}

std::shared_ptr<UCAudio> UCAudio::LoadFromMemory(const uint8_t* data, size_t size,
                                                 AudioFormat /*hint*/) {
    auto* backend = GetAudioBackend();
    if (!backend) return std::make_shared<UCAudio>();
    auto audio = backend->DecodeMemory(data, size);
    return audio ? audio : std::make_shared<UCAudio>();
}

std::shared_ptr<UCAudio> UCAudio::FromRawPCM(const void* samples, size_t bytes,
                                             const AudioBufferInfo& info) {
    auto audio = std::make_shared<UCAudio>();
    audio->info = info;
    audio->sourceFormat = AudioFormat::PCM;
    audio->data.resize(bytes);
    if (samples && bytes) {
        std::memcpy(audio->data.data(), samples, bytes);
    }
    audio->valid = true;
    return audio;
}

bool UCAudio::SaveToFile(const std::string& filePath, AudioFormat format) const {
    auto* backend = GetAudioBackend();
    if (!backend) return false;
    return backend->EncodeFile(filePath, *this, format);
}

} // namespace UltraCanvas
