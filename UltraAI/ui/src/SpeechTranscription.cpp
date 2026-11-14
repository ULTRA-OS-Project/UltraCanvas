// UltraAI/ui/src/SpeechTranscription.cpp
// UltraAI as UltraCanvas's speech transcriber. See
// include/UltraAISpeechTranscription.h.
// Version: 1.1.0 - the local runtime's whisper engine; one with no model yet steps aside
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraAI Module

#include "UltraAISpeechTranscription.h"

#include "UltraAIRouting.h"
#include "UltraAISpeechToText.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"
#ifdef ULTRAAI_UI_HAS_VIDEOFX
#include "VideoFX/VideoFX.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

namespace fs = std::filesystem;

thread_local std::string g_lastProvider;

// The providers on the route that construct, in order: the test double
// hears nothing, and the local runtime counts when it offers a model for
// this computer.
std::vector<std::pair<std::string, std::unique_ptr<ISpeechToText>>> Providers(bool firstOnly) {
    std::vector<std::pair<std::string, std::unique_ptr<ISpeechToText>>> out;
    for (const std::string& id : ResolveProviderOrder("speechtotext", ListSpeechToTextProviders())) {
        if (id == "mock") continue;
        SpeechToTextConfig config;
        config.providerId = id;
        Error error;
        std::unique_ptr<ISpeechToText> candidate = CreateSpeechToText(config, &error);
        if (!candidate) continue;
        if (id == "local" && candidate->GetCapabilities().models.empty()) continue;
        out.emplace_back(id, std::move(candidate));
        if (firstOnly) break;
    }
    return out;
}

// The local runtime's answer when its model is not downloaded and the
// download policy does not let a call fetch it: not set up, so the next
// provider is asked.
bool NotSetUp(const Error& error) {
    return error.code == ErrorCode::ModelNotFound && error.providerCode == "needs-install";
}

std::string LowerExtension(const std::string& path) {
    std::string ext = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(path).extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// A sound file's encoding and MIME type by its extension; Auto for anything
// else (a video, a type the providers do not name).
AudioEncoding EncodingOf(const std::string& ext, std::string& mime) {
    if (ext == "mp3")  { mime = "audio/mpeg"; return AudioEncoding::Mp3; }
    if (ext == "wav")  { mime = "audio/wav";  return AudioEncoding::Wav; }
    if (ext == "flac") { mime = "audio/flac"; return AudioEncoding::Flac; }
    if (ext == "ogg" || ext == "oga") { mime = "audio/ogg"; return AudioEncoding::Ogg; }
    if (ext == "opus") { mime = "audio/ogg";  return AudioEncoding::Opus; }
    if (ext == "m4a")  { mime = "audio/mp4";  return AudioEncoding::M4a; }
    if (ext == "weba") { mime = "audio/webm"; return AudioEncoding::Webm; }
    mime.clear();
    return AudioEncoding::Auto;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    std::FILE* file = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!file) return false;
    uint8_t buffer[65536];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) out.insert(out.end(), buffer, buffer + read);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    return !failed;
}

// The sound to send: the file as it is where the provider takes it, else
// its sound track as a small MP3 (see the header). False, with `error`, when
// neither works.
bool SoundToSend(const std::string& path, const STTProviderCapabilities& caps, MediaBlob& blob,
                 std::string& error) {
    std::string mime;
    const AudioEncoding encoding = EncodingOf(LowerExtension(path), mime);
    const bool taken = encoding != AudioEncoding::Auto &&
                       (caps.supportedEncodings.empty() ||
                        std::find(caps.supportedEncodings.begin(), caps.supportedEncodings.end(), encoding) !=
                                caps.supportedEncodings.end());
    std::error_code ec;
    const uintmax_t size = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
    const bool fits = !ec && (caps.maxFileSizeBytes <= 0 || size <= static_cast<uintmax_t>(caps.maxFileSizeBytes));
    if (taken && fits) {
        if (!ReadWholeFile(path, blob.bytes)) {
            error = UltraCanvas::Tr("The file could not be read.");
            return false;
        }
        blob.mimeType = mime;
        blob.filename = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(path).filename());
        return true;
    }
#ifdef ULTRAAI_UI_HAS_VIDEOFX
    // The sound track as a mono 16 kHz MP3, in a temporary file of our own.
    std::random_device random;
    char name[48];
    std::snprintf(name, sizeof(name), "ultraai-speech-%08x%08x.mp3", random(), random());
    const fs::path temp = fs::temp_directory_path(ec) / UltraCanvas::PathFromUtf8(name);
    VideoFX::VideoFXExportSettings settings = VideoFX::VideoFXExportSettings::AudioOnlyMP3(64000);
    settings.sampleRate = 16000;
    settings.channels = 1;
    const VideoFX::VideoFXResult result = VideoFX::VideoFX_ExtractAudio(path, UltraCanvas::PathToUtf8(temp), settings);
    const bool read = result == VideoFX::VideoFXResult::Ok && ReadWholeFile(UltraCanvas::PathToUtf8(temp), blob.bytes);
    fs::remove(temp, ec);
    if (!read) {
        error = UltraCanvas::Tr("The file could not be read.") + "\n" +
                (result == VideoFX::VideoFXResult::Ok ? std::string("the sound track could not be read back")
                                             : std::string(VideoFX::VideoFX_ResultToString(result)));
        return false;
    }
    if (caps.maxFileSizeBytes > 0 && blob.bytes.size() > static_cast<size_t>(caps.maxFileSizeBytes)) {
        error = UltraCanvas::Tr("Speech to text failed.") + "\n" + caps.providerId +
                ": the recording is too long for one upload";
        return false;
    }
    blob.mimeType = "audio/mpeg";
    blob.filename = "speech.mp3";
    return true;
#else
    error = UltraCanvas::Tr("Speech to text failed.") + "\n" +
            (taken ? std::string("the recording is over the provider's upload limit")
                   : std::string("this build cannot take the sound out of this type of file (no VideoFX)"));
    return false;
#endif
}

} // namespace

bool FindSpeechProvider(std::string& providerId) {
    providerId.clear();
    auto providers = Providers(true);
    if (providers.empty()) return false;
    providerId = providers.front().first;
    return true;
}

std::string LastSpeechTranscriptionProvider() { return g_lastProvider; }

void RegisterUltraAISpeechTranscription() {
    using UltraCanvas::UltraCanvasFileLoader;
    UltraCanvasFileLoader::SpeechTranscriber fallback = UltraCanvasFileLoader::GetSpeechTranscriber();
    UltraCanvasFileLoader::SetSpeechTranscriber(
            [fallback](const std::string& path, std::string& text, std::string& error) {
        g_lastProvider.clear();
        std::string notSetUp;   // the first provider's that is there but has no model yet
        for (auto& [providerId, provider] : Providers(false)) {
            TranscribeRequest request;
            if (!SoundToSend(path, provider->GetCapabilities(), request.audio, error)) return false;
            request.segmentTimestamps = false;   // the words are what is wanted
            const TranscribeResponse response = provider->Transcribe(request);
            if (NotSetUp(response.error)) {
                if (notSetUp.empty()) notSetUp = providerId + ": " + response.error.message;
                continue;
            }
            if (!response.error.IsOk()) {
                error = UltraCanvas::Tr("Speech to text failed.") + "\n" + providerId +
                        (response.error.message.empty() ? std::string() : ": " + response.error.message);
                return false;
            }
            text = response.text;
            g_lastProvider = providerId;
            return true;
        }
        if (fallback) return fallback(path, text, error);
        // The standard set's sentence (UltraCanvasFileLoaderText.cpp has it).
        error = UltraCanvas::Tr("Speech to text is not available: no speech recognition (UltraAI STT or "
                                "another engine) is set up.");
        if (!notSetUp.empty()) error += "\n" + notSetUp;
        return false;
    });
}

} // namespace UltraAI
