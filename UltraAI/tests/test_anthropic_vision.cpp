// UltraAI/tests/test_anthropic_vision.cpp
// The Anthropic IVisionAnalyzer (OCR) adapter, offline through
// ScriptedTransport:
//
//   - capabilities: provider "anthropic", every model declares Ocr only,
//     the configured default model first, nothing runs locally;
//   - the request: POST /v1/messages with the key, claude-opus-5-5 by
//     default, a base64 image block then the transcription instruction,
//     structured output {"text": string} at low effort, "fallbacks":
//     "default" with the server-side-fallback-2026-07-01 beta; a configured
//     "fallbacks" / "output_config" replaces those;
//   - the response: the structured output's text, thinking blocks skipped;
//     a refusal as ContentFiltered, max_tokens as an error, HTTP errors
//     mapped (401 -> AuthenticationFailed);
//   - a task other than Ocr, a missing image or mime type: InvalidRequest
//     without a request; no key: AuthenticationFailed without a request;
//   - the registry: CreateVisionAnalyzer({providerId = "anthropic"}).

#include "UltraAIAnthropicVisionAnalyzer.h"
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

VisionAnalyzerConfig TestConfig() {
    VisionAnalyzerConfig cfg;
    cfg.providerId = "anthropic";
    cfg.apiKey = "sk-test-key";
    return cfg;
}

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers)
        if (kv.first == name) return kv.second;
    return {};
}

TransportResponse Reply(const json& content, const std::string& stopReason = "end_turn") {
    TransportResponse r;
    r.statusCode = 200;
    r.headers = {{"content-type", "application/json"}};
    r.body = json{{"id", "msg_01"}, {"model", "claude-opus-5-5"}, {"content", content},
                  {"stop_reason", stopReason},
                  {"usage", {{"input_tokens", 1200}, {"output_tokens", 40}}}}.dump();
    return r;
}

json TextBlock(const std::string& text) { return json{{"type", "text"}, {"text", text}}; }

VisionAnalyzeRequest OcrRequest() {
    VisionAnalyzeRequest req;
    req.tasks = {VisionTask::Ocr};
    req.image.bytes = {0x89, 'P', 'N', 'G', 1, 2, 3};
    req.image.mimeType = "image/png";
    return req;
}

void TestCapabilities() {
    auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, std::make_shared<ScriptedTransport>());
    EXPECT_TRUE(vision != nullptr);
    const VisionProviderCapabilities caps = vision->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("anthropic"));
    EXPECT_TRUE(!caps.models.empty());
    EXPECT_EQ(caps.models.front().id, std::string("claude-opus-5-5"));
    for (const auto& m : caps.models) {
        EXPECT_EQ(m.supportedTasks.size(), size_t(1));
        EXPECT_TRUE(m.supportedTasks.front() == VisionTask::Ocr);
        EXPECT_TRUE(!m.runsLocally);
    }
    VisionAnalyzerConfig sonnet = TestConfig();
    sonnet.defaultModel = "claude-sonnet-5-5";
    auto other = CreateAnthropicVisionAnalyzer(sonnet, nullptr, std::make_shared<ScriptedTransport>());
    EXPECT_EQ(other->GetCapabilities().models.front().id, std::string("claude-sonnet-5-5"));
}

void TestRequestAndResponse() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(Reply(json::array({
            json{{"type", "thinking"}, {"thinking", ""}, {"signature", "sig"}},
            TextBlock(json{{"text", "Invoice 0042\nTotal: EUR 128.50"}}.dump())})));
    auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, transport);
    const VisionAnalyzeResponse resp = vision->Analyze(OcrRequest());
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.ocrText, std::string("Invoice 0042\nTotal: EUR 128.50"));
    EXPECT_EQ(resp.usage.inputTokens, 1200);

    EXPECT_EQ(transport->Requests().size(), size_t(1));
    const TransportRequest& net = transport->Requests().front();
    EXPECT_EQ(net.method, std::string("POST"));
    EXPECT_TRUE(net.url.find("/v1/messages") != std::string::npos);
    EXPECT_EQ(FindHeader(net, "x-api-key"), std::string("sk-test-key"));
    EXPECT_EQ(FindHeader(net, "anthropic-beta"), std::string("server-side-fallback-2026-07-01"));
    const json body = json::parse(net.body);
    EXPECT_EQ(body["model"], json("claude-opus-5-5"));
    EXPECT_EQ(body["fallbacks"], json("default"));
    EXPECT_EQ(body["output_config"]["effort"], json("low"));
    EXPECT_EQ(body["output_config"]["format"]["type"], json("json_schema"));
    EXPECT_EQ(body["output_config"]["format"]["schema"]["required"], json::array({"text"}));
    EXPECT_TRUE(!body.contains("thinking"));      // Opus 5.5 cannot turn it off; effort steers
    EXPECT_TRUE(!body.contains("temperature"));
    const json& content = body["messages"][0]["content"];
    EXPECT_EQ(body["messages"][0]["role"], json("user"));
    EXPECT_EQ(content[0]["type"], json("image"));
    EXPECT_EQ(content[0]["source"]["type"], json("base64"));
    EXPECT_EQ(content[0]["source"]["media_type"], json("image/png"));
    EXPECT_TRUE(content[0]["source"]["data"].get<std::string>().find('\n') == std::string::npos);
    EXPECT_EQ(content[1]["type"], json("text"));
    EXPECT_TRUE(content[1]["text"].get<std::string>().find("Transcribe") != std::string::npos);
}

void TestConfiguredOptions() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(Reply(json::array({TextBlock("plain words")})));
    VisionAnalyzerConfig cfg = TestConfig();
    cfg.providerOptions["fallbacks"] = std::string(R"([{"model":"claude-opus-5"}])");
    cfg.providerOptions["anthropic-beta"] = std::string("server-side-fallback-2026-06-01");
    cfg.providerOptions["output_config"] = std::string(R"({"effort":"medium"})");
    auto vision = CreateAnthropicVisionAnalyzer(cfg, nullptr, transport);
    const VisionAnalyzeResponse resp = vision->Analyze(OcrRequest());
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.ocrText, std::string("plain words"));   // not JSON: the reply itself
    const json body = json::parse(transport->Requests().front().body);
    EXPECT_EQ(body["fallbacks"][0]["model"], json("claude-opus-5"));
    EXPECT_EQ(body["output_config"], json({{"effort", "medium"}}));
    EXPECT_EQ(FindHeader(transport->Requests().front(), "anthropic-beta"),
              std::string("server-side-fallback-2026-06-01"));
}

void TestOutcomes() {
    {   // a refusal
        auto transport = std::make_shared<ScriptedTransport>();
        transport->ScriptResponse(Reply(json::array(), "refusal"));
        auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, transport);
        EXPECT_TRUE(vision->Analyze(OcrRequest()).error.code == ErrorCode::ContentFiltered);
    }
    {   // cut off
        auto transport = std::make_shared<ScriptedTransport>();
        transport->ScriptResponse(Reply(json::array({TextBlock("{\"text\": \"par")}), "max_tokens"));
        auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, transport);
        EXPECT_TRUE(vision->Analyze(OcrRequest()).error.code == ErrorCode::ProviderError);
    }
    {   // an HTTP error
        auto transport = std::make_shared<ScriptedTransport>();
        TransportResponse r;
        r.statusCode = 401;
        r.body = R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})";
        transport->ScriptResponse(r);
        auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, transport);
        EXPECT_TRUE(vision->Analyze(OcrRequest()).error.code == ErrorCode::AuthenticationFailed);
    }
    {   // no text in the picture
        auto transport = std::make_shared<ScriptedTransport>();
        transport->ScriptResponse(Reply(json::array({TextBlock(R"({"text": ""})")})));
        auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, transport);
        const VisionAnalyzeResponse resp = vision->Analyze(OcrRequest());
        EXPECT_TRUE(resp.error.IsOk() && resp.ocrText.empty());
    }
}

void TestRejectedWithoutARequest() {
    auto transport = std::make_shared<ScriptedTransport>();
    auto vision = CreateAnthropicVisionAnalyzer(TestConfig(), nullptr, transport);
    VisionAnalyzeRequest caption = OcrRequest();
    caption.tasks = {VisionTask::Caption};
    EXPECT_TRUE(vision->Analyze(caption).error.code == ErrorCode::InvalidRequest);
    VisionAnalyzeRequest noImage = OcrRequest();
    noImage.image = {};
    EXPECT_TRUE(vision->Analyze(noImage).error.code == ErrorCode::InvalidRequest);
    VisionAnalyzeRequest noMime = OcrRequest();
    noMime.image.mimeType.clear();
    EXPECT_TRUE(vision->Analyze(noMime).error.code == ErrorCode::InvalidRequest);
    EXPECT_TRUE(transport->Requests().empty());

    VisionAnalyzerConfig keyless;
    keyless.providerId = "anthropic";
    auto noKey = CreateAnthropicVisionAnalyzer(keyless, nullptr, transport);
    EXPECT_TRUE(noKey != nullptr);   // constructs; the key is needed at the request
    EXPECT_TRUE(noKey->Analyze(OcrRequest()).error.code == ErrorCode::AuthenticationFailed);
    EXPECT_TRUE(transport->Requests().empty());
}

void TestRegistry() {
    const std::vector<std::string> ids = ListVisionAnalyzerProviders();
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "anthropic") != ids.end());
    VisionAnalyzerConfig cfg;
    cfg.providerId = "anthropic";
    Error err;
    auto vision = CreateVisionAnalyzer(cfg, &err);
    // A build without UltraNet has no production transport: the factory says
    // so instead of constructing.
    if (vision) EXPECT_EQ(vision->GetCapabilities().providerId, std::string("anthropic"));
    else EXPECT_TRUE(err.code == ErrorCode::NetworkError);
    // A cloud provider: not on the default route unless chosen or allowed.
    SetCloudFallbackAllowed(false);
    const std::vector<std::string> order = ResolveProviderOrder("visionanalyzer", ids);
    EXPECT_TRUE(std::find(order.begin(), order.end(), "anthropic") == order.end());
    SetDefaultProvider("visionanalyzer", "anthropic");
    EXPECT_EQ(ResolveProviderOrder("visionanalyzer", ids).front(), std::string("anthropic"));
    ClearDefaultProviders();
}

} // namespace

int main() {
    TestCapabilities();
    TestRequestAndResponse();
    TestConfiguredOptions();
    TestOutcomes();
    TestRejectedWithoutARequest();
    TestRegistry();
    std::cout << "anthropic vision adapter: all tests passed" << std::endl;
    return 0;
}
