// UltraAI/adapters/minimax/src/MiniMaxTextToSpeech.cpp
// MiniMax ITextToSpeech adapter (POST {baseUrl}/v1/t2a_v2). One-shot
// synthesis returns the audio hex-encoded in the response body; streaming
// synthesis delivers the same hex in SSE chunks, which is what makes
// low-latency playback possible.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIMiniMax.h"

#include "MiniMaxInternal.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <algorithm>
#include <cmath>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using nlohmann::json;
using namespace minimax_detail;

// MiniMax rejects a zero volume; SpeakRequest allows one, so silence is
// expressed as the quietest value the API accepts rather than an error.
constexpr double kMinVolume = 0.01;

const char* FormatName(TtsAudioFormat format) {
    switch (format) {
        case TtsAudioFormat::Mp3:      return "mp3";
        case TtsAudioFormat::Wav:      return "wav";
        case TtsAudioFormat::Flac:     return "flac";
        case TtsAudioFormat::PcmS16Le: return "pcm";
        default:                       return nullptr;   // unsupported
    }
}

const char* FormatMime(TtsAudioFormat format) {
    switch (format) {
        case TtsAudioFormat::Mp3:      return "audio/mpeg";
        case TtsAudioFormat::Wav:      return "audio/wav";
        case TtsAudioFormat::Flac:     return "audio/flac";
        case TtsAudioFormat::PcmS16Le: return "audio/L16";
        default:                       return "application/octet-stream";
    }
}

class MiniMaxTextToSpeech : public ITextToSpeech {
public:
    MiniMaxTextToSpeech(TextToSpeechConfig config,
                        std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    TTSProviderCapabilities GetCapabilities() const override {
        TTSProviderCapabilities caps;
        caps.providerId = "minimax";

        auto add = [&caps](const char* id, const char* name) {
            TTSModelInfo model;
            model.id                   = id;
            model.displayName          = name;
            model.supportedFormats     = {TtsAudioFormat::Mp3,
                                          TtsAudioFormat::Wav,
                                          TtsAudioFormat::Flac,
                                          TtsAudioFormat::PcmS16Le};
            model.supportsSsml         = false;
            model.supportsStreaming    = true;
            // Cloning exists at MiniMax but needs its file-upload flow,
            // which this adapter does not implement (see CloneVoice).
            model.supportsVoiceCloning = false;
            model.runsLocally          = false;
            caps.models.push_back(std::move(model));
        };
        // The API accepts any model string; these are the ones this adapter
        // has been exercised against. Newer ids work without a code change.
        add("speech-2.8-turbo", "MiniMax Speech 2.8 Turbo");
        add("speech-2.8-hd",    "MiniMax Speech 2.8 HD");
        add("speech-2.6-turbo", "MiniMax Speech 2.6 Turbo");
        add("speech-2.6-hd",    "MiniMax Speech 2.6 HD");
        return caps;
    }

    // POST /v1/get_voice. MiniMax's voice records carry no language tag, so
    // a `language` filter cannot be applied — it is ignored rather than
    // silently returning nothing.
    std::vector<VoiceInfo> ListVoices(const std::string& language) override {
        (void)language;
        std::vector<VoiceInfo> voices;

        std::string key;
        if (!ResolveKeyOnce(key, nullptr)) return voices;

        TransportRequest net;
        net.method    = "POST";
        net.url       = baseUrl_ + "/v1/get_voice";
        net.timeoutMs = config_.timeoutMs;
        net.headers   = {{"content-type", "application/json"},
                         {"authorization", "Bearer " + key}};
        net.body      = json{{"voice_type", "all"}}.dump();

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk() || resp.statusCode >= 400) return voices;

        json body = ParseJsonLenient(resp.body);
        Error envelope;
        if (!body.is_object() || !CheckBaseResp(body, &envelope)) return voices;

        auto collect = [&voices, &body](const char* key, bool cloned) {
            if (!body.contains(key) || !body[key].is_array()) return;
            for (const auto& entry : body[key]) {
                if (!entry.is_object()) continue;
                VoiceInfo voice;
                voice.id = entry.value("voice_id", "");
                if (voice.id.empty()) continue;
                voice.displayName = entry.value("voice_name", voice.id);
                voice.isCloned    = cloned;
                voices.push_back(std::move(voice));
            }
        };
        collect("system_voice", false);
        collect("voice_cloning", true);
        collect("voice_generation", true);
        return voices;
    }

    SpeakResponse Speak(const SpeakRequest& request) override {
        SpeakResponse out;

        TransportRequest net;
        if (!BuildRequest(request, /*stream=*/false, net, &out.error)) return out;

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapMiniMaxHttpError(resp.statusCode, resp.body);
            return out;
        }

        json body = ParseJsonLenient(resp.body);
        if (!body.is_object()) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "MiniMax response is not a JSON object";
            return out;
        }
        if (!CheckBaseResp(body, &out.error)) return out;

        const std::string hex =
            body.contains("data") && body["data"].is_object()
                ? body["data"].value("audio", "") : std::string();
        if (hex.empty() || !HexDecode(hex, out.audio.bytes)) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "MiniMax returned no decodable audio";
            return out;
        }
        out.audio.mimeType = FormatMime(request.format);

        if (body.contains("extra_info") && body["extra_info"].is_object()) {
            // audio_length is milliseconds.
            out.durationSec =
                body["extra_info"].value("audio_length", 0.0) / 1000.0;
        }
        out.usage.units        = CountUtf8Characters(request.text);
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
        Error buildError;
        if (!BuildRequest(request, /*stream=*/true, net, &buildError)) {
            Fail(handle, onEvent, buildError);
            return handle;
        }

        // Chunks arrive as SSE events carrying the same hex payload as the
        // one-shot response; the last one adds extra_info and no audio.
        auto state = std::make_shared<StreamState>();
        state->format = request.format;

        CancelFn cancel = transport_->SseStream(
            net,
            [state, handle, onEvent](const TransportSseEvent& event) {
                if (handle->IsCancelled()) return;
                json payload = ParseJsonLenient(event.data);
                if (!payload.is_object()) return;

                Error envelope;
                if (!CheckBaseResp(payload, &envelope)) {
                    state->error = envelope;
                    return;
                }
                if (!payload.contains("data") || !payload["data"].is_object()) {
                    return;
                }
                const std::string hex = payload["data"].value("audio", "");
                if (hex.empty()) return;

                TtsStreamEvent chunk;
                if (!HexDecode(hex, chunk.audioChunk)) {
                    state->error.code    = ErrorCode::ProviderError;
                    state->error.message = "MiniMax sent an audio chunk that "
                                           "is not valid hex";
                    return;
                }
                chunk.kind = TtsStreamEventKind::AudioChunk;
                if (onEvent) onEvent(chunk);
            },
            [state, handle, onEvent](const Error& error, int statusCode) {
                (void)statusCode;
                TtsStreamEvent terminal;
                Error final = error.IsOk() ? state->error : error;
                if (handle->IsCancelled() && final.IsOk()) {
                    final.code    = ErrorCode::Cancelled;
                    final.message = "synthesis cancelled";
                }
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
        (void)request;
        CloneVoiceResponse out;
        out.error.code = ErrorCode::UnsupportedFormat;
        // Cloning at MiniMax is a two-step flow — upload the sample through
        // the file API, then call the voice-clone endpoint — and the cloned
        // voice only becomes listable after its first synthesis. None of
        // that is implemented here; the console does it in one screen.
        out.error.message = "the MiniMax adapter does not implement voice "
                            "cloning; create the voice in the MiniMax "
                            "console and pass its id as SpeakRequest::voiceId";
        return out;
    }

private:
    struct StreamState {
        TtsAudioFormat format = TtsAudioFormat::Mp3;
        Error error;
    };

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

    bool BuildRequest(const SpeakRequest& request, bool stream,
                      TransportRequest& outNet, Error* outError) {
        auto reject = [outError](ErrorCode code, const std::string& message) {
            if (outError) { outError->code = code; outError->message = message; }
            return false;
        };

        if (request.text.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "SpeakRequest::text is empty");
        }
        if (request.ssml) {
            return reject(ErrorCode::UnsupportedFormat,
                          "MiniMax speech synthesis takes plain text, not "
                          "SSML");
        }
        if (request.voiceId.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "SpeakRequest::voiceId is required; MiniMax has no "
                          "default voice — call ListVoices() for the ids this "
                          "account can use");
        }
        const char* format = FormatName(request.format);
        if (!format) {
            return reject(ErrorCode::UnsupportedFormat,
                          "MiniMax supports mp3, wav, flac and 16-bit PCM");
        }

        json voice;
        voice["voice_id"] = request.voiceId;
        voice["speed"]    = request.speed;
        voice["vol"]      = std::max(kMinVolume, request.volume);
        voice["pitch"]    = static_cast<int>(std::lround(request.pitch));
        if (!request.style.empty()) voice["emotion"] = request.style;

        json audio;
        audio["format"]  = format;
        audio["channel"] = 1;
        if (request.sampleRateHz > 0) audio["sample_rate"] = request.sampleRateHz;

        json body;
        body["model"]         = !request.model.empty()       ? request.model
                              : !config_.defaultModel.empty() ? config_.defaultModel
                                                              : kDefaultSpeechModel;
        body["text"]          = request.text;
        body["stream"]        = stream;
        body["output_format"] = "hex";
        body["language_boost"] =
            request.language.empty() ? "auto" : request.language;
        body["voice_setting"] = std::move(voice);
        body["audio_setting"] = std::move(audio);

        ApplyOptions(body, config_.providerOptions, {});
        ApplyOptions(body, request.options, {});

        std::string key;
        if (!ResolveKeyOnce(key, outError)) return false;

        outNet.method    = "POST";
        outNet.url       = baseUrl_ + "/v1/t2a_v2";
        outNet.timeoutMs = config_.timeoutMs;
        outNet.headers   = {{"content-type", "application/json"},
                            {"authorization", "Bearer " + key}};
        if (stream) outNet.headers.push_back({"accept", "text/event-stream"});
        outNet.body      = body.dump();
        return true;
    }

    bool ResolveKeyOnce(std::string& outKey, Error* outError) {
        std::lock_guard<std::mutex> lock(keyMutex_);
        if (!keyResolved_) {
            if (!minimax_detail::ResolveKey(config_, resolvedKey_, outError)) {
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

std::unique_ptr<ITextToSpeech> CreateMiniMaxTextToSpeech(
    const TextToSpeechConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code    = ErrorCode::NetworkError;
            outError->message = "MiniMax adapter needs a transport: build "
                                "with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    return std::make_unique<MiniMaxTextToSpeech>(config, std::move(transport));
}

} // namespace UltraAI
