// UltraAI/tests/test_anthropic_adapter.cpp
// Exercises the Anthropic ITextLLM adapter offline through ScriptedTransport:
// request serialization (system lift, tools, tool results, sampling,
// structured output, headers), response parsing (text, tool calls, usage,
// stop reasons), provider error mapping, SSE stream assembly (text deltas,
// tool-call deltas, usage, terminal events), CountTokens, and credential
// failure.
//
// Uses plain asserts so the test suite has no third-party dependency
// beyond the repo-vendored nlohmann/json (used to inspect recorded
// request bodies).

#include "UltraAIAnthropicTextLLM.h"
#include "UltraAI.h"

#include <nlohmann/json.hpp>

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

TextLLMConfig TestConfig() {
    TextLLMConfig cfg;
    cfg.providerId = "anthropic";
    cfg.apiKey     = "sk-test-key";
    return cfg;
}

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first == name) return kv.second;
    }
    return {};
}

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers    = {{"content-type", "application/json"}};
    r.body       = body.dump();
    return r;
}

void TestRequestSerialization() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"id", "msg_01"}, {"model", "claude-opus-5"},
        {"content", json::array({json{{"type", "text"}, {"text", "hi"}}})},
        {"stop_reason", "end_turn"},
        {"usage", {{"input_tokens", 10}, {"output_tokens", 2}}}}));

    auto llm = CreateAnthropicTextLLM(TestConfig(), nullptr, transport);
    EXPECT_TRUE(llm != nullptr);

    ChatRequest req;
    req.model = "claude-opus-5";
    Message sys;  sys.role = Role::System;    sys.text = "Be terse.";
    Message user; user.role = Role::User;     user.text = "hello";
    Message asst; asst.role = Role::Assistant;
    ToolCall priorCall;
    priorCall.id = "toolu_1"; priorCall.name = "get_weather";
    priorCall.argumentsJson = R"({"location":"Paris"})";
    asst.toolCalls.push_back(priorCall);
    Message toolMsg; toolMsg.role = Role::Tool;
    toolMsg.toolCallId = "toolu_1"; toolMsg.text = "22C and sunny";
    req.messages = {sys, user, asst, toolMsg};

    ToolDefinition tool;
    tool.name = "get_weather";
    tool.description = "Get the weather";
    tool.parametersJsonSchema =
        R"({"type":"object","properties":{"location":{"type":"string"}},"required":["location"]})";
    req.tools.push_back(tool);
    req.toolChoice = ToolChoice::Auto;

    req.sampling.maxOutputTokens = 512;
    req.sampling.temperature     = 0.5;
    req.sampling.stopSequences   = {"END"};

    req.responseFormat = ResponseFormat::JsonSchema;
    req.jsonSchema = R"({"type":"object","properties":{"answer":{"type":"string"}}})";

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("hi"));
    EXPECT_EQ(resp.finishReason, FinishReason::Stop);
    EXPECT_EQ(resp.usage.inputTokens, 10);
    EXPECT_EQ(resp.id, std::string("msg_01"));

    EXPECT_EQ(transport->Requests().size(), static_cast<size_t>(1));
    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(sent.method, std::string("POST"));
    EXPECT_EQ(sent.url, std::string("https://api.anthropic.com/v1/messages"));
    EXPECT_EQ(FindHeader(sent, "x-api-key"), std::string("sk-test-key"));
    EXPECT_EQ(FindHeader(sent, "anthropic-version"), std::string("2023-06-01"));

    json body = json::parse(sent.body);
    EXPECT_EQ(body["model"], std::string("claude-opus-5"));
    EXPECT_EQ(body["max_tokens"], 512);
    EXPECT_EQ(body["system"], std::string("Be terse."));
    EXPECT_EQ(body["temperature"], 0.5);
    EXPECT_EQ(body["stop_sequences"][0], std::string("END"));

    // System messages are lifted: the messages array holds user, assistant
    // (tool_use), and the tool result as a user turn.
    EXPECT_EQ(body["messages"].size(), static_cast<size_t>(3));
    EXPECT_EQ(body["messages"][0]["role"], std::string("user"));
    EXPECT_EQ(body["messages"][0]["content"], std::string("hello"));
    EXPECT_EQ(body["messages"][1]["role"], std::string("assistant"));
    EXPECT_EQ(body["messages"][1]["content"][0]["type"], std::string("tool_use"));
    EXPECT_EQ(body["messages"][1]["content"][0]["input"]["location"],
              std::string("Paris"));
    EXPECT_EQ(body["messages"][2]["role"], std::string("user"));
    EXPECT_EQ(body["messages"][2]["content"][0]["type"], std::string("tool_result"));
    EXPECT_EQ(body["messages"][2]["content"][0]["tool_use_id"],
              std::string("toolu_1"));

    EXPECT_EQ(body["tools"][0]["name"], std::string("get_weather"));
    EXPECT_TRUE(body["tools"][0]["input_schema"].is_object());
    EXPECT_TRUE(!body.contains("tool_choice"));   // Auto is the API default

    EXPECT_EQ(body["output_config"]["format"]["type"], std::string("json_schema"));
    EXPECT_TRUE(body["output_config"]["format"]["schema"].is_object());
    EXPECT_TRUE(!body.contains("stream"));
}

void TestToolCallResponse() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"id", "msg_02"}, {"model", "claude-opus-5"},
        {"content", json::array({
            json{{"type", "text"}, {"text", "Checking."}},
            json{{"type", "tool_use"}, {"id", "toolu_9"},
                 {"name", "get_weather"},
                 {"input", {{"location", "Berlin"}}}}})},
        {"stop_reason", "tool_use"},
        {"usage", {{"input_tokens", 30}, {"output_tokens", 15},
                   {"cache_read_input_tokens", 12}}}}));

    auto llm = CreateAnthropicTextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "weather in berlin?";
    req.messages.push_back(m);

    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.finishReason, FinishReason::ToolCalls);
    EXPECT_EQ(resp.toolCalls.size(), static_cast<size_t>(1));
    EXPECT_EQ(resp.toolCalls[0].id, std::string("toolu_9"));
    EXPECT_EQ(resp.toolCalls[0].name, std::string("get_weather"));
    json args = json::parse(resp.toolCalls[0].argumentsJson);
    EXPECT_EQ(args["location"], std::string("Berlin"));
    EXPECT_EQ(resp.usage.cachedInputTokens, 12);

    // Default model applies when the request leaves it empty.
    json body = json::parse(transport->Requests()[0].body);
    EXPECT_EQ(body["model"], std::string("claude-opus-5"));
}

void TestProviderErrorMapping() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(429, json{
        {"type", "error"},
        {"error", {{"type", "rate_limit_error"},
                   {"message", "Too many requests"}}}}));
    transport->ScriptResponse(JsonResponse(401, json{
        {"type", "error"},
        {"error", {{"type", "authentication_error"},
                   {"message", "invalid x-api-key"}}}}));

    auto llm = CreateAnthropicTextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);

    ChatResponse r1 = llm->Chat(req);
    EXPECT_EQ(r1.error.code, ErrorCode::RateLimited);
    EXPECT_EQ(r1.error.providerCode, std::string("rate_limit_error"));
    EXPECT_TRUE(r1.error.message.find("Too many requests") != std::string::npos);

    ChatResponse r2 = llm->Chat(req);
    EXPECT_EQ(r2.error.code, ErrorCode::AuthenticationFailed);
}

void TestStreaming() {
    auto transport = std::make_shared<ScriptedTransport>();

    auto sseEvent = [](const std::string& event, const json& data) {
        TransportSseEvent ev;
        ev.event = event;
        ev.data  = data.dump();
        return ev;
    };
    std::vector<TransportSseEvent> script = {
        sseEvent("message_start", json{
            {"type", "message_start"},
            {"message", {{"id", "msg_03"},
                         {"usage", {{"input_tokens", 25}}}}}}),
        sseEvent("content_block_start", json{
            {"type", "content_block_start"}, {"index", 0},
            {"content_block", {{"type", "text"}, {"text", ""}}}}),
        sseEvent("content_block_delta", json{
            {"type", "content_block_delta"}, {"index", 0},
            {"delta", {{"type", "text_delta"}, {"text", "Hel"}}}}),
        sseEvent("content_block_delta", json{
            {"type", "content_block_delta"}, {"index", 0},
            {"delta", {{"type", "text_delta"}, {"text", "lo"}}}}),
        sseEvent("content_block_stop", json{
            {"type", "content_block_stop"}, {"index", 0}}),
        sseEvent("content_block_start", json{
            {"type", "content_block_start"}, {"index", 1},
            {"content_block", {{"type", "tool_use"}, {"id", "toolu_s1"},
                               {"name", "lookup"}, {"input", json::object()}}}}),
        sseEvent("content_block_delta", json{
            {"type", "content_block_delta"}, {"index", 1},
            {"delta", {{"type", "input_json_delta"},
                       {"partial_json", "{\"q\":"}}}}),
        sseEvent("content_block_delta", json{
            {"type", "content_block_delta"}, {"index", 1},
            {"delta", {{"type", "input_json_delta"},
                       {"partial_json", "\"cats\"}"}}}}),
        sseEvent("content_block_stop", json{
            {"type", "content_block_stop"}, {"index", 1}}),
        sseEvent("message_delta", json{
            {"type", "message_delta"},
            {"delta", {{"stop_reason", "tool_use"}}},
            {"usage", {{"output_tokens", 40}}}}),
        sseEvent("message_stop", json{{"type", "message_stop"}}),
    };
    transport->ScriptSse(script);

    auto llm = CreateAnthropicTextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "stream please";
    req.messages.push_back(m);

    std::string text;
    std::string toolArgs;
    ToolCall finishedCall;
    bool sawUsage = false, sawDone = false;
    FinishReason finish = FinishReason::Unknown;
    TokenUsage finalUsage;

    StreamHandle handle = llm->ChatStream(req, [&](const StreamEvent& ev) {
        switch (ev.kind) {
            case StreamEventKind::TextDelta:     text += ev.textDelta; break;
            case StreamEventKind::ToolCallDelta: toolArgs += ev.toolCallDelta.argumentsJson; break;
            case StreamEventKind::ToolCallEnd:   finishedCall = ev.toolCallDelta; break;
            case StreamEventKind::UsageUpdate:   sawUsage = true; break;
            case StreamEventKind::Done:
                sawDone = true; finish = ev.finishReason; finalUsage = ev.usage;
                break;
            case StreamEventKind::Error:
                std::cerr << "unexpected stream error: " << ev.error.message
                          << std::endl;
                std::abort();
        }
    });

    EXPECT_TRUE(handle != nullptr);
    EXPECT_TRUE(handle->IsDone());          // ScriptedTransport runs inline
    EXPECT_EQ(text, std::string("Hello"));
    EXPECT_EQ(finishedCall.id, std::string("toolu_s1"));
    EXPECT_EQ(finishedCall.name, std::string("lookup"));
    json args = json::parse(finishedCall.argumentsJson);
    EXPECT_EQ(args["q"], std::string("cats"));
    EXPECT_TRUE(sawUsage);
    EXPECT_TRUE(sawDone);
    EXPECT_EQ(finish, FinishReason::ToolCalls);
    EXPECT_EQ(finalUsage.inputTokens, 25);
    EXPECT_EQ(finalUsage.outputTokens, 40);

    // The streamed request carries stream=true.
    json body = json::parse(transport->Requests()[0].body);
    EXPECT_TRUE(body["stream"].get<bool>());
}

void TestStreamTerminalError() {
    auto transport = std::make_shared<ScriptedTransport>();
    Error rate;
    rate.code = ErrorCode::RateLimited;
    transport->ScriptSse({}, rate, 429);

    auto llm = CreateAnthropicTextLLM(TestConfig(), nullptr, transport);
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);

    bool sawError = false;
    llm->ChatStream(req, [&](const StreamEvent& ev) {
        if (ev.kind == StreamEventKind::Error) {
            sawError = true;
            EXPECT_EQ(ev.error.code, ErrorCode::RateLimited);
        }
    });
    EXPECT_TRUE(sawError);
}

void TestCountTokens() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"input_tokens", 321}}));

    auto llm = CreateAnthropicTextLLM(TestConfig(), nullptr, transport);
    Message m; m.role = Role::User; m.text = "count me";
    EXPECT_EQ(llm->CountTokens("claude-opus-5", {m}), 321);
    EXPECT_EQ(transport->Requests()[0].url,
              std::string("https://api.anthropic.com/v1/messages/count_tokens"));
    json body = json::parse(transport->Requests()[0].body);
    EXPECT_TRUE(!body.contains("max_tokens"));

    // Transport failure falls back to the byte estimate (>= 0, no crash).
    Error netFail; netFail.code = ErrorCode::NetworkError;
    transport->ScriptError(netFail);
    Message longMsg; longMsg.role = Role::User;
    longMsg.text = std::string(400, 'x');
    EXPECT_EQ(llm->CountTokens("claude-opus-5", {longMsg}), 100);
}

void TestMissingCredentials() {
    auto transport = std::make_shared<ScriptedTransport>();
    TextLLMConfig cfg;                       // no apiKey, no vault ref
    auto llm = CreateAnthropicTextLLM(cfg, nullptr, transport);
    EXPECT_TRUE(llm != nullptr);

    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    EXPECT_EQ(resp.error.code, ErrorCode::AuthenticationFailed);
    EXPECT_TRUE(transport->Requests().empty());   // never reached the wire
}

void TestFactoryRegistration() {
    bool hasAnthropic = false;
    for (const auto& p : ListTextLLMProviders()) {
        if (p == "anthropic") hasAnthropic = true;
    }
    EXPECT_TRUE(hasAnthropic);
}

void TestProviderOptionsPassthrough() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"id", "msg_04"}, {"model", "claude-opus-5"},
        {"content", json::array()}, {"stop_reason", "end_turn"}}));

    TextLLMConfig cfg = TestConfig();
    cfg.providerOptions["anthropic-beta"] = std::string("compact-2026-01-12");
    auto llm = CreateAnthropicTextLLM(cfg, nullptr, transport);

    ChatRequest req;
    Message m; m.role = Role::User; m.text = "hi";
    req.messages.push_back(m);
    req.options["thinking"] = std::string(R"({"type":"adaptive"})");

    llm->Chat(req);
    const TransportRequest& sent = transport->Requests()[0];
    EXPECT_EQ(FindHeader(sent, "anthropic-beta"), std::string("compact-2026-01-12"));
    json body = json::parse(sent.body);
    EXPECT_EQ(body["thinking"]["type"], std::string("adaptive"));
}

} // namespace

int main() {
    TestRequestSerialization();
    TestToolCallResponse();
    TestProviderErrorMapping();
    TestStreaming();
    TestStreamTerminalError();
    TestCountTokens();
    TestMissingCredentials();
    TestFactoryRegistration();
    TestProviderOptionsPassthrough();
    std::cout << "test_anthropic_adapter: all checks passed" << std::endl;
    return 0;
}
