// Tests/UltraAISpeechTranscriptionTest.cpp
// UltraAI as UltraCanvas's speech transcriber
// (UltraAI/ui/include/UltraAISpeechTranscription.h), with stand-in providers
// and a generated two-second WAV:
//
//   - with only "mock" registered no provider transcribes: ExtractText says
//     speech to text is not available, or the transcriber installed before
//     transcribes;
//   - a chosen provider that takes WAV gets the file as it is (bytes, MIME
//     type, name), and its words come back through ExtractText;
//     LastSpeechTranscriptionProvider names it;
//   - a provider that does not take WAV, or whose size limit the file is
//     over, gets the sound as a small mono MP3 (VideoFX, where FFmpeg is in
//     the build), and so does a video's sound track (one made with the
//     ffmpeg command line, where there is one);
//   - a provider's error comes back with its id.
//
// Headless.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraAIRouting.h"
#include "UltraAISpeechToText.h"
#include "UltraAISpeechTranscription.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Two seconds of a 440 Hz tone, 16 kHz mono 16-bit PCM.
std::vector<uint8_t> ToneWav() {
    const int rate = 16000, samples = rate * 2;
    std::vector<uint8_t> out;
    auto u32 = [&out](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto u16 = [&out](uint16_t v) { out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8)); };
    auto tag = [&out](const char* t) { out.insert(out.end(), t, t + 4); };
    tag("RIFF"); u32(36 + samples * 2); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    tag("data"); u32(samples * 2);
    for (int i = 0; i < samples; ++i)
        u16(static_cast<uint16_t>(static_cast<int16_t>(8000 * std::sin(2 * 3.14159265 * 440 * i / rate))));
    return out;
}

struct Seen {
    int calls = 0;
    std::vector<uint8_t> bytes;
    std::string mime, filename;
};

class FakeSpeech : public UltraAI::ISpeechToText {
public:
    FakeSpeech(std::vector<UltraAI::AudioEncoding> takes, int32_t maxBytes, UltraAI::TranscribeResponse answer,
               Seen* seen)
        : takes_(std::move(takes)), maxBytes_(maxBytes), answer_(std::move(answer)), seen_(seen) {}
    UltraAI::STTProviderCapabilities GetCapabilities() const override {
        UltraAI::STTProviderCapabilities caps;
        caps.providerId = "test-stt";
        caps.supportedEncodings = takes_;
        caps.maxFileSizeBytes = maxBytes_;
        return caps;
    }
    UltraAI::TranscribeResponse Transcribe(const UltraAI::TranscribeRequest& request) override {
        if (seen_) {
            ++seen_->calls;
            seen_->bytes = request.audio.bytes;
            seen_->mime = request.audio.mimeType;
            seen_->filename = request.audio.filename;
        }
        return answer_;
    }
    std::future<UltraAI::TranscribeResponse> TranscribeAsync(const UltraAI::TranscribeRequest& r) override {
        std::promise<UltraAI::TranscribeResponse> p;
        p.set_value(Transcribe(r));
        return p.get_future();
    }
    UltraAI::LiveTranscriber StartLiveTranscribe(const UltraAI::TranscribeRequest&, UltraAI::TranscriptCallback) override {
        return nullptr;
    }

private:
    std::vector<UltraAI::AudioEncoding> takes_;
    int32_t maxBytes_;
    UltraAI::TranscribeResponse answer_;
    Seen* seen_;
};

UltraAI::TranscribeResponse Words(const std::string& text) {
    UltraAI::TranscribeResponse r;
    r.text = text;
    return r;
}

// The provider "test-stt", as the next stand-in says.
std::vector<UltraAI::AudioEncoding> g_takes;
int32_t g_maxBytes = 0;
UltraAI::TranscribeResponse g_answer;
Seen g_seen;

} // namespace

int main() {
    std::cout << "UltraAISpeechTranscriptionTest" << std::endl;
    std::error_code ec;
    const fs::path folder = fs::temp_directory_path(ec) / PathFromUtf8("SpeechTranscription-\xE0\xB9\x80\xE0\xB8\xAA\xE0\xB8\xB5\xE0\xB8\xA2\xE0\xB8\x87");
    fs::remove_all(folder, ec);
    fs::create_directories(folder, ec);
    const std::string wav = PathToUtf8(folder / PathFromUtf8("memo.wav"));
    const std::vector<uint8_t> tone = ToneWav();
    if (std::FILE* f = OpenFileUtf8(wav, "wb")) {
        std::fwrite(tone.data(), 1, tone.size(), f);
        std::fclose(f);
    }

    UltraAI::ClearDefaultProviders();
    UltraAI::SetCloudFallbackAllowed(false);
    UltraCanvasFileLoader::SetSpeechTranscriber(nullptr);
    UltraAI::RegisterUltraAISpeechTranscription();

    // ----- no provider -----
    std::string provider;
    Check(!UltraAI::FindSpeechProvider(provider), "only mock registered: no speech provider (mock never counts)");
    auto result = UltraCanvasFileLoader::ExtractText(wav);
    Check(!result.Ok() && result.method == UltraCanvasFileLoader::TextExtractionMethod::Transcription &&
          Contains(result.error, "Speech to text is not available"),
          "... so ExtractText says speech to text is not available");

    int fallbackCalls = 0;
    UltraCanvasFileLoader::SetSpeechTranscriber([&fallbackCalls](const std::string&, std::string& text, std::string&) {
        ++fallbackCalls;
        text = "from the application's own engine";
        return true;
    });
    UltraAI::RegisterUltraAISpeechTranscription();
    result = UltraCanvasFileLoader::ExtractText(wav);
    Check(result.Ok() && result.text == "from the application's own engine" && fallbackCalls == 1,
          "... or the transcriber installed before transcribes");

    // ----- a chosen provider that takes WAV -----
    UltraAI::RegisterSpeechToTextProvider("test-stt", [](const UltraAI::SpeechToTextConfig&, UltraAI::Error*) {
        return std::make_unique<FakeSpeech>(g_takes, g_maxBytes, g_answer, &g_seen);
    });
    Check(!UltraAI::FindSpeechProvider(provider), "a cloud provider nobody chose is not used");
    UltraAI::SetDefaultProvider("speechtotext", "test-stt");
    Check(UltraAI::FindSpeechProvider(provider) && provider == "test-stt", "the provider the user chose is found");

    g_takes = {UltraAI::AudioEncoding::Wav, UltraAI::AudioEncoding::Mp3};
    g_answer = Words("Hello from the recording");
    result = UltraCanvasFileLoader::ExtractText(wav);
    Check(result.Ok() && result.text == "Hello from the recording" && fallbackCalls == 1,
          "a provider that takes WAV transcribes, instead of the engine before");
    Check(g_seen.bytes == tone && g_seen.mime == "audio/wav" && g_seen.filename == "memo.wav",
          "... handed the file as it is, with its MIME type and name");
    Check(UltraAI::LastSpeechTranscriptionProvider() == "test-stt", "... and LastSpeechTranscriptionProvider names it");

    // ----- the sound converted -----
#ifdef UC_TEST_HAS_VIDEOFX
    g_takes = {UltraAI::AudioEncoding::Mp3};
    g_seen = {};
    result = UltraCanvasFileLoader::ExtractText(wav);
    if (!result.Ok() && Contains(result.error, "not available")) {
        std::cout << "  [SKIP] VideoFX has no FFmpeg engine here: " << result.error << "\n";
    } else {
        const bool mp3 = g_seen.bytes.size() > 3 &&
                         ((g_seen.bytes[0] == 'I' && g_seen.bytes[1] == 'D' && g_seen.bytes[2] == '3') ||
                          (g_seen.bytes[0] == 0xFF && (g_seen.bytes[1] & 0xE0) == 0xE0));
        Check(result.Ok() && mp3 && g_seen.mime == "audio/mpeg" && g_seen.bytes.size() < tone.size(),
              "a provider that does not take WAV gets the sound as a smaller MP3");
        g_takes = {UltraAI::AudioEncoding::Wav, UltraAI::AudioEncoding::Mp3};
        g_maxBytes = 40000;   // the WAV is 64 KB
        g_seen = {};
        result = UltraCanvasFileLoader::ExtractText(wav);
        Check(result.Ok() && g_seen.mime == "audio/mpeg" && g_seen.bytes.size() <= 40000u,
              "... and so does one whose size limit the file is over");
        g_maxBytes = 0;

        // A video: its sound track taken out. Made with the ffmpeg command
        // line where one is installed (the test needs no media of its own).
        const std::string video = PathToUtf8(folder / PathFromUtf8("clip.mp4"));
        const std::string make = "ffmpeg -loglevel error -y -f lavfi -i color=c=blue:s=160x120:d=2 "
                                 "-f lavfi -i sine=frequency=440:duration=2 -shortest \"" + video + "\"";
        if (std::system("ffmpeg -version > /dev/null 2>&1") == 0 && std::system(make.c_str()) == 0) {
            g_takes = {UltraAI::AudioEncoding::Mp3, UltraAI::AudioEncoding::Wav};
            g_answer = Words("spoken in the video");
            g_seen = {};
            result = UltraCanvasFileLoader::ExtractText(video);
            Check(result.Ok() && result.method == UltraCanvasFileLoader::TextExtractionMethod::Transcription &&
                  result.text == "spoken in the video" && g_seen.mime == "audio/mpeg" && !g_seen.bytes.empty(),
                  "a video: its sound track goes to the provider as MP3");
        } else {
            std::cout << "  [SKIP] no ffmpeg command line to make a video with\n";
        }
    }
#else
    std::cout << "  [SKIP] no VideoFX in this build: the conversion is not tested\n";
#endif

    // ----- a provider's error -----
    g_takes = {UltraAI::AudioEncoding::Wav};
    g_answer = {};
    g_answer.error.code = UltraAI::ErrorCode::QuotaExceeded;
    g_answer.error.message = "quota exceeded";
    result = UltraCanvasFileLoader::ExtractText(wav);
    Check(!result.Ok() && Contains(result.error, "test-stt: quota exceeded") &&
          UltraAI::LastSpeechTranscriptionProvider().empty(),
          "a provider's error comes back with its id");

    UltraCanvasFileLoader::SetSpeechTranscriber(nullptr);
    UltraAI::ClearDefaultProviders();
    fs::remove_all(folder, ec);
    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << " (" << g_failures << " failure(s))" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
