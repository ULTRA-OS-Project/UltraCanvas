// UltraAI/adapters/elevenlabs/src/ElevenLabsTextToSpeech.cpp
// ElevenLabs ITextToSpeech adapter. One-shot synthesis returns raw audio
// bytes; streaming uses /stream/with-timestamps, whose newline-delimited
// JSON carries base64 audio per line and lets an error body be told apart
// from audio even though the HTTP status is only known at the end.
// Version: 0.1.0
// Last Modified: 2026-09-24
// Author: UltraAI Module

#include "UltraAIElevenLabs.h"

#include "ElevenLabsInternal.h"
#include "UltraAIBase64.h"
#include "UltraAIMultipart.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using nlohmann::json;
using namespace elevenlabs_detail;

constexpr int kDefaultPcmRate = 24000;
// How much of a streamed body is kept for mapping an HTTP error response.
constexpr size_t kMaxErrorBody = 16 * 1024;

// Resolve the ElevenLabs output_format for a request. Empty result with
// *outError set when the requested format / rate has no equivalent.
std::string ResolveOutputFormat(const SpeakRequest& request,
                                const std::string& overrideFormat,
                                Error* outError) {
    if (!overrideFormat.empty()) return overrideFormat;

    auto reject = [outError](const std::string& message) {
        if (outError) {
            outError->code    = ErrorCode::UnsupportedFormat;
            outError->message = message;
        }
        return std::string();
    };

    switch (request.format) {
        case TtsAudioFormat::Mp3:
            if (request.sampleRateHz == 0 || request.sampleRateHz == 44100) {
                return "mp3_44100_128";
            }
            if (request.sampleRateHz == 22050) return "mp3_22050_32";
            return reject("ElevenLabs MP3 output is 44100 or 22050 Hz");
        case TtsAudioFormat::PcmS16Le: {
            const int rate = request.sampleRateHz == 0 ? kDefaultPcmRate
                                                       : request.sampleRateHz;
            for (int allowed : {8000, 16000, 22050, 24000, 44100, 48000}) {
                if (rate == allowed) return "pcm_" + std::to_string(rate);
            }
            return reject("ElevenLabs PCM output is 8000, 16000, 22050, "
                          "24000, 44100 or 48000 Hz");
        }
        default:
            return reject("ElevenLabs output is mapped for mp3 and 16-bit "
                          "PCM; pass options[\"output_format\"] for others");
    }
}

std::string MimeForOutputFormat(const std::string& format) {
    auto startsWith = [&format](const char* prefix) {
        return format.rfind(prefix, 0) == 0;
    };
    if (startsWith("mp3"))  return "audio/mpeg";
    if (startsWith("pcm"))  return "audio/L16";
    if (startsWith("ulaw")) return "audio/basic";
    if (startsWith("alaw")) return "audio/x-alaw-basic";
    if (startsWith("opus")) return "audio/opus";
    if (startsWith("wav"))  return "audio/wav";
    return "application/octet-stream";
}

// Sample rate of a "pcm_<rate>" format; 0 for anything else.
int PcmRate(const std::string& format) {
    if (format.rfind("pcm_", 0) != 0) return 0;
    return std::atoi(format.c_str() + 4);
}

// "de-DE" -> "de"; ElevenLabs takes ISO 639-1 codes.
std::string PrimarySubtag(const std::string& tag) {
    std::string out = tag.substr(0, tag.find_first_of("-_"));
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

VoiceGender GenderFromLabel(const std::string& label) {
    if (label == "female")  return VoiceGender::Female;
    if (label == "male")    return VoiceGender::Male;
    if (label == "neutral" || label == "non-binary") return VoiceGender::Neutral;
    return VoiceGender::Unspecified;
}

std::string ExtensionForMime(const std::string& mime) {
    if (mime == "audio/mpeg" || mime == "audio/mp3") return ".mp3";
    if (mime == "audio/wav" || mime == "audio/x-wav") return ".wav";
    if (mime == "audio/flac")  return ".flac";
    if (mime == "audio/ogg")   return ".ogg";
    if (mime == "audio/mp4" || mime == "audio/m4a") return ".m4a";
    return ".bin";
}

class ElevenLabsTextToSpeech : public ITextToSpeech {
public:
    ElevenLabsTextToSpeech(TextToSpeechConfig config,
                           std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    TTSProviderCapabilities GetCapabilities() const override {
        TTSProviderCapabilities caps;
        caps.providerId = "elevenlabs";

        auto add = [&caps](const char* id, const char* name) {
            TTSModelInfo model;
            model.id                   = id;
            model.displayName          = name;
            model.supportedFormats     = {TtsAudioFormat::Mp3,
                                          TtsAudioFormat::PcmS16Le};
            model.supportsSsml         = false;
            model.supportsStreaming    = true;
            model.supportsVoiceCloning = true;
            model.runsLocally          = false;
            caps.models.push_back(std::move(model));
        };
        // The API accepts any model id the account can use; these are the
        // current general-purpose ones. Newer ids work without a code change.
        add("eleven_multilingual_v2", "Eleven Multilingual v2");
        add("eleven_v3",              "Eleven v3");
        add("eleven_flash_v2_5",      "Eleven Flash v2.5");
        add("eleven_turbo_v2_5",      "Eleven Turbo v2.5");
        return caps;
    }

    std::vector<VoiceInfo> ListVoices(const std::string& language) override {
        std::vector<VoiceInfo> voices;

        TransportRequest net;
        if (!BuildBaseRequest("GET", "/v1/voices", net, nullptr)) return voices;

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk() || resp.statusCode >= 400) return voices;

        json body = ParseJsonLenient(resp.body);
        if (!body.is_object() || !body.contains("voices") ||
            !body["voices"].is_array()) {
            return voices;
        }

        const std::string wanted = PrimarySubtag(language);
        for (const auto& entry : body["voices"]) {
            if (!entry.is_object()) continue;
            VoiceInfo voice;
            voice.id = entry.value("voice_id", "");
            if (voice.id.empty()) continue;
            voice.displayName = entry.value("name", voice.id);

            const std::string category = entry.value("category", "");
            voice.isCloned = category == "cloned" || category == "professional";

            std::vector<std::string> languages;
            if (entry.contains("verified_languages") &&
                entry["verified_languages"].is_array()) {
                for (const auto& lang : entry["verified_languages"]) {
                    if (lang.is_object()) {
                        std::string code = lang.value("language", "");
                        if (!code.empty()) languages.push_back(code);
                    }
                }
            }
            if (entry.contains("labels") && entry["labels"].is_object()) {
                const json& labels = entry["labels"];
                voice.gender = GenderFromLabel(labels.value("gender", ""));
                for (const char* key : {"use_case", "descriptive", "description"}) {
                    std::string value = labels.value(key, "");
                    if (!value.empty()) voice.styles.push_back(value);
                }
                std::string labelLanguage = labels.value("language", "");
                if (!labelLanguage.empty()) languages.push_back(labelLanguage);
            }
            if (!languages.empty()) voice.language = languages.front();

            if (!wanted.empty() && !languages.empty()) {
                bool match = false;
                for (const auto& code : languages) {
                    if (PrimarySubtag(code) == wanted) { match = true; break; }
                }
                if (!match) continue;
            }
            voices.push_back(std::move(voice));
        }
        return voices;
    }

    SpeakResponse Speak(const SpeakRequest& request) override {
        SpeakResponse out;

        TransportRequest net;
        std::string format;
        if (!BuildSpeakRequest(request, /*stream=*/false, net, format,
                               &out.error)) {
            return out;
        }

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapElevenLabsHttpError(resp.statusCode, resp.body);
            return out;
        }
        if (resp.body.empty()) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "ElevenLabs returned no audio";
            return out;
        }

        out.audio.bytes.assign(resp.body.begin(), resp.body.end());
        out.audio.mimeType = MimeForOutputFormat(format);
        if (const int rate = PcmRate(format); rate > 0) {
            out.durationSec = static_cast<double>(out.audio.bytes.size()) /
                              2.0 / static_cast<double>(rate);
        }
        out.usage.units        = CharacterCost(resp, request.text);
        out.usage.audioSeconds = out.durationSec;
        return out;
    }

    std::future<SpeakResponse> SpeakAsync(const SpeakRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Speak(request); });
    }

    StreamHandle SpeakStream(const SpeakRequest& request,
                             TtsStreamCallback onEvent) override {
        auto handle = std::make_shared<StreamHandleBase>();

        TransportRequest net;
        std::string format;
        Error buildError;
        if (!BuildSpeakRequest(request, /*stream=*/true, net, format,
                               &buildError)) {
            Fail(handle, onEvent, buildError);
            return handle;
        }

        auto state = std::make_shared<StreamState>();
        CancelFn cancel = transport_->ByteStream(
            net,
            [state, handle, onEvent](const std::string& chunk) {
                if (handle->IsCancelled()) return;
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->head.size() < kMaxErrorBody) {
                    state->head += chunk.substr(0, kMaxErrorBody - state->head.size());
                }
                state->pending += chunk;
                size_t newline;
                while ((newline = state->pending.find('\n')) != std::string::npos) {
                    std::string line = state->pending.substr(0, newline);
                    state->pending.erase(0, newline + 1);
                    HandleLine(*state, line, onEvent);
                }
            },
            [state, handle, onEvent](const Error& error, int statusCode) {
                Error final = error;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    // The last line need not end with a newline.
                    if (!handle->IsCancelled() && !state->pending.empty()) {
                        HandleLine(*state, state->pending, onEvent);
                        state->pending.clear();
                    }
                    if (statusCode >= 400) {
                        // The error body arrived through the chunks; it
                        // says more than the bare status.
                        final = MapElevenLabsHttpError(statusCode, state->head);
                    } else if (final.IsOk() && !state->error.IsOk()) {
                        final = state->error;
                    }
                }
                if (handle->IsCancelled() && final.IsOk()) {
                    final.code    = ErrorCode::Cancelled;
                    final.message = "synthesis cancelled";
                }
                TtsStreamEvent terminal;
                if (final.IsOk()) {
                    terminal.kind = TtsStreamEventKind::Done;
                } else {
                    terminal.kind  = TtsStreamEventKind::Error;
                    terminal.error = final;
                }
                if (onEvent) onEvent(terminal);
                handle->MarkDone();
            });

        handle->SetCancelHook([cancel] { if (cancel) cancel(); });
        return handle;
    }

    CloneVoiceResponse CloneVoice(const CloneVoiceRequest& request) override {
        CloneVoiceResponse out;
        auto reject = [&out](ErrorCode code, const std::string& message) {
            out.error.code    = code;
            out.error.message = message;
            return out;
        };

        if (request.displayName.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "CloneVoiceRequest::displayName is empty");
        }
        if (request.samples.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "CloneVoiceRequest::samples is empty");
        }

        std::vector<MultipartPart> parts;
        parts.push_back({"name", request.displayName, "", ""});
        for (size_t i = 0; i < request.samples.size(); ++i) {
            const MediaBlob& sample = request.samples[i];
            if (sample.bytes.empty()) {
                return reject(ErrorCode::InvalidRequest,
                              "voice samples must be inline bytes; the "
                              "ElevenLabs adapter does not download URLs");
            }
            MultipartPart part;
            part.name        = "files";
            part.value.assign(sample.bytes.begin(), sample.bytes.end());
            part.contentType = sample.mimeType.empty() ? "audio/mpeg"
                                                       : sample.mimeType;
            part.filename    = !sample.filename.empty()
                                   ? sample.filename
                                   : "sample" + std::to_string(i + 1) +
                                         ExtensionForMime(part.contentType);
            parts.push_back(std::move(part));
        }
        for (const auto& [key, value] : request.options) {
            if (const auto* s = std::get_if<std::string>(&value)) {
                parts.push_back({key, *s, "", ""});
            } else if (const auto* b = std::get_if<bool>(&value)) {
                parts.push_back({key, *b ? "true" : "false", "", ""});
            } else if (const auto* n = std::get_if<int64_t>(&value)) {
                parts.push_back({key, std::to_string(*n), "", ""});
            } else if (const auto* d = std::get_if<double>(&value)) {
                parts.push_back({key, std::to_string(*d), "", ""});
            }
        }

        TransportRequest net;
        if (!BuildBaseRequest("POST", "/v1/voices/add", net, &out.error)) {
            return out;
        }
        MultipartBody multipart = BuildMultipartBody(parts);
        net.headers.push_back({"content-type", multipart.contentType});
        net.body = std::move(multipart.body);

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapElevenLabsHttpError(resp.statusCode, resp.body);
            return out;
        }

        json body = ParseJsonLenient(resp.body);
        const std::string voiceId =
            body.is_object() ? body.value("voice_id", "") : std::string();
        if (voiceId.empty()) {
            return reject(ErrorCode::ProviderError,
                          "ElevenLabs voice creation returned no voice_id");
        }
        out.voice.id          = voiceId;
        out.voice.displayName = request.displayName;
        out.voice.isCloned    = true;
        return out;
    }

private:
    struct StreamState {
        std::mutex mutex;
        std::string head;      // first bytes of the body, for error mapping
        std::string pending;   // bytes after the last complete line
        Error error;           // first failure seen in the body; audio stops
    };

    static void HandleLine(StreamState& state, const std::string& rawLine,
                           const TtsStreamCallback& onEvent) {
        std::string line = rawLine;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || !state.error.IsOk()) return;

        json payload = ParseJsonLenient(line);
        if (!payload.is_object()) {
            state.error.code    = ErrorCode::ProviderError;
            state.error.message = "ElevenLabs sent a stream line that is not "
                                  "a JSON object";
            return;
        }
        if (ErrorFromDetail(payload, 0, &state.error)) return;
        if (!payload.contains("audio_base64") ||
            !payload["audio_base64"].is_string()) {
            return;   // alignment-only line
        }
        const std::string encoded = payload["audio_base64"].get<std::string>();
        if (encoded.empty()) return;

        bool ok = false;
        TtsStreamEvent chunk;
        chunk.kind       = TtsStreamEventKind::AudioChunk;
        chunk.audioChunk = Base64Decode(encoded, &ok);
        if (!ok) {
            state.error.code    = ErrorCode::ProviderError;
            state.error.message = "ElevenLabs sent an audio chunk that is not "
                                  "valid base64";
            return;
        }
        if (onEvent) onEvent(chunk);
    }

    static void Fail(const std::shared_ptr<StreamHandleBase>& handle,
                     const TtsStreamCallback& onEvent, const Error& error) {
        if (onEvent) {
            TtsStreamEvent event;
            event.kind  = TtsStreamEventKind::Error;
            event.error = error;
            onEvent(event);
        }
        handle->MarkDone();
    }

    static int32_t CharacterCost(const TransportResponse& resp,
                                 const std::string& text) {
        const std::string header = resp.GetHeader("character-cost");
        if (!header.empty()) {
            char* end = nullptr;
            const long cost = std::strtol(header.c_str(), &end, 10);
            if (end && *end == '\0' && cost >= 0) {
                return static_cast<int32_t>(cost);
            }
        }
        return CountUtf8Characters(text);
    }

    // Method, URL, timeout and authentication; `pathAndQuery` starts with '/'.
    bool BuildBaseRequest(const std::string& method,
                          const std::string& pathAndQuery,
                          TransportRequest& outNet, Error* outError) {
        std::string key;
        if (!ResolveKeyOnce(key, outError)) return false;
        std::pair<std::string, std::string> auth;
        if (!AuthHeader(config_, key, auth, outError)) return false;

        outNet.method    = method;
        outNet.url       = baseUrl_ + pathAndQuery;
        outNet.timeoutMs = config_.timeoutMs;
        outNet.headers   = {std::move(auth)};
        return true;
    }

    bool BuildSpeakRequest(const SpeakRequest& request, bool stream,
                           TransportRequest& outNet, std::string& outFormat,
                           Error* outError) {
        auto reject = [outError](ErrorCode code, const std::string& message) {
            if (outError) { outError->code = code; outError->message = message; }
            return false;
        };
        const OptionsMap& provider = config_.providerOptions;
        const OptionsMap& perCall  = request.options;

        if (request.text.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "SpeakRequest::text is empty");
        }
        if (request.ssml) {
            return reject(ErrorCode::UnsupportedFormat,
                          "ElevenLabs speech synthesis takes plain text, not "
                          "SSML");
        }
        std::string voiceId = request.voiceId;
        if (voiceId.empty()) {
            voiceId = StringOption(provider, perCall, "default_voice_id");
        }
        if (voiceId.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "SpeakRequest::voiceId is required (or set the "
                          "\"default_voice_id\" option) — call ListVoices() "
                          "for the ids this account can use");
        }
        outFormat = ResolveOutputFormat(
            request, StringOption(provider, perCall, "output_format"), outError);
        if (outFormat.empty()) return false;

        json voiceSettings = json::object();
        if (request.speed != 1.0) voiceSettings["speed"] = request.speed;
        for (const char* key : {"stability", "similarity_boost", "style"}) {
            const OptionValue* v = FindOption(provider, perCall, key);
            if (!v) continue;
            if (const auto* d = std::get_if<double>(v))        voiceSettings[key] = *d;
            else if (const auto* i = std::get_if<int64_t>(v))  voiceSettings[key] = *i;
        }
        if (const OptionValue* v = FindOption(provider, perCall, "use_speaker_boost")) {
            if (const auto* b = std::get_if<bool>(v)) {
                voiceSettings["use_speaker_boost"] = *b;
            }
        }

        json body;
        body["text"]     = request.text;
        body["model_id"] = !request.model.empty()       ? request.model
                         : !config_.defaultModel.empty() ? config_.defaultModel
                                                         : kDefaultSpeechModel;
        if (!request.language.empty()) {
            body["language_code"] = PrimarySubtag(request.language);
        }
        if (!voiceSettings.empty()) body["voice_settings"] = std::move(voiceSettings);
        ApplyOptions(body, provider);
        ApplyOptions(body, perCall);

        std::string path = "/v1/text-to-speech/" + EncodePathSegment(voiceId);
        if (stream) path += "/stream/with-timestamps";
        path += "?output_format=" + EncodePathSegment(outFormat);
        if (const OptionValue* v = FindOption(provider, perCall, "enable_logging")) {
            if (const auto* b = std::get_if<bool>(v)) {
                path += std::string("&enable_logging=") + (*b ? "true" : "false");
            }
        }

        if (!BuildBaseRequest("POST", path, outNet, outError)) return false;
        outNet.headers.push_back({"content-type", "application/json"});
        outNet.headers.push_back(
            {"accept", stream ? "application/json" : MimeForOutputFormat(outFormat)});
        outNet.body = body.dump();
        return true;
    }

    bool ResolveKeyOnce(std::string& outKey, Error* outError) {
        std::lock_guard<std::mutex> lock(keyMutex_);
        if (!keyResolved_) {
            Error localError;
            resolvedKey_ = ResolveApiKey(config_, &localError);
            if (resolvedKey_.empty()) {
                if (outError) *outError = localError;
                return false;
            }
            keyResolved_ = true;
        }
        outKey = resolvedKey_;
        return true;
    }

    TextToSpeechConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
    bool keyResolved_ = false;
};

} // namespace

std::unique_ptr<ITextToSpeech> CreateElevenLabsTextToSpeech(
    const TextToSpeechConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code    = ErrorCode::NetworkError;
            outError->message = "ElevenLabs adapter needs a transport: build "
                                "with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    return std::make_unique<ElevenLabsTextToSpeech>(config, std::move(transport));
}

} // namespace UltraAI
