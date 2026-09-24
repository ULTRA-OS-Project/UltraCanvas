// UltraAI/tests/test_elevenlabs_adapter.cpp
// Exercises the ElevenLabs text-to-speech adapter offline through
// ScriptedTransport: one-shot synthesis and its request serialization,
// ElevenLabs' {"detail": ...} error bodies, request validation, both
// authentication schemes (xi-api-key, and bearer for a relay), streamed
// synthesis over newline-delimited JSON split at arbitrary chunk
// boundaries, streamed error bodies, voice listing with language
// filtering, instant voice cloning, and factory registration.
//
// Uses plain asserts so the test suite has no third-party dependency
// beyond the repo-vendored nlohmann/json (used to inspect recorded
// request bodies).

#include "UltraAIElevenLabs.h"
#include "UltraAI.h"
#include "UltraAIBase64.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraAI;
using nlohmann::json;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

// Delivers a scripted response through ByteStream in fixed-size pieces, so
// the adapter's line reassembly is exercised across chunk boundaries the
// way a real network splits a body.
class ChunkingTransport : public ScriptedTransport {
public:
    explicit ChunkingTransport(size_t chunkSize) : chunkSize_(chunkSize) {}

    CancelFn ByteStream(const TransportRequest& request,
                        ByteChunkCallback onChunk,
                        SseCompleteCallback onComplete) override {
        Error transportError;
        TransportResponse resp = Request(request, &transportError);
        if (!transportError.IsOk()) {
            if (onComplete) onComplete(transportError, 0);
            return [] {};
        }
        for (size_t i = 0; i < resp.body.size(); i += chunkSize_) {
            if (onChunk) onChunk(resp.body.substr(i, chunkSize_));
        }
        Error final;
        if (resp.statusCode >= 400) {
            final.code    = ErrorCode::ProviderError;
            final.message = "HTTP " + std::to_string(resp.statusCode);
        }
        if (onComplete) onComplete(final, resp.statusCode);
        return [] {};
    }

private:
    size_t chunkSize_;
};

TransportResponse BytesResponse(int status, const std::string& body,
                                std::vector<std::pair<std::string, std::string>>
                                    headers = {}) {
    TransportResponse r;
    r.statusCode = status;
    r.headers    = std::move(headers);
    r.body       = body;
    return r;
}

TransportResponse JsonResponse(int status, const json& body) {
    return BytesResponse(status, body.dump(),
                         {{"content-type", "application/json"}});
}

json Detail(const char* status, const char* message) {
    return json{{"detail", {{"status", status}, {"message", message}}}};
}

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first == name) return kv.second;
    }
    return {};
}

TextToSpeechConfig Config() {
    TextToSpeechConfig cfg;
    cfg.providerId = "elevenlabs";
    cfg.apiKey     = "el-test-key";
    return cfg;
}

SpeakRequest Request(const char* text = "Hallo Welt") {
    SpeakRequest req;
    req.text    = text;
    req.voiceId = "JBFqnCBsd6RMkjVDRZzb";
    return req;
}

void TestSpeak() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(BytesResponse(
        200, std::string("\xFF\xFB\x90\x00mp3", 7),
        {{"content-type", "audio/mpeg"}, {"character-cost", "10"}}));

    TextToSpeechConfig cfg = Config();
    cfg.providerOptions["stability"] = 0.4;
    auto tts = CreateElevenLabsTextToSpeech(cfg, nullptr, transport);

    SpeakRequest req = Request();
    req.language = "de-DE";
    req.speed    = 1.1;
    req.options["seed"] = int64_t(42);
    req.options["use_speaker_boost"] = true;

    SpeakResponse resp = tts->Speak(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.audio.bytes.size(), size_t(7));
    EXPECT_EQ(resp.audio.mimeType, std::string("audio/mpeg"));
    EXPECT_EQ(resp.usage.units, 10);        // from character-cost

    EXPECT_EQ(transport->Requests().size(), size_t(1));
    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.method, std::string("POST"));
    EXPECT_EQ(sent.url, std::string("https://api.elevenlabs.io/v1/"
                                    "text-to-speech/JBFqnCBsd6RMkjVDRZzb"
                                    "?output_format=mp3_44100_128"));
    EXPECT_EQ(FindHeader(sent, "xi-api-key"), std::string("el-test-key"));
    EXPECT_TRUE(FindHeader(sent, "authorization").empty());
    EXPECT_EQ(FindHeader(sent, "accept"), std::string("audio/mpeg"));

    json body = json::parse(sent.body);
    EXPECT_EQ(body["text"], "Hallo Welt");
    EXPECT_EQ(body["model_id"], "eleven_multilingual_v2");
    EXPECT_EQ(body["language_code"], "de");
    EXPECT_EQ(body["seed"], 42);
    EXPECT_EQ(body["voice_settings"]["speed"], 1.1);
    EXPECT_EQ(body["voice_settings"]["stability"], 0.4);
    EXPECT_EQ(body["voice_settings"]["use_speaker_boost"], true);
    // Reserved keys never reach the body as top-level fields.
    EXPECT_TRUE(!body.contains("stability"));
    EXPECT_TRUE(!body.contains("use_speaker_boost"));
}

void TestSpeakPcmAndOverrides() {
    auto transport = std::make_shared<ScriptedTransport>();
    // 16000 bytes of 16-bit PCM at 16 kHz = 0.5 s. No character-cost
    // header: usage falls back to counting characters, not bytes.
    transport->ScriptResponse(BytesResponse(200, std::string(16000, '\0')));
    transport->ScriptResponse(BytesResponse(200, "ulaw-bytes"));

    TextToSpeechConfig cfg = Config();
    cfg.defaultModel = "eleven_flash_v2_5";
    cfg.providerOptions["default_voice_id"] = "fallback-voice";
    auto tts = CreateElevenLabsTextToSpeech(cfg, nullptr, transport);

    SpeakRequest pcm;
    pcm.text         = "Grüße";                 // 5 characters, 7 bytes
    pcm.format       = TtsAudioFormat::PcmS16Le;
    pcm.sampleRateHz = 16000;
    SpeakResponse resp = tts->Speak(pcm);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.audio.mimeType, std::string("audio/L16"));
    EXPECT_EQ(resp.durationSec, 0.5);
    EXPECT_EQ(resp.usage.units, 5);
    EXPECT_EQ(transport->Requests()[0].url,
              std::string("https://api.elevenlabs.io/v1/text-to-speech/"
                          "fallback-voice?output_format=pcm_16000"));
    EXPECT_EQ(json::parse(transport->Requests()[0].body)["model_id"],
              "eleven_flash_v2_5");
    EXPECT_TRUE(!json::parse(transport->Requests()[0].body)
                     .contains("voice_settings"));

    SpeakRequest ulaw = Request("hi");
    ulaw.options["output_format"]  = std::string("ulaw_8000");
    ulaw.options["enable_logging"] = false;
    resp = tts->Speak(ulaw);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.audio.mimeType, std::string("audio/basic"));
    EXPECT_EQ(resp.durationSec, 0.0);
    EXPECT_EQ(transport->Requests()[1].url,
              std::string("https://api.elevenlabs.io/v1/text-to-speech/"
                          "JBFqnCBsd6RMkjVDRZzb?output_format=ulaw_8000"
                          "&enable_logging=false"));
}

void TestSpeakErrors() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(
        401, Detail("invalid_api_key", "Invalid API key")));
    transport->ScriptResponse(JsonResponse(
        401, Detail("quota_exceeded", "This request exceeds your quota")));
    transport->ScriptResponse(JsonResponse(
        404, Detail("voice_not_found", "A voice with that id was not found")));
    transport->ScriptResponse(JsonResponse(
        422, json{{"detail", json::array({json{{"loc", {"body", "text"}},
                                               {"msg", "field required"}}})}}));
    transport->ScriptResponse(BytesResponse(503, "upstream unavailable"));
    Error refused;
    refused.code = ErrorCode::NetworkError;
    transport->ScriptError(refused);

    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);

    SpeakResponse r = tts->Speak(Request());
    EXPECT_EQ(r.error.code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(r.error.providerCode, std::string("invalid_api_key"));
    EXPECT_EQ(r.error.message, std::string("Invalid API key"));

    // ElevenLabs reports quota exhaustion with 401; the detail status wins.
    EXPECT_EQ(tts->Speak(Request()).error.code, ErrorCode::QuotaExceeded);
    EXPECT_EQ(tts->Speak(Request()).error.code, ErrorCode::InvalidRequest);

    r = tts->Speak(Request());
    EXPECT_EQ(r.error.code, ErrorCode::InvalidRequest);
    EXPECT_EQ(r.error.message, std::string("field required"));

    EXPECT_EQ(tts->Speak(Request()).error.code, ErrorCode::ProviderError);
    EXPECT_EQ(tts->Speak(Request()).error.code, ErrorCode::NetworkError);
}

void TestSpeakRejections() {
    auto transport = std::make_shared<ScriptedTransport>();
    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);

    SpeakRequest noVoice;
    noVoice.text = "hello";
    EXPECT_EQ(tts->Speak(noVoice).error.code, ErrorCode::InvalidRequest);

    SpeakRequest empty = Request("");
    EXPECT_EQ(tts->Speak(empty).error.code, ErrorCode::InvalidRequest);

    SpeakRequest ssml = Request("<speak>hello</speak>");
    ssml.ssml = true;
    EXPECT_EQ(tts->Speak(ssml).error.code, ErrorCode::UnsupportedFormat);

    SpeakRequest wav = Request();
    wav.format = TtsAudioFormat::Wav;
    EXPECT_EQ(tts->Speak(wav).error.code, ErrorCode::UnsupportedFormat);

    SpeakRequest oddRate = Request();
    oddRate.format       = TtsAudioFormat::PcmS16Le;
    oddRate.sampleRateHz = 11025;
    EXPECT_EQ(tts->Speak(oddRate).error.code, ErrorCode::UnsupportedFormat);

    SpeakRequest mp3Rate = Request();
    mp3Rate.sampleRateHz = 48000;
    EXPECT_EQ(tts->Speak(mp3Rate).error.code, ErrorCode::UnsupportedFormat);

    // A caller-supplied voice id cannot rewrite the request path.
    auto pathTransport = std::make_shared<ScriptedTransport>();
    pathTransport->ScriptResponse(BytesResponse(200, "x"));
    auto tts2 = CreateElevenLabsTextToSpeech(Config(), nullptr, pathTransport);
    SpeakRequest sneaky = Request();
    sneaky.voiceId = "../voices?x=1";
    EXPECT_TRUE(tts2->Speak(sneaky).error.IsOk());
    EXPECT_EQ(pathTransport->Requests()[0].url,
              std::string("https://api.elevenlabs.io/v1/text-to-speech/"
                          "..%2Fvoices%3Fx%3D1?output_format=mp3_44100_128"));

    EXPECT_EQ(transport->Requests().size(), size_t(0));
}

void TestCredentialsAndRelay() {
    // No credential: fails before any traffic.
    auto transport = std::make_shared<ScriptedTransport>();
    TextToSpeechConfig bare;
    bare.providerId = "elevenlabs";
    auto tts = CreateElevenLabsTextToSpeech(bare, nullptr, transport);
    EXPECT_EQ(tts->Speak(Request()).error.code, ErrorCode::AuthenticationFailed);
    EXPECT_TRUE(tts->ListVoices().empty());
    EXPECT_EQ(transport->Requests().size(), size_t(0));

    // Hosted service: a relay base URL and the user's session token as a
    // bearer credential. The wire format stays ElevenLabs'.
    auto relay = std::make_shared<ScriptedTransport>();
    relay->ScriptResponse(BytesResponse(200, "audio"));
    TextToSpeechConfig hosted;
    hosted.apiKey  = "ultra-session-token";
    hosted.baseUrl = "https://ai.example.test/elevenlabs/";
    hosted.providerOptions["auth_scheme"] = std::string("bearer");
    auto relayed = CreateElevenLabsTextToSpeech(hosted, nullptr, relay);
    EXPECT_TRUE(relayed->Speak(Request()).error.IsOk());
    const TransportRequest& sent = relay->Requests()[0];
    EXPECT_EQ(sent.url, std::string("https://ai.example.test/elevenlabs/v1/"
                                    "text-to-speech/JBFqnCBsd6RMkjVDRZzb"
                                    "?output_format=mp3_44100_128"));
    EXPECT_EQ(FindHeader(sent, "authorization"),
              std::string("Bearer ultra-session-token"));
    EXPECT_TRUE(FindHeader(sent, "xi-api-key").empty());

    // A misspelt scheme is a configuration error, not a silent fallback.
    TextToSpeechConfig wrong = Config();
    wrong.providerOptions["auth_scheme"] = std::string("basic");
    auto misconfigured = CreateElevenLabsTextToSpeech(wrong, nullptr, transport);
    EXPECT_EQ(misconfigured->Speak(Request()).error.code,
              ErrorCode::InvalidRequest);
    EXPECT_EQ(transport->Requests().size(), size_t(0));
}

std::string StreamLine(const std::string& audio) {
    return json{{"audio_base64", Base64Encode(audio)},
                {"alignment", {{"characters", {"H", "i"}},
                               {"character_start_times_seconds", {0.0, 0.1}},
                               {"character_end_times_seconds", {0.1, 0.2}}}}}
               .dump();
}

void TestSpeakStream() {
    // Chunks of 7 bytes split lines, and even base64 runs, mid-way.
    auto transport = std::make_shared<ChunkingTransport>(7);
    const std::string body = StreamLine("foo") + "\n" +
                             json{{"audio_base64", nullptr},
                                  {"alignment", nullptr}}.dump() + "\r\n" +
                             StreamLine("bar") + "\n\n" +
                             StreamLine("baz");   // no trailing newline
    transport->ScriptResponse(BytesResponse(200, body));

    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);

    std::string audio;
    std::vector<TtsStreamEventKind> kinds;
    StreamHandle handle = tts->SpeakStream(Request(), [&](const TtsStreamEvent& ev) {
        kinds.push_back(ev.kind);
        audio.append(ev.audioChunk.begin(), ev.audioChunk.end());
    });

    EXPECT_EQ(audio, std::string("foobarbaz"));
    EXPECT_EQ(kinds.size(), size_t(4));
    EXPECT_EQ(kinds.back(), TtsStreamEventKind::Done);
    EXPECT_TRUE(handle->IsDone());
    EXPECT_EQ(transport->Requests()[0].url,
              std::string("https://api.elevenlabs.io/v1/text-to-speech/"
                          "JBFqnCBsd6RMkjVDRZzb/stream/with-timestamps"
                          "?output_format=mp3_44100_128"));
    EXPECT_EQ(FindHeader(transport->Requests()[0], "accept"),
              std::string("application/json"));
}

void TestSpeakStreamErrors() {
    // An error body arrives through the chunks: no audio is emitted and the
    // detail status decides the error code.
    auto transport = std::make_shared<ChunkingTransport>(5);
    transport->ScriptResponse(JsonResponse(
        401, Detail("quota_exceeded", "This request exceeds your quota")));
    // Pretty-printed (multi-line) error body: no single line is JSON.
    transport->ScriptResponse(BytesResponse(
        429, Detail("too_many_concurrent_requests", "Slow down").dump(2)));
    // Non-streaming default transport path: the whole body as one chunk.
    transport->ScriptResponse(BytesResponse(200, "not json\n"));

    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);

    auto run = [&tts](size_t* audioEvents) {
        Error last;
        *audioEvents = 0;
        tts->SpeakStream(Request(), [&](const TtsStreamEvent& ev) {
            if (ev.kind == TtsStreamEventKind::AudioChunk) ++*audioEvents;
            if (ev.kind == TtsStreamEventKind::Error) last = ev.error;
        });
        return last;
    };

    size_t audioEvents = 0;
    Error e = run(&audioEvents);
    EXPECT_EQ(e.code, ErrorCode::QuotaExceeded);
    EXPECT_EQ(e.providerCode, std::string("quota_exceeded"));
    EXPECT_EQ(audioEvents, size_t(0));

    e = run(&audioEvents);
    EXPECT_EQ(e.code, ErrorCode::RateLimited);
    EXPECT_EQ(e.message, std::string("Slow down"));
    EXPECT_EQ(audioEvents, size_t(0));

    e = run(&audioEvents);
    EXPECT_EQ(e.code, ErrorCode::ProviderError);

    // Validation failures surface as a single Error event, no traffic.
    auto quiet = std::make_shared<ScriptedTransport>();
    auto tts2 = CreateElevenLabsTextToSpeech(Config(), nullptr, quiet);
    std::vector<TtsStreamEventKind> kinds;
    SpeakRequest noVoice;
    noVoice.text = "hello";
    StreamHandle handle = tts2->SpeakStream(noVoice, [&](const TtsStreamEvent& ev) {
        kinds.push_back(ev.kind);
    });
    EXPECT_EQ(kinds.size(), size_t(1));
    EXPECT_EQ(kinds[0], TtsStreamEventKind::Error);
    EXPECT_TRUE(handle->IsDone());
    EXPECT_EQ(quiet->Requests().size(), size_t(0));
}

void TestListVoices() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"voices", json::array({
        json{{"voice_id", "v-en"}, {"name", "George"}, {"category", "premade"},
             {"labels", {{"gender", "male"}, {"use_case", "narration"}}},
             {"verified_languages", json::array({
                 json{{"language", "en"}, {"model_id", "eleven_multilingual_v2"}}})}},
        json{{"voice_id", "v-de"}, {"name", "Klara"}, {"category", "professional"},
             {"labels", {{"gender", "female"}}},
             {"verified_languages", json::array({json{{"language", "de"}}})}},
        json{{"voice_id", "v-any"}, {"name", "Mine"}, {"category", "cloned"}},
        json{{"name", "no id"}}})}}));
    transport->ScriptResponse(JsonResponse(200, json{{"voices", json::array()}}));

    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);

    const std::vector<VoiceInfo> english = tts->ListVoices("en-US");
    EXPECT_EQ(english.size(), size_t(2));        // v-en, and v-any (no language)
    EXPECT_EQ(english[0].id, std::string("v-en"));
    EXPECT_EQ(english[0].displayName, std::string("George"));
    EXPECT_EQ(english[0].language, std::string("en"));
    EXPECT_EQ(english[0].gender, VoiceGender::Male);
    EXPECT_EQ(english[0].styles.size(), size_t(1));
    EXPECT_TRUE(!english[0].isCloned);
    EXPECT_EQ(english[1].id, std::string("v-any"));
    EXPECT_TRUE(english[1].isCloned);

    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.method, std::string("GET"));
    EXPECT_EQ(sent.url, std::string("https://api.elevenlabs.io/v1/voices"));
    EXPECT_EQ(FindHeader(sent, "xi-api-key"), std::string("el-test-key"));

    EXPECT_TRUE(tts->ListVoices().empty());      // empty second listing

    // No server, no voices — and no error propagated to the caller.
    auto offline = std::make_shared<ScriptedTransport>();
    offline->ScriptResponse(JsonResponse(401, Detail("invalid_api_key", "no")));
    auto tts2 = CreateElevenLabsTextToSpeech(Config(), nullptr, offline);
    EXPECT_TRUE(tts2->ListVoices().empty());
}

void TestCloneVoice() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(
        200, json{{"voice_id", "cloned-1"}, {"requires_verification", false}}));
    transport->ScriptResponse(JsonResponse(
        400, Detail("voice_limit_reached", "You have reached your voice limit")));

    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);

    CloneVoiceRequest req;
    req.displayName = "Narrator";
    MediaBlob sample;
    sample.bytes    = {'R', 'I', 'F', 'F'};
    sample.mimeType = "audio/wav";
    req.samples.push_back(sample);
    req.options["description"]             = std::string("calm narrator");
    req.options["remove_background_noise"] = true;

    CloneVoiceResponse resp = tts->CloneVoice(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.voice.id, std::string("cloned-1"));
    EXPECT_EQ(resp.voice.displayName, std::string("Narrator"));
    EXPECT_TRUE(resp.voice.isCloned);

    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.url, std::string("https://api.elevenlabs.io/v1/voices/add"));
    EXPECT_TRUE(FindHeader(sent, "content-type").rfind(
                    "multipart/form-data; boundary=", 0) == 0);
    EXPECT_TRUE(sent.body.find("name=\"name\"") != std::string::npos);
    EXPECT_TRUE(sent.body.find("Narrator") != std::string::npos);
    EXPECT_TRUE(sent.body.find("name=\"files\"; filename=\"sample1.wav\"") !=
                std::string::npos);
    EXPECT_TRUE(sent.body.find("RIFF") != std::string::npos);
    EXPECT_TRUE(sent.body.find("calm narrator") != std::string::npos);
    EXPECT_TRUE(sent.body.find("name=\"remove_background_noise\"") !=
                std::string::npos);

    resp = tts->CloneVoice(req);
    EXPECT_EQ(resp.error.code, ErrorCode::InvalidRequest);
    EXPECT_EQ(resp.error.providerCode, std::string("voice_limit_reached"));

    // Rejected before any traffic.
    CloneVoiceRequest noSamples;
    noSamples.displayName = "x";
    EXPECT_EQ(tts->CloneVoice(noSamples).error.code, ErrorCode::InvalidRequest);
    CloneVoiceRequest urlOnly;
    urlOnly.displayName = "x";
    MediaBlob remote;
    remote.url = "https://example.test/sample.mp3";
    urlOnly.samples.push_back(remote);
    EXPECT_EQ(tts->CloneVoice(urlOnly).error.code, ErrorCode::InvalidRequest);
    EXPECT_EQ(transport->Requests().size(), size_t(2));
}

void TestFactoryRegistration() {
    const std::vector<std::string> providers = ListTextToSpeechProviders();
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "elevenlabs") !=
                providers.end());

    auto transport = std::make_shared<ScriptedTransport>();
    auto tts = CreateElevenLabsTextToSpeech(Config(), nullptr, transport);
    TTSProviderCapabilities caps = tts->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("elevenlabs"));
    EXPECT_TRUE(!caps.models.empty());
    EXPECT_EQ(caps.models[0].id, std::string("eleven_multilingual_v2"));
    EXPECT_TRUE(caps.models[0].supportsStreaming);
    EXPECT_TRUE(!caps.models[0].runsLocally);
}

} // namespace

int main() {
    TestSpeak();
    TestSpeakPcmAndOverrides();
    TestSpeakErrors();
    TestSpeakRejections();
    TestCredentialsAndRelay();
    TestSpeakStream();
    TestSpeakStreamErrors();
    TestListVoices();
    TestCloneVoice();
    TestFactoryRegistration();
    std::cout << "test_elevenlabs_adapter: all checks passed" << std::endl;
    return 0;
}
