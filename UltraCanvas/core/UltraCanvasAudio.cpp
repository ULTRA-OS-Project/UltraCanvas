// core/UltraCanvasAudio.cpp
// UCAudio resource implementation. Decode/encode is delegated to the active
// IAudioBackend (miniaudio when ULTRACANVAS_ENABLE_AUDIO=ON; null otherwise).
// Version: 0.2.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasAudio.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "../libspecific/Audio/IAudioBackend.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace UltraCanvas {

namespace {
// The extension of a path, lowercase and without its dot ("" when there is
// none). AudioFormatFromExtension takes an extension, not a path, so the
// save-side codec lookup needs this first.
std::string ExtensionOfPath(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
} // namespace

AudioFormat AudioFormatFromExtension(const std::string& extension) {
    std::string e;
    e.reserve(extension.size());
    for (char c : extension) {
        if (c == '.' && e.empty()) continue;   // strip a leading dot
        e.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (e == "wav" || e == "wave")  return AudioFormat::WAV;
    if (e == "mp3")                 return AudioFormat::MP3;
    if (e == "ogg" || e == "oga")   return AudioFormat::OGG;
    if (e == "flac")                return AudioFormat::FLAC;
    if (e == "aac" || e == "m4a" || e == "m4b") return AudioFormat::AAC;
    if (e == "opus")                return AudioFormat::Opus;
    if (e == "pcm" || e == "raw")   return AudioFormat::PCM;
    return AudioFormat::Unknown;
}

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
    std::shared_ptr<UCAudio> audio;
    if (auto* backend = GetAudioBackend()) {
        audio = backend->DecodeFile(filePath);
    }
    // Nothing built in could read it: give a codec registered by the
    // application or a plugin its turn. Running last means a plugin is
    // reachable without having to displace anything, and a plugin's decoder
    // must therefore never call back into LoadFromFile.
    if (!audio || !audio->IsValid()) {
        if (auto codec = FindMediaCodecForFile(MediaCodecKind::Audio, filePath)) {
            if (codec->decodeAudio) {
                if (auto decoded = codec->decodeAudio(filePath); decoded && decoded->IsValid()) {
                    audio = decoded;
                }
            }
        }
    }
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
    if (auto* backend = GetAudioBackend()) {
        if (backend->EncodeFile(filePath, *this, format)) return true;
    }
    // As with loading, a registered codec gets the file the built-ins turned
    // down. The lookup is by extension, not by content: AudioFormat cannot name
    // a format a plugin brought with it, and the file does not exist yet.
    if (auto codec = FindMediaCodecByExtension(MediaCodecKind::Audio,
                                               ExtensionOfPath(filePath))) {
        if (codec->encodeAudio) return codec->encodeAudio(filePath, *this);
    }
    return false;
}

} // namespace UltraCanvas
