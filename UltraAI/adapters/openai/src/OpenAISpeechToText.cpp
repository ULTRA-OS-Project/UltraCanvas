// UltraAI/adapters/openai/src/OpenAISpeechToText.cpp
// OpenAI-compatible ISpeechToText: file transcription over the audio API
// (POST {baseUrl}/v1/audio/transcriptions, /v1/audio/translations), a
// multipart upload. See include/UltraAIOpenAI.h.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraAI Module

#include "UltraAIOpenAI.h"
#include "OpenAIInternal.h"
#include "UltraAIMultipart.h"
#include "UltraAIUltraNetTransport.h"

#include <algorithm>
#include <cctype>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using namespace openai_detail;

// The hosted API's default; a self-hosted OpenAI-compatible whisper server
// (speaches, faster-whisper-server, LocalAI) answers to "whisper-1" too.
constexpr const char* kDefaultHostedModel = "gpt-4o-transcribe";
constexpr const char* kDefaultSelfHostedModel = "whisper-1";
constexpr const char* kTranslationModel = "whisper-1";   // the only one /translations takes
constexpr int32_t kMaxFileBytes = 25 * 1024 * 1024;     // the hosted API's upload limit
constexpr const char* kVaultRef = "ai.openai.api_key";  // UltraAICommon.h's canonical name

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// A file name the API can tell the format from (it goes by the extension).
std::string UploadName(const TranscribeRequest& request) {
    if (!request.audio.filename.empty()) return request.audio.filename;
    switch (request.format.encoding) {
        case AudioEncoding::Mp3:  return "audio.mp3";
        case AudioEncoding::Wav:  return "audio.wav";
        case AudioEncoding::Flac: return "audio.flac";
        case AudioEncoding::Opus:
        case AudioEncoding::Ogg:  return "audio.ogg";
        case AudioEncoding::M4a:  return "audio.m4a";
        case AudioEncoding::Webm: return "audio.webm";
        default: break;
    }
    const std::string mime = Lower(request.audio.mimeType);
    if (mime.find("mpeg") != std::string::npos || mime.find("mp3") != std::string::npos) return "audio.mp3";
    if (mime.find("wav") != std::string::npos) return "audio.wav";
    if (mime.find("flac") != std::string::npos) return "audio.flac";
    if (mime.find("ogg") != std::string::npos || mime.find("opus") != std::string::npos) return "audio.ogg";
    if (mime.find("mp4") != std::string::npos || mime.find("m4a") != std::string::npos) return "audio.m4a";
    if (mime.find("webm") != std::string::npos) return "audio.webm";
    return "audio.mp3";
}

// BCP-47 "de-CH" -> ISO-639-1 "de", what the API takes.
std::string PrimaryLanguage(const std::string& tag) {
    return Lower(tag.substr(0, tag.find_first_of("-_")));
}

class OpenAISpeechToText : public ISpeechToText {
public:
    OpenAISpeechToText(SpeechToTextConfig config, std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    STTProviderCapabilities GetCapabilities() const override {
        STTProviderCapabilities caps;
        caps.providerId = "openai";
        caps.supportedEncodings = {AudioEncoding::Mp3, AudioEncoding::Wav, AudioEncoding::Flac,
                                   AudioEncoding::Ogg, AudioEncoding::Opus, AudioEncoding::M4a,
                                   AudioEncoding::Webm};
        caps.maxFileSizeBytes = kMaxFileBytes;
        auto add = [&caps](const std::string& id, const std::string& name, bool whisper) {
            STTModelInfo m;
            m.id = id;
            m.displayName = name;
            m.supportsTranslation = whisper;
            m.supportsWordTimestamps = whisper;
            m.runsLocally = false;
            caps.models.push_back(std::move(m));
        };
        // The configured default first: a caller reads with the first model.
        const std::string preferred = DefaultModel();
        add(preferred, preferred, preferred == "whisper-1");
        for (const char* id : {"gpt-4o-transcribe", "gpt-4o-mini-transcribe", "whisper-1"}) {
            if (preferred != id) add(id, id, std::string(id) == "whisper-1");
        }
        return caps;
    }

    TranscribeResponse Transcribe(const TranscribeRequest& request) override {
        TranscribeResponse out;
        if (request.audio.bytes.empty()) {
            out.error.code = ErrorCode::InvalidRequest;
            out.error.message = request.audio.url.empty()
                    ? "TranscribeRequest::audio is empty"
                    : "the audio API takes the file's bytes, not a URL";
            return out;
        }
        if (baseUrl_ == kDefaultBaseUrl && request.audio.bytes.size() > static_cast<size_t>(kMaxFileBytes)) {
            out.error.code = ErrorCode::InvalidRequest;
            out.error.message = "the recording is over the API's 25 MB upload limit";
            return out;
        }

        const bool translate = request.translateToEnglish;
        std::string model = translate ? kTranslationModel
                                      : !request.model.empty() ? request.model : DefaultModel();
        // Segments and word timings come only with whisper-1's verbose_json.
        const bool verbose = model == "whisper-1" && (request.segmentTimestamps || request.wordTimestamps);

        std::vector<MultipartPart> parts = {
            MultipartPart{"file", std::string(request.audio.bytes.begin(), request.audio.bytes.end()),
                          UploadName(request),
                          request.audio.mimeType.empty() ? "application/octet-stream" : request.audio.mimeType},
            MultipartPart{"model", model, "", ""},
            MultipartPart{"response_format", verbose ? "verbose_json" : "json", "", ""}};
        if (!translate && !request.language.empty())
            parts.push_back(MultipartPart{"language", PrimaryLanguage(request.language), "", ""});
        if (!request.prompt.empty()) parts.push_back(MultipartPart{"prompt", request.prompt, "", ""});
        if (verbose && !translate) {
            if (request.segmentTimestamps)
                parts.push_back(MultipartPart{"timestamp_granularities[]", "segment", "", ""});
            if (request.wordTimestamps)
                parts.push_back(MultipartPart{"timestamp_granularities[]", "word", "", ""});
        }
        // Options as plain form fields ("temperature", "chunking_strategy" ...).
        auto addOptions = [&parts](const OptionsMap& options) {
            for (const auto& [key, value] : options) {
                std::string text;
                if (const auto* b = std::get_if<bool>(&value)) text = *b ? "true" : "false";
                else if (const auto* i = std::get_if<int64_t>(&value)) text = std::to_string(*i);
                else if (const auto* d = std::get_if<double>(&value)) text = json(*d).dump();
                else if (const auto* s = std::get_if<std::string>(&value)) text = *s;
                parts.push_back(MultipartPart{key, text, "", ""});
            }
        };
        addOptions(config_.providerOptions);
        addOptions(request.options);

        std::string key;
        if (!ResolveKey(key, &out.error)) return out;
        const MultipartBody upload = BuildMultipartBody(parts);
        TransportRequest net;
        net.method = "POST";
        net.url = baseUrl_ + (translate ? "/v1/audio/translations" : "/v1/audio/transcriptions");
        net.timeoutMs = config_.timeoutMs;
        net.headers = {{"content-type", upload.contentType}};
        if (!key.empty()) net.headers.push_back({"authorization", "Bearer " + key});
        net.body = upload.body;

        Error transportError;
        const TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapOpenAIError(resp.statusCode, resp.body);
            return out;
        }
        return ParseResponse(resp.body);
    }

    std::future<TranscribeResponse> TranscribeAsync(const TranscribeRequest& request) override {
        return std::async(std::launch::async, [this, request] { return Transcribe(request); });
    }

    // The audio API transcribes files; live transcription is the Realtime
    // API's, which this adapter does not speak.
    LiveTranscriber StartLiveTranscribe(const TranscribeRequest&, TranscriptCallback onEvent) override {
        if (onEvent) {
            StreamingTranscriptEvent ev;
            ev.kind = StreamingTranscriptKind::Error;
            ev.error.code = ErrorCode::InvalidRequest;
            ev.error.message = "live transcription is not supported by the OpenAI audio adapter";
            onEvent(ev);
        }
        return nullptr;
    }

private:
    std::string DefaultModel() const {
        if (!config_.defaultModel.empty()) return config_.defaultModel;
        return baseUrl_ == kDefaultBaseUrl ? kDefaultHostedModel : kDefaultSelfHostedModel;
    }

    static TranscribeResponse ParseResponse(const std::string& body) {
        TranscribeResponse out;
        const json j = ParseJsonLenient(body);
        if (!j.is_object()) {
            // response_format "text" (set through the options) answers plain text.
            out.text = body;
            return out;
        }
        out.text = j.value("text", "");
        out.detectedLanguage = j.value("language", "");
        out.durationSec = j.value("duration", 0.0);
        if (j.contains("segments") && j["segments"].is_array()) {
            for (const auto& s : j["segments"]) {
                TranscriptSegment seg;
                seg.text = s.value("text", "");
                seg.startSec = s.value("start", 0.0);
                seg.endSec = s.value("end", 0.0);
                out.segments.push_back(std::move(seg));
            }
        }
        if (j.contains("words") && j["words"].is_array()) {
            if (out.segments.empty()) {
                TranscriptSegment all;
                all.text = out.text;
                out.segments.push_back(std::move(all));
            }
            for (const auto& w : j["words"]) {
                WordTiming word;
                word.word = w.value("word", "");
                word.startSec = w.value("start", 0.0);
                word.endSec = w.value("end", 0.0);
                out.segments.back().words.push_back(std::move(word));
            }
        }
        if (j.contains("usage") && j["usage"].is_object()) {
            const json& u = j["usage"];
            out.usage.inputTokens = u.value("input_tokens", out.usage.inputTokens);
            out.usage.outputTokens = u.value("output_tokens", out.usage.outputTokens);
            if (u.value("type", "") == "duration") out.usage.audioSeconds = u.value("seconds", 0.0);
        }
        return out;
    }

    bool ResolveKey(std::string& outKey, Error* outError) {
        std::lock_guard<std::mutex> lock(keyMutex_);
        if (!keyResolved_) {
            if (!ResolveOptionalKey(config_, baseUrl_, resolvedKey_, outError)) return false;
            keyResolved_ = true;
        }
        outKey = resolvedKey_;
        return true;
    }

    SpeechToTextConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
    bool keyResolved_ = false;
};

} // namespace

std::unique_ptr<ISpeechToText> CreateOpenAISpeechToText(const SpeechToTextConfig& config, Error* outError,
                                                        std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code = ErrorCode::NetworkError;
            outError->message = "OpenAI adapter needs a transport: build with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    SpeechToTextConfig resolved = config;
    // Built by the default route the config is empty: against the hosted
    // API the key is then the canonical UltraVault entry.
    if (resolved.apiKey.empty() && resolved.apiKeyVaultRef.empty() &&
        NormalizeBaseUrl(resolved.baseUrl) == openai_detail::kDefaultBaseUrl) {
        resolved.apiKeyVaultRef = kVaultRef;
    }
    return std::make_unique<OpenAISpeechToText>(std::move(resolved), std::move(transport));
}

} // namespace UltraAI
