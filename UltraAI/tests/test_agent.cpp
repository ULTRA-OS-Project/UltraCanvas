// UltraAI/tests/test_agent.cpp
// Exercises the agent loop (UltraAIAgent.h): the model <-> tool cycle, what
// is sent each turn, tool results for every call (denied, stopped, unknown,
// throwing), permission and after-call hooks, limits, errors and history
// rollback, conversations across runs, streaming (events on Run's thread),
// cancellation, ToolArguments and subagents.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAI.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace UltraAI;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

ToolCall Call(const std::string& id, const std::string& name, const std::string& args) {
    ToolCall c;
    c.id = id;
    c.name = name;
    c.argumentsJson = args;
    return c;
}

// A text LLM that records every request and answers from a script, through
// Chat and through ChatStream (on a thread of its own, as adapters do).
class ScriptedLLM : public ITextLLM {
public:
    std::vector<ChatRequest> requests;
    std::deque<ChatResponse> replies;
    std::atomic<bool> holdStream{false};   // stream never ends until cancelled

    void Reply(const std::string& text, FinishReason finish = FinishReason::Stop) {
        ChatResponse r;
        r.text = text;
        r.finishReason = finish;
        r.usage.inputTokens = 10;
        r.usage.outputTokens = 5;
        replies.push_back(std::move(r));
    }
    void Tools(std::vector<ToolCall> calls, const std::string& text = {}) {
        ChatResponse r;
        r.text = text;
        r.toolCalls = std::move(calls);
        r.finishReason = FinishReason::ToolCalls;
        r.usage.inputTokens = 10;
        r.usage.outputTokens = 5;
        replies.push_back(std::move(r));
    }
    void Fail(ErrorCode code, const std::string& message) {
        ChatResponse r;
        r.error.code = code;
        r.error.message = message;
        r.finishReason = FinishReason::Error;
        replies.push_back(std::move(r));
    }

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId = "scripted";
        caps.supportsTools = true;
        caps.supportsStreaming = true;
        return caps;
    }

    ChatResponse Next(const ChatRequest& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        requests.push_back(request);
        if (replies.empty()) {
            ChatResponse r;
            r.error.code = ErrorCode::ProviderError;
            r.error.message = "no scripted reply";
            return r;
        }
        ChatResponse r = std::move(replies.front());
        replies.pop_front();
        return r;
    }

    ChatResponse Chat(const ChatRequest& request) override { return Next(request); }

    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        std::promise<ChatResponse> p;
        p.set_value(Chat(request));
        return p.get_future();
    }

    struct Handle : IStreamHandle {
        std::atomic<bool> cancelled{false};
        std::atomic<bool> done{false};
        void Cancel() override { cancelled = true; }
        bool IsDone() const override { return done.load(); }
    };

    StreamHandle ChatStream(const ChatRequest& request, StreamCallback onEvent) override {
        auto handle = std::make_shared<Handle>();
        ChatResponse r = Next(request);
        const bool hold = holdStream.load();
        streamThreads_.emplace_back([handle, r, onEvent, hold]() {
            if (hold) {
                StreamEvent first;
                first.kind = StreamEventKind::TextDelta;
                first.textDelta = "thinking";
                onEvent(first);
                while (!handle->cancelled) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                StreamEvent ev;
                ev.kind = StreamEventKind::Error;
                ev.error.code = ErrorCode::Cancelled;
                onEvent(ev);
                handle->done = true;
                return;
            }
            if (!r.error.IsOk()) {
                StreamEvent ev;
                ev.kind = StreamEventKind::Error;
                ev.error = r.error;
                onEvent(ev);
                handle->done = true;
                return;
            }
            // The text in two pieces, then the tool calls, then Done.
            const size_t half = r.text.size() / 2;
            for (const std::string& piece : {r.text.substr(0, half), r.text.substr(half)}) {
                if (piece.empty()) continue;
                StreamEvent ev;
                ev.kind = StreamEventKind::TextDelta;
                ev.textDelta = piece;
                onEvent(ev);
            }
            for (const auto& call : r.toolCalls) {
                StreamEvent ev;
                ev.kind = StreamEventKind::ToolCallEnd;
                ev.toolCallDelta = call;
                onEvent(ev);
            }
            StreamEvent done;
            done.kind = StreamEventKind::Done;
            done.finishReason = r.finishReason;
            done.usage = r.usage;
            onEvent(done);
            handle->done = true;
        });
        return handle;
    }

    int32_t CountTokens(const std::string&, const std::vector<Message>&) override { return 0; }

    ~ScriptedLLM() override {
        for (auto& t : streamThreads_) if (t.joinable()) t.join();
    }

private:
    std::mutex mutex_;
    std::vector<std::thread> streamThreads_;
};

const char* kCitySchema =
    R"({"type":"object","properties":{"city":{"type":"string"}},"required":["city"]})";

// ---------------------------------------------------------------------

void TestToolRoundTrip() {
    auto llm = std::make_shared<ScriptedLLM>();
    llm->Tools({Call("t1", "weather", R"({"city":"Oslo"})")}, "Let me check.");
    llm->Reply("It is sunny in Oslo.");

    AgentOptions opts;
    opts.systemPrompt = "You are terse.";
    opts.model = "m-1";
    opts.sampling.temperature = 0.0;
    Agent agent(llm, opts);

    std::string seenCity;
    EXPECT_TRUE(agent.AddTool("weather", "Weather for a city.", kCitySchema,
        [&](const ToolCall& call) {
            ToolArguments args(call.argumentsJson);
            seenCity = args.String("city").value_or("");
            return ToolResult::Ok("sunny, 21 C");
        }));
    EXPECT_TRUE(!agent.AddTool("weather", "dup", kCitySchema,
        [](const ToolCall&) { return ToolResult::Ok(""); }));
    EXPECT_TRUE(!agent.AddTool("nohandler", "x", kCitySchema, nullptr));

    std::vector<AgentEventKind> kinds;
    AgentResult r = agent.Run("Weather in Oslo?", [&](const AgentEvent& ev) {
        kinds.push_back(ev.kind);
        if (ev.kind == AgentEventKind::Finished) EXPECT_TRUE(ev.result != nullptr);
    });

    EXPECT_TRUE(r.Succeeded());
    EXPECT_EQ(r.text, "It is sunny in Oslo.");
    EXPECT_EQ(r.turns, 2);
    EXPECT_EQ(r.toolCalls, 1);
    EXPECT_EQ(r.usage.inputTokens, 20);
    EXPECT_EQ(r.usage.outputTokens, 10);
    EXPECT_EQ(seenCity, "Oslo");

    const std::vector<AgentEventKind> expected = {
        AgentEventKind::TurnStarted, AgentEventKind::AssistantMessage,
        AgentEventKind::ToolCallStarted, AgentEventKind::ToolCallFinished,
        AgentEventKind::TurnStarted, AgentEventKind::AssistantMessage,
        AgentEventKind::Finished};
    EXPECT_TRUE(kinds == expected);

    // First request: system prompt first, the user message, the tool offered.
    EXPECT_EQ(llm->requests.size(), size_t(2));
    const ChatRequest& first = llm->requests[0];
    EXPECT_EQ(first.model, "m-1");
    EXPECT_TRUE(first.sampling.temperature.has_value());
    EXPECT_EQ(first.messages.size(), size_t(2));
    EXPECT_TRUE(first.messages[0].role == Role::System);
    EXPECT_EQ(first.messages[0].text, "You are terse.");
    EXPECT_EQ(first.messages[1].text, "Weather in Oslo?");
    EXPECT_EQ(first.tools.size(), size_t(1));
    EXPECT_EQ(first.tools[0].name, "weather");

    // Second request: the assistant's tool call and its result follow.
    const ChatRequest& second = llm->requests[1];
    EXPECT_EQ(second.messages.size(), size_t(4));
    EXPECT_TRUE(second.messages[2].role == Role::Assistant);
    EXPECT_EQ(second.messages[2].text, "Let me check.");
    EXPECT_EQ(second.messages[2].toolCalls.size(), size_t(1));
    EXPECT_TRUE(second.messages[3].role == Role::Tool);
    EXPECT_EQ(second.messages[3].toolCallId, "t1");
    EXPECT_EQ(second.messages[3].text, "sunny, 21 C");

    // History: no system prompt; user, assistant, tool, assistant.
    auto history = agent.History();
    EXPECT_EQ(history.size(), size_t(4));
    EXPECT_TRUE(history[0].role == Role::User);
    EXPECT_EQ(r.messages.size(), size_t(4));
}

void TestUnknownAndThrowingTools() {
    auto llm = std::make_shared<ScriptedLLM>();
    llm->Tools({Call("a", "missing", "{}"), Call("b", "boom", "{}")});
    llm->Reply("done");
    Agent agent(llm);
    agent.AddTool("boom", "Throws.", "", [](const ToolCall&) -> ToolResult {
        throw std::runtime_error("disk on fire");
    });
    AgentResult r = agent.Run("go");
    EXPECT_TRUE(r.Succeeded());
    const auto& msgs = llm->requests[1].messages;
    EXPECT_EQ(msgs.size(), size_t(4));
    EXPECT_TRUE(Contains(msgs[2].text, "Error: There is no tool named \"missing\""));
    EXPECT_TRUE(Contains(msgs[3].text, "Error: The tool failed: disk on fire"));
    // An empty schema is filled with an empty object schema.
    EXPECT_TRUE(Contains(llm->requests[0].tools[0].parametersJsonSchema, "\"object\""));
}

void TestPermissionDenyAndStop() {
    auto llm = std::make_shared<ScriptedLLM>();
    llm->Tools({Call("1", "read", "{}"), Call("2", "delete", "{}"), Call("3", "read", "{}")});
    llm->Reply("ok");

    AgentOptions opts;
    int deleteAsked = 0;
    opts.canUseTool = [&](const ToolCall& call) {
        if (call.name == "delete") { ++deleteAsked; return ToolPermission::Deny("Not allowed here."); }
        return ToolPermission::Allow();
    };
    opts.afterToolCall = [](const ToolCall&, ToolResult& result) {
        result.content += " [checked]";
    };
    Agent agent(llm, opts);
    int reads = 0;
    agent.AddTool("read", "Read.", "", [&](const ToolCall&) { ++reads; return ToolResult::Ok("data"); });
    agent.AddTool("delete", "Delete.", "", [](const ToolCall&) { return ToolResult::Ok("gone"); });

    int denied = 0;
    AgentResult r = agent.Run("tidy up", [&](const AgentEvent& ev) {
        if (ev.kind == AgentEventKind::ToolCallDenied) ++denied;
    });
    EXPECT_TRUE(r.Succeeded());
    EXPECT_EQ(reads, 2);
    EXPECT_EQ(deleteAsked, 1);
    EXPECT_EQ(denied, 1);
    const auto& msgs = llm->requests[1].messages;
    EXPECT_EQ(msgs[2].text, "data [checked]");
    EXPECT_EQ(msgs[3].text, "Error: Not allowed here.");
    EXPECT_EQ(msgs[4].text, "data [checked]");

    // Stop: the run ends, and every call still has a result in the history.
    auto llm2 = std::make_shared<ScriptedLLM>();
    llm2->Tools({Call("1", "delete", "{}"), Call("2", "read", "{}")});
    AgentOptions stopOpts;
    stopOpts.canUseTool = [](const ToolCall& call) {
        return call.name == "delete" ? ToolPermission::Stop() : ToolPermission::Allow();
    };
    Agent stopper(llm2, stopOpts);
    int ran = 0;
    stopper.AddTool("read", "Read.", "", [&](const ToolCall&) { ++ran; return ToolResult::Ok("x"); });
    stopper.AddTool("delete", "Delete.", "", [&](const ToolCall&) { ++ran; return ToolResult::Ok("x"); });
    AgentResult s = stopper.Run("tidy up");
    EXPECT_TRUE(s.stopReason == AgentStopReason::Stopped);
    EXPECT_EQ(ran, 0);
    EXPECT_EQ(s.toolCalls, 2);
    auto history = stopper.History();
    EXPECT_EQ(history.size(), size_t(4));   // user, assistant, two tool results
    EXPECT_TRUE(history[2].role == Role::Tool && history[3].role == Role::Tool);
    EXPECT_EQ(history[3].toolCallId, "2");
}

void TestLimitsAndErrors() {
    // maxTurns: the model keeps asking for a tool.
    auto llm = std::make_shared<ScriptedLLM>();
    for (int i = 0; i < 5; ++i) llm->Tools({Call(std::to_string(i), "t", "{}")});
    AgentOptions opts;
    opts.maxTurns = 3;
    Agent agent(llm, opts);
    agent.AddTool("t", "T.", "", [](const ToolCall&) { return ToolResult::Ok("again"); });
    AgentResult r = agent.Run("loop");
    EXPECT_TRUE(r.stopReason == AgentStopReason::MaxTurns);
    EXPECT_EQ(r.turns, 3);
    EXPECT_EQ(llm->requests.size(), size_t(3));

    // A failure before the model answered leaves the history as it was.
    auto llm2 = std::make_shared<ScriptedLLM>();
    llm2->Reply("first answer");
    llm2->Fail(ErrorCode::RateLimited, "slow down");
    Agent a2(llm2);
    EXPECT_TRUE(a2.Run("one").Succeeded());
    EXPECT_EQ(a2.History().size(), size_t(2));
    AgentResult failed = a2.Run("two");
    EXPECT_TRUE(failed.stopReason == AgentStopReason::Error);
    EXPECT_TRUE(failed.error.code == ErrorCode::RateLimited);
    EXPECT_EQ(a2.History().size(), size_t(2));
    EXPECT_TRUE(failed.messages.empty());

    // Cut off at the token limit.
    auto llm3 = std::make_shared<ScriptedLLM>();
    llm3->Reply("partial", FinishReason::Length);
    Agent a3(llm3);
    EXPECT_TRUE(a3.Run("long").stopReason == AgentStopReason::MaxOutputTokens);

    // No LLM.
    Agent none(nullptr);
    AgentResult n = none.Run("x");
    EXPECT_TRUE(n.stopReason == AgentStopReason::Error);
    EXPECT_TRUE(n.error.code == ErrorCode::InvalidRequest);
}

void TestConversationAcrossRuns() {
    auto llm = std::make_shared<ScriptedLLM>();
    llm->Reply("Hello Ada.");
    llm->Reply("Your name is Ada.");
    llm->Reply("I don't know.");
    Agent agent(llm);
    agent.Run("I am Ada.");
    agent.Run("What is my name?");
    EXPECT_EQ(llm->requests[1].messages.size(), size_t(3));
    EXPECT_EQ(llm->requests[1].messages[0].text, "I am Ada.");
    agent.ClearHistory();
    agent.Run("What is my name?");
    EXPECT_EQ(llm->requests[2].messages.size(), size_t(1));
}

void TestStreaming() {
    auto llm = std::make_shared<ScriptedLLM>();
    llm->Tools({Call("s1", "echo", R"({"text":"hi"})")}, "Calling echo.");
    llm->Reply("Echo said hi.");
    AgentOptions opts;
    opts.stream = true;
    Agent agent(llm, opts);
    agent.AddTool("echo", "Echo.", "", [](const ToolCall& call) {
        return ToolResult::Ok(ToolArguments(call.argumentsJson).String("text").value_or(""));
    });

    const auto runThread = std::this_thread::get_id();
    std::string streamed;
    bool allOnRunThread = true;
    AgentResult r = agent.Run("echo hi", [&](const AgentEvent& ev) {
        if (std::this_thread::get_id() != runThread) allOnRunThread = false;
        if (ev.kind == AgentEventKind::TextDelta) streamed += ev.textDelta;
    });
    EXPECT_TRUE(r.Succeeded());
    EXPECT_TRUE(allOnRunThread);
    EXPECT_EQ(streamed, "Calling echo.Echo said hi.");
    EXPECT_EQ(r.text, "Echo said hi.");
    EXPECT_EQ(r.toolCalls, 1);
    EXPECT_EQ(r.usage.inputTokens, 20);
    EXPECT_EQ(llm->requests[1].messages[2].text, "hi");
}

void TestCancelAndBusy() {
    auto llm = std::make_shared<ScriptedLLM>();
    llm->holdStream = true;
    llm->Reply("never");
    AgentOptions opts;
    opts.stream = true;
    Agent agent(llm, opts);

    std::mutex m;
    std::condition_variable cv;
    bool streaming = false;
    auto future = agent.RunAsync("wait forever", [&](const AgentEvent& ev) {
        if (ev.kind == AgentEventKind::TextDelta) {
            std::lock_guard<std::mutex> lock(m);
            streaming = true;
            cv.notify_all();
        }
    });
    {
        std::unique_lock<std::mutex> lock(m);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return streaming; }));
    }
    EXPECT_TRUE(agent.IsRunning());
    AgentResult busy = agent.Run("second");
    EXPECT_TRUE(busy.error.code == ErrorCode::InvalidRequest);

    agent.Cancel();
    EXPECT_TRUE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    AgentResult r = future.get();
    EXPECT_TRUE(r.stopReason == AgentStopReason::Cancelled);
    EXPECT_TRUE(agent.History().empty());     // cancelled before an answer
    EXPECT_TRUE(!agent.IsRunning());

    // Cancel with nothing running does nothing; the next run is normal.
    agent.Cancel();
    llm->holdStream = false;
    llm->Reply("fine");
    EXPECT_TRUE(agent.Run("again").Succeeded());
}

void TestToolArguments() {
    ToolArguments a(R"({"s":"xé","n":2.5,"i":7,"b":true,"list":["a","b"],
                        "mixed":[1,"a"],"obj":{"k":[1,2]},"z":null})");
    EXPECT_TRUE(a.IsValid());
    EXPECT_EQ(a.String("s").value(), "x\xC3\xA9");
    EXPECT_EQ(a.Number("n").value(), 2.5);
    EXPECT_TRUE(!a.Integer("n").has_value());
    EXPECT_EQ(a.Integer("i").value(), 7);
    EXPECT_EQ(a.Boolean("b").value(), true);
    EXPECT_TRUE(!a.String("n").has_value());
    EXPECT_TRUE((a.StringArray("list").value() == std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(!a.StringArray("mixed").has_value());
    EXPECT_EQ(a.Json("obj").value(), R"({"k":[1,2]})");
    EXPECT_EQ(a.Json("n").value(), "2.5");
    EXPECT_EQ(a.Json("z").value(), "null");
    EXPECT_TRUE(a.Has("z"));
    EXPECT_TRUE(!a.Has("nope"));
    EXPECT_EQ(a.Keys().size(), size_t(8));

    EXPECT_TRUE(ToolArguments("").IsValid());
    EXPECT_TRUE(ToolArguments("  ").Keys().empty());
    EXPECT_TRUE(!ToolArguments("[1,2]").IsValid());
    EXPECT_TRUE(!ToolArguments("{\"a\":").IsValid());
    EXPECT_TRUE(!ToolArguments("{\"a\":1}").String("a").has_value());

    ToolArguments copy = a;
    EXPECT_EQ(copy.Integer("i").value(), 7);

    EXPECT_EQ(JsonQuote("a\"b\n"), "\"a\\\"b\\n\"");
}

void TestSubagent() {
    auto subLlm = std::make_shared<ScriptedLLM>();
    subLlm->Reply("Paris");
    subLlm->Reply("Rome");
    AgentOptions subOpts;
    subOpts.systemPrompt = "You answer geography questions.";
    auto sub = std::make_shared<Agent>(subLlm, subOpts);

    auto llm = std::make_shared<ScriptedLLM>();
    llm->Tools({Call("g1", "geographer", R"({"task":"Capital of France?"})")});
    llm->Tools({Call("g2", "geographer", R"({"task":"Capital of Italy?"})"),
                Call("g3", "geographer", R"({})")});
    llm->Reply("Paris and Rome.");
    Agent agent(llm);
    EXPECT_TRUE(agent.AddTool(MakeSubagentTool(sub, "geographer", "Answers geography questions.")));
    EXPECT_TRUE(Contains(agent.Tools()[0].parametersJsonSchema, "\"task\""));

    AgentResult r = agent.Run("Capitals of France and Italy?");
    EXPECT_TRUE(r.Succeeded());
    EXPECT_EQ(llm->requests[1].messages.back().text, "Paris");
    const auto& third = llm->requests[2].messages;
    EXPECT_EQ(third[third.size() - 2].text, "Rome");
    EXPECT_TRUE(Contains(third.back().text, "\"task\" argument is missing"));
    // Each task starts the subagent on an empty conversation.
    EXPECT_EQ(subLlm->requests[1].messages.size(), size_t(2));   // system + task
    EXPECT_EQ(subLlm->requests[1].messages[1].text, "Capital of Italy?");
}

void TestCreateAgentWithMock() {
    TextLLMConfig cfg;
    cfg.providerId = "mock";
    Error err;
    auto agent = CreateAgent(cfg, {}, &err);
    EXPECT_TRUE(agent != nullptr);
    AgentResult r = agent->Run("hello");
    EXPECT_TRUE(r.Succeeded());
    EXPECT_TRUE(!r.text.empty());

    TextLLMConfig bad;
    bad.providerId = "no-such-provider";
    Error badErr;
    EXPECT_TRUE(CreateAgent(bad, {}, &badErr) == nullptr);
    EXPECT_TRUE(!badErr.IsOk());
}

} // namespace

int main() {
    TestToolRoundTrip();
    TestUnknownAndThrowingTools();
    TestPermissionDenyAndStop();
    TestLimitsAndErrors();
    TestConversationAcrossRuns();
    TestStreaming();
    TestCancelAndBusy();
    TestToolArguments();
    TestSubagent();
    TestCreateAgentWithMock();
    std::cout << "ultraai agent tests passed" << std::endl;
    return 0;
}
