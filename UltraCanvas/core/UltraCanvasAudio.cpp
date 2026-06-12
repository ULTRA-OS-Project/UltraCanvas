// core/UltraCanvasAudio.cpp
// UCAudio resource implementation. Decoding/encoding is currently a stub;
// real format support will land via Plugins/Audio/{WAV,MP3,OGG,...}.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudio.h"
#include <cstring>
#include <fstream>

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
    // TODO: dispatch to format decoder plugins (WAV native; MP3/OGG/FLAC via plugin)
    auto audio = std::make_shared<UCAudio>();
    audio->sourcePath = filePath;
    return audio;
}

std::shared_ptr<UCAudio> UCAudio::LoadFromMemory(const uint8_t* /*data*/, size_t /*size*/,
                                                 AudioFormat /*hint*/) {
    // TODO: dispatch to format decoder plugins
    return std::make_shared<UCAudio>();
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

bool UCAudio::SaveToFile(const std::string& /*filePath*/, AudioFormat /*format*/) const {
    // TODO: WAV writer (built-in); other encoders via plugin
    return false;
}

} // namespace UltraCanvas
