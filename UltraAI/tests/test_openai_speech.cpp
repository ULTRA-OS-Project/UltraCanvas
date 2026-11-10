// UltraAI/tests/test_openai_speech.cpp
// The OpenAI-compatible ISpeechToText adapter, offline through
// ScriptedTransport:
//
//   - the request: POST /v1/audio/transcriptions, multipart with the file
//     (its name and MIME type), model (gpt-4o-transcribe by default),
//     response_format json, the language's primary subtag, the prompt,
//     options as form fields, the bearer key;
//   - whisper-1 with timestamps asks verbose_json and parses segments and
//     words; translation goes to /v1/audio/translations with whisper-1;
//   - a custom baseUrl: keyless, whisper-1 by default;
//   - refused before a request: no bytes, a URL, over 25 MB (hosted API);
//   - errors mapped (401 -> AuthenticationFailed); live transcription is an
//     error event;
//   - the registry: "openai" for "speechtotext", off the default route
//     until chosen.

#include "UltraAIOpenAI.h"
#include "UltraAI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
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

SpeechToTextConfig KeyConfig() {
    SpeechToTextConfig cfg;
    cfg.providerId = "openai";
    cfg.apiKey = "sk-test-key";
    return cfg;
}

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers)
        if (kv.first == name) return kv.second;
    return {};
}

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers = {{"content-type", "application/json"}};
    r.body = body.dump();
    return r;
}

// The value of a plain form field in a multipart body; "" when absent.
std::string Field(const std::string& body, const std::string& name) {
    const std::string marker = "name=\"" + name + "\"\r\n\r\n";
    const size_t at = body.find(marker);
    if (at == std::string::npos) return {};
    const size_t start = at + marker.size();
    return body.substr(start, body.find("\r\n", start) - start);
}

bool Has(const std::string& body, const std::string& part) { return body.find(part) != std::string::npos; }

TranscribeRequest Recording() {
    TranscribeRequest req;
    req.audio.bytes = {'I', 'D', '3', 1, 2, 3, 4};
    req.audio.mimeType = "audio/mpeg";
    req.audio.filename = "memo.mp3";
    req.segmentTimestamps = false;
    return req;
}

void TestTranscription() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"text", "Hello there."},
                                                     {"usage", {{"type", "tokens"}, {"input_tokens", 14},
                                                                {"output_tokens", 4}}}}));
    auto stt = CreateOpenAISpeechToText(KeyConfig(), nullptr, transport);
    EXPECT_TRUE(stt != nullptr);
    TranscribeRequest req = Recording();
    req.language = "de-CH";
    req.prompt = "UltraCanvas, UltraAI";
    req.options["temperature"] = 0.0;
    const TranscribeResponse resp = stt->Transcribe(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("Hello there."));
    EXPECT_EQ(resp.usage.inputTokens, 14);

    const TransportRequest& net = transport->Requests().front();
    EXPECT_EQ(net.url, std::string("https://api.openai.com/v1/audio/transcriptions"));
    EXPECT_EQ(FindHeader(net, "authorization"), std::string("Bearer sk-test-key"));
    EXPECT_TRUE(FindHeader(net, "content-type").rfind("multipart/form-data; boundary=", 0) == 0);
    EXPECT_EQ(Field(net.body, "model"), std::string("gpt-4o-transcribe"));
    EXPECT_EQ(Field(net.body, "response_format"), std::string("json"));
    EXPECT_EQ(Field(net.body, "language"), std::string("de"));
    EXPECT_EQ(Field(net.body, "prompt"), std::string("UltraCanvas, UltraAI"));
    EXPECT_EQ(Field(net.body, "temperature"), std::string("0.0"));
    EXPECT_TRUE(Has(net.body, "name=\"file\"; filename=\"memo.mp3\""));
    EXPECT_TRUE(Has(net.body, "Content-Type: audio/mpeg"));
    EXPECT_TRUE(!Has(net.body, "timestamp_granularities"));
}

void TestWhisperTimestampsAndTranslation() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
            {"text", "One. Two."}, {"language", "english"}, {"duration", 3.5},
            {"segments", json::array({json{{"start", 0.0}, {"end", 1.5}, {"text", "One."}},
                                      json{{"start", 1.5}, {"end", 3.5}, {"text", "Two."}}})},
            {"words", json::array({json{{"word", "One"}, {"start", 0.1}, {"end", 0.6}}})}}));
    transport->ScriptResponse(JsonResponse(200, json{{"text", "Good morning."}}));
    auto stt = CreateOpenAISpeechToText(KeyConfig(), nullptr, transport);
    TranscribeRequest req = Recording();
    req.model = "whisper-1";
    req.segmentTimestamps = true;
    req.wordTimestamps = true;
    TranscribeResponse resp = stt->Transcribe(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.segments.size(), size_t(2));
    EXPECT_EQ(resp.segments[1].text, std::string("Two."));
    EXPECT_TRUE(resp.durationSec == 3.5);
    EXPECT_EQ(resp.segments.back().words.size(), size_t(1));
    const std::string body = transport->Requests()[0].body;
    EXPECT_EQ(Field(body, "response_format"), std::string("verbose_json"));
    EXPECT_TRUE(Has(body, "timestamp_granularities[]\"\r\n\r\nsegment"));
    EXPECT_TRUE(Has(body, "timestamp_granularities[]\"\r\n\r\nword"));

    TranscribeRequest translate = Recording();
    translate.translateToEnglish = true;
    translate.language = "de";
    resp = stt->Transcribe(translate);
    EXPECT_EQ(resp.text, std::string("Good morning."));
    const TransportRequest& net = transport->Requests()[1];
    EXPECT_EQ(net.url, std::string("https://api.openai.com/v1/audio/translations"));
    EXPECT_EQ(Field(net.body, "model"), std::string("whisper-1"));
    EXPECT_EQ(Field(net.body, "language"), std::string(""));   // translations take none
}

void TestSelfHosted() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"text", "local words"}}));
    SpeechToTextConfig cfg;
    cfg.providerId = "openai";
    cfg.baseUrl = "http://127.0.0.1:8000/";
    auto stt = CreateOpenAISpeechToText(cfg, nullptr, transport);
    const TranscribeResponse resp = stt->Transcribe(Recording());
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("local words"));
    const TransportRequest& net = transport->Requests().front();
    EXPECT_EQ(net.url, std::string("http://127.0.0.1:8000/v1/audio/transcriptions"));
    EXPECT_TRUE(FindHeader(net, "authorization").empty());
    EXPECT_EQ(Field(net.body, "model"), std::string("whisper-1"));
    EXPECT_EQ(stt->GetCapabilities().models.front().id, std::string("whisper-1"));
}

void TestRefusedAndErrors() {
    auto transport = std::make_shared<ScriptedTransport>();
    auto stt = CreateOpenAISpeechToText(KeyConfig(), nullptr, transport);
    TranscribeRequest empty;
    EXPECT_TRUE(stt->Transcribe(empty).error.code == ErrorCode::InvalidRequest);
    TranscribeRequest url;
    url.audio.url = "https://example.com/a.mp3";
    EXPECT_TRUE(stt->Transcribe(url).error.code == ErrorCode::InvalidRequest);
    TranscribeRequest huge = Recording();
    huge.audio.bytes.assign(25 * 1024 * 1024 + 1, 0);
    EXPECT_TRUE(stt->Transcribe(huge).error.code == ErrorCode::InvalidRequest);
    EXPECT_TRUE(transport->Requests().empty());

    TransportResponse unauthorized;
    unauthorized.statusCode = 401;
    unauthorized.body = R"({"error":{"message":"Incorrect API key","type":"invalid_request_error","code":"invalid_api_key"}})";
    transport->ScriptResponse(unauthorized);
    EXPECT_TRUE(stt->Transcribe(Recording()).error.code == ErrorCode::AuthenticationFailed);

    bool sawError = false;
    auto live = stt->StartLiveTranscribe(Recording(), [&sawError](const StreamingTranscriptEvent& ev) {
        sawError = ev.kind == StreamingTranscriptKind::Error;
    });
    EXPECT_TRUE(live == nullptr && sawError);

    const STTProviderCapabilities caps = stt->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("openai"));
    EXPECT_EQ(caps.maxFileSizeBytes, 25 * 1024 * 1024);
    EXPECT_EQ(caps.models.front().id, std::string("gpt-4o-transcribe"));
    EXPECT_TRUE(std::find(caps.supportedEncodings.begin(), caps.supportedEncodings.end(), AudioEncoding::Mp3) !=
                caps.supportedEncodings.end());
}

void TestRegistry() {
    const std::vector<std::string> ids = ListSpeechToTextProviders();
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "openai") != ids.end());
    SetCloudFallbackAllowed(false);
    std::vector<std::string> order = ResolveProviderOrder("speechtotext", ids);
    EXPECT_TRUE(std::find(order.begin(), order.end(), "openai") == order.end());
    SetDefaultProvider("speechtotext", "openai");
    EXPECT_EQ(ResolveProviderOrder("speechtotext", ids).front(), std::string("openai"));
    ClearDefaultProviders();
}

} // namespace

int main() {
    TestTranscription();
    TestWhisperTimestampsAndTranslation();
    TestSelfHosted();
    TestRefusedAndErrors();
    TestRegistry();
    std::cout << "openai speech-to-text adapter: all tests passed" << std::endl;
    return 0;
}
