// UltraAI/src/Agent.cpp
// The agent loop over any ITextLLM - see UltraAIAgent.h.
//
// One run: append the user message, then per turn send the system prompt
// and the history, append the model's reply, and when it asks for tools run
// each one (through canUseTool / afterToolCall) and append a Tool message
// per call. Every tool call the model made gets a Tool message - denied,
// stopped and cancelled ones included - because a provider refuses a
// conversation where a tool call has no result.
//
// Version: 0.1.0 - first version
// Last Modified: 2026-11-09
// Author: UltraAI Module

#include "UltraAIAgent.h"
#include "UltraAIMiniJson.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>

namespace UltraAI {

namespace {

using detail::JsonReader;
using detail::JsonValue;

// How long a cancelled stream may take to deliver its terminal event before
// the run gives up on it (an adapter may drop events once cancelled).
constexpr std::chrono::milliseconds kCancelGrace{2000};

void AddUsage(TokenUsage& into, const TokenUsage& from) {
    into.inputTokens       += from.inputTokens;
    into.outputTokens      += from.outputTokens;
    into.cachedInputTokens += from.cachedInputTokens;
    into.reasoningTokens   += from.reasoningTokens;
    into.audioSeconds      += from.audioSeconds;
    into.units             += from.units;
}

bool UsageIsEmpty(const TokenUsage& u) {
    return u.inputTokens == 0 && u.outputTokens == 0 &&
           u.cachedInputTokens == 0 && u.reasoningTokens == 0;
}

void AppendNumber(std::string& out, double value) {
    // JSON numbers are dot-decimal; write them without the process locale.
    if (!std::isfinite(value)) { out += "null"; return; }
    if (value == std::floor(value) && std::fabs(value) < 9.0e15) {
        out += std::to_string(static_cast<long long>(value));
        return;
    }
    // Up to 15 significant digits, built by hand so the decimal point is '.'.
    const bool negative = value < 0;
    double v = negative ? -value : value;
    long long whole = static_cast<long long>(v);
    double frac = v - static_cast<double>(whole);
    std::string digits;
    for (int i = 0; i < 12 && frac > 0; ++i) {
        frac *= 10.0;
        const int d = static_cast<int>(frac);
        digits += static_cast<char>('0' + d);
        frac -= d;
    }
    while (!digits.empty() && digits.back() == '0') digits.pop_back();
    if (negative) out += '-';
    out += std::to_string(whole);
    if (!digits.empty()) { out += '.'; out += digits; }
}

void WriteJson(std::string& out, const JsonValue& v) {
    switch (v.kind) {
        case JsonValue::Kind::Null:    out += "null"; break;
        case JsonValue::Kind::Boolean: out += v.boolean ? "true" : "false"; break;
        case JsonValue::Kind::Number:  AppendNumber(out, v.number); break;
        case JsonValue::Kind::String:  detail::AppendJsonString(out, v.string); break;
        case JsonValue::Kind::Array:
            out += '[';
            for (size_t i = 0; i < v.array.size(); ++i) {
                if (i) out += ',';
                WriteJson(out, v.array[i]);
            }
            out += ']';
            break;
        case JsonValue::Kind::Object:
            out += '{';
            for (size_t i = 0; i < v.objectKeys.size(); ++i) {
                if (i) out += ',';
                detail::AppendJsonString(out, v.objectKeys[i]);
                out += ':';
                WriteJson(out, v.objectValues[i]);
            }
            out += '}';
            break;
    }
}

bool IsBlank(const std::string& s) {
    for (const char c : s) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

Message ToolMessage(const ToolCall& call, const ToolResult& result) {
    Message m;
    m.role = Role::Tool;
    m.toolCallId = call.id;
    m.name = call.name;
    m.text = result.isError ? "Error: " + result.content : result.content;
    return m;
}

} // namespace

// =====================================================================
// ToolArguments
// =====================================================================

struct ToolArguments::Impl {
    JsonValue root;
    bool valid = false;

    const JsonValue* Get(const std::string& key) const {
        return valid ? root.Get(key) : nullptr;
    }
};

ToolArguments::ToolArguments(const std::string& json) : impl_(std::make_unique<Impl>()) {
    if (IsBlank(json)) {
        impl_->root.kind = JsonValue::Kind::Object;
        impl_->valid = true;
        return;
    }
    JsonReader reader(json);
    impl_->valid = reader.Parse(impl_->root) &&
                   impl_->root.kind == JsonValue::Kind::Object;
}

ToolArguments::~ToolArguments() = default;

ToolArguments::ToolArguments(const ToolArguments& other)
    : impl_(std::make_unique<Impl>(*other.impl_)) {}

ToolArguments& ToolArguments::operator=(const ToolArguments& other) {
    if (this != &other) *impl_ = *other.impl_;
    return *this;
}

bool ToolArguments::IsValid() const { return impl_->valid; }

bool ToolArguments::Has(const std::string& key) const {
    return impl_->Get(key) != nullptr;
}

std::vector<std::string> ToolArguments::Keys() const {
    if (!impl_->valid) return {};
    return impl_->root.objectKeys;
}

std::optional<std::string> ToolArguments::String(const std::string& key) const {
    const JsonValue* v = impl_->Get(key);
    if (!v || v->kind != JsonValue::Kind::String) return std::nullopt;
    return v->string;
}

std::optional<double> ToolArguments::Number(const std::string& key) const {
    const JsonValue* v = impl_->Get(key);
    if (!v || v->kind != JsonValue::Kind::Number) return std::nullopt;
    return v->number;
}

std::optional<int64_t> ToolArguments::Integer(const std::string& key) const {
    const JsonValue* v = impl_->Get(key);
    if (!v || v->kind != JsonValue::Kind::Number) return std::nullopt;
    if (v->number != std::floor(v->number) || std::fabs(v->number) > 9.0e15) {
        return std::nullopt;
    }
    return static_cast<int64_t>(v->number);
}

std::optional<bool> ToolArguments::Boolean(const std::string& key) const {
    const JsonValue* v = impl_->Get(key);
    if (!v || v->kind != JsonValue::Kind::Boolean) return std::nullopt;
    return v->boolean;
}

std::optional<std::vector<std::string>> ToolArguments::StringArray(const std::string& key) const {
    const JsonValue* v = impl_->Get(key);
    if (!v || v->kind != JsonValue::Kind::Array) return std::nullopt;
    std::vector<std::string> out;
    out.reserve(v->array.size());
    for (const auto& item : v->array) {
        if (item.kind != JsonValue::Kind::String) return std::nullopt;
        out.push_back(item.string);
    }
    return out;
}

std::optional<std::string> ToolArguments::Json(const std::string& key) const {
    const JsonValue* v = impl_->Get(key);
    if (!v) return std::nullopt;
    std::string out;
    WriteJson(out, *v);
    return out;
}

std::string JsonQuote(const std::string& text) {
    std::string out;
    detail::AppendJsonString(out, text);
    return out;
}

// =====================================================================
// Agent
// =====================================================================

struct Agent::Impl {
    std::shared_ptr<ITextLLM> llm;
    AgentOptions options;
    std::vector<AgentTool> tools;
    std::vector<Message> history;

    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};

    // The stream of the turn in progress, so Cancel() can stop it.
    std::mutex streamMutex;
    StreamHandle activeStream;

    const AgentTool* FindTool(const std::string& name) const {
        for (const auto& t : tools) {
            if (t.definition.name == name) return &t;
        }
        return nullptr;
    }

    ChatRequest BuildRequest() const {
        ChatRequest req;
        req.model = options.model;
        req.sampling = options.sampling;
        req.options = options.options;
        if (!options.systemPrompt.empty()) {
            Message sys;
            sys.role = Role::System;
            sys.text = options.systemPrompt;
            req.messages.push_back(std::move(sys));
        }
        req.messages.insert(req.messages.end(), history.begin(), history.end());
        req.tools.reserve(tools.size());
        for (const auto& t : tools) req.tools.push_back(t.definition);
        return req;
    }

    ChatResponse CallModel(const ChatRequest& req, const AgentEventCallback& emit, int turn);
    ChatResponse CallModelStreaming(const ChatRequest& req, const AgentEventCallback& emit, int turn);
    ToolResult RunTool(const ToolCall& call);
    AgentResult RunLoop(Message userMessage, const AgentEventCallback& onEvent);
};

ChatResponse Agent::Impl::CallModel(const ChatRequest& req,
                                    const AgentEventCallback& emit, int turn) {
    if (options.stream) return CallModelStreaming(req, emit, turn);
    return llm->Chat(req);
}

ChatResponse Agent::Impl::CallModelStreaming(const ChatRequest& req,
                                             const AgentEventCallback& emit, int turn) {
    // The adapter calls back on its own thread; the deltas are queued and
    // reported from this one, so every agent event has the same thread.
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::string> deltas;
        ChatResponse response;
        TokenUsage lastUsage;
        bool finished = false;
    };
    auto state = std::make_shared<State>();

    StreamHandle handle = llm->ChatStream(req, [state](const StreamEvent& ev) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->finished) return;
        switch (ev.kind) {
            case StreamEventKind::TextDelta:
                state->response.text += ev.textDelta;
                state->deltas.push_back(ev.textDelta);
                break;
            case StreamEventKind::ToolCallDelta:
                break;   // the finished call arrives as ToolCallEnd
            case StreamEventKind::ToolCallEnd:
                state->response.toolCalls.push_back(ev.toolCallDelta);
                break;
            case StreamEventKind::UsageUpdate:
                state->lastUsage = ev.usage;
                break;
            case StreamEventKind::Done:
                state->response.finishReason = ev.finishReason;
                state->response.usage = UsageIsEmpty(ev.usage) ? state->lastUsage : ev.usage;
                state->finished = true;
                break;
            case StreamEventKind::Error:
                state->response.error = ev.error;
                if (state->response.error.IsOk()) {
                    state->response.error.code = ErrorCode::ProviderError;
                }
                state->response.finishReason = FinishReason::Error;
                state->finished = true;
                break;
        }
        state->cv.notify_all();
    });

    {
        std::lock_guard<std::mutex> lock(streamMutex);
        activeStream = handle;
    }
    if (cancelRequested.load() && handle) handle->Cancel();

    std::chrono::steady_clock::time_point cancelledAt{};
    bool sawCancel = false;
    std::unique_lock<std::mutex> lock(state->mutex);
    for (;;) {
        while (!state->deltas.empty()) {
            std::string delta = std::move(state->deltas.front());
            state->deltas.pop_front();
            if (emit) {
                lock.unlock();
                AgentEvent ev;
                ev.kind = AgentEventKind::TextDelta;
                ev.turn = turn;
                ev.textDelta = std::move(delta);
                emit(ev);
                lock.lock();
            }
        }
        if (state->finished) break;

        if (cancelRequested.load()) {
            if (!sawCancel) {
                sawCancel = true;
                cancelledAt = std::chrono::steady_clock::now();
            }
            const bool handleDone = !handle || handle->IsDone();
            if (handleDone || std::chrono::steady_clock::now() - cancelledAt > kCancelGrace) {
                break;
            }
        }
        state->cv.wait_for(lock, std::chrono::milliseconds(20));
    }
    ChatResponse response = state->response;
    if (!state->finished) {
        response.error.code = ErrorCode::Cancelled;
        response.error.message = "cancelled";
        response.finishReason = FinishReason::Error;
    }
    state->finished = true;   // late events after this are ignored
    lock.unlock();

    {
        std::lock_guard<std::mutex> guard(streamMutex);
        activeStream.reset();
    }
    return response;
}

ToolResult Agent::Impl::RunTool(const ToolCall& call) {
    const AgentTool* tool = FindTool(call.name);
    if (!tool) return ToolResult::Fail("There is no tool named \"" + call.name + "\".");
    try {
        return tool->handler(call);
    } catch (const std::exception& e) {
        return ToolResult::Fail(std::string("The tool failed: ") + e.what());
    } catch (...) {
        return ToolResult::Fail("The tool failed.");
    }
}

AgentResult Agent::Impl::RunLoop(Message userMessage, const AgentEventCallback& onEvent) {
    AgentResult result;
    const size_t historyStart = history.size();
    bool modelAnswered = false;

    auto emit = [&](AgentEvent ev) { if (onEvent) onEvent(ev); };
    auto finish = [&](AgentStopReason reason) -> AgentResult {
        result.stopReason = reason;
        if (!modelAnswered &&
            (reason == AgentStopReason::Error || reason == AgentStopReason::Cancelled)) {
            history.resize(historyStart);
        }
        result.messages.assign(history.begin() + static_cast<std::ptrdiff_t>(
                                   std::min(historyStart, history.size())),
                               history.end());
        AgentEvent ev;
        ev.kind = AgentEventKind::Finished;
        ev.turn = result.turns;
        ev.usage = result.usage;
        ev.result = &result;
        emit(ev);
        return std::move(result);
    };
    auto cancelled = [&]() -> AgentResult {
        result.error.code = ErrorCode::Cancelled;
        result.error.message = "cancelled";
        return finish(AgentStopReason::Cancelled);
    };

    if (!llm) {
        result.error.code = ErrorCode::InvalidRequest;
        result.error.message = "the agent has no text LLM";
        return finish(AgentStopReason::Error);
    }

    userMessage.role = Role::User;
    history.push_back(std::move(userMessage));

    for (;;) {
        if (cancelRequested.load()) return cancelled();
        if (options.maxTurns > 0 && result.turns >= options.maxTurns) {
            return finish(AgentStopReason::MaxTurns);
        }

        const int turn = ++result.turns;
        AgentEvent started;
        started.kind = AgentEventKind::TurnStarted;
        started.turn = turn;
        emit(started);

        ChatResponse reply = CallModel(BuildRequest(), onEvent, turn);
        AddUsage(result.usage, reply.usage);

        if (!reply.error.IsOk()) {
            if (reply.error.code == ErrorCode::Cancelled || cancelRequested.load()) {
                return cancelled();
            }
            result.error = reply.error;
            return finish(AgentStopReason::Error);
        }

        Message assistant;
        assistant.role = Role::Assistant;
        assistant.text = reply.text;
        assistant.toolCalls = reply.toolCalls;
        history.push_back(assistant);
        modelAnswered = true;
        result.text = reply.text;

        AgentEvent answered;
        answered.kind = AgentEventKind::AssistantMessage;
        answered.turn = turn;
        answered.message = assistant;
        answered.usage = reply.usage;
        emit(answered);

        if (reply.toolCalls.empty()) {
            switch (reply.finishReason) {
                case FinishReason::Length:          return finish(AgentStopReason::MaxOutputTokens);
                case FinishReason::ContentFiltered: return finish(AgentStopReason::ContentFiltered);
                default:                            return finish(AgentStopReason::Completed);
            }
        }

        // Run the tools in the order the model asked for them. Once the run
        // is stopping, the remaining calls still get a result.
        bool stop = false;
        bool cancel = false;
        for (const ToolCall& call : reply.toolCalls) {
            ++result.toolCalls;
            AgentEvent ev;
            ev.turn = turn;
            ev.toolCall = call;

            if (stop || cancel || cancelRequested.load()) {
                if (!stop) cancel = true;
                ToolResult skipped = ToolResult::Fail(
                    cancel ? "The run was cancelled before this tool ran."
                           : "The run was stopped before this tool ran.");
                history.push_back(ToolMessage(call, skipped));
                continue;
            }

            ToolPermission permission;
            if (options.canUseTool) permission = options.canUseTool(call);

            if (permission.decision != ToolDecision::Allow) {
                ToolResult denied;
                denied.isError = true;
                if (permission.decision == ToolDecision::Stop) {
                    stop = true;
                    denied.content = "The user stopped the run at this tool call.";
                } else {
                    denied.content = permission.message.empty()
                        ? "The user denied this tool call." : permission.message;
                }
                ev.kind = AgentEventKind::ToolCallDenied;
                ev.toolResult = denied;
                emit(ev);
                history.push_back(ToolMessage(call, denied));
                continue;
            }

            ev.kind = AgentEventKind::ToolCallStarted;
            emit(ev);

            ToolResult toolResult = RunTool(call);
            if (options.afterToolCall) options.afterToolCall(call, toolResult);

            ev.kind = AgentEventKind::ToolCallFinished;
            ev.toolResult = toolResult;
            emit(ev);
            history.push_back(ToolMessage(call, toolResult));
        }

        if (stop) return finish(AgentStopReason::Stopped);
        if (cancel) return cancelled();
    }
}

Agent::Agent(std::shared_ptr<ITextLLM> llm, AgentOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->llm = std::move(llm);
    impl_->options = std::move(options);
}

Agent::~Agent() = default;

bool Agent::AddTool(AgentTool tool) {
    if (tool.definition.name.empty() || !tool.handler) return false;
    if (impl_->FindTool(tool.definition.name)) return false;
    if (tool.definition.parametersJsonSchema.empty()) {
        tool.definition.parametersJsonSchema = R"({"type":"object","properties":{}})";
    }
    impl_->tools.push_back(std::move(tool));
    return true;
}

bool Agent::AddTool(const std::string& name, const std::string& description,
                    const std::string& parametersJsonSchema, ToolHandler handler) {
    AgentTool tool;
    tool.definition.name = name;
    tool.definition.description = description;
    tool.definition.parametersJsonSchema = parametersJsonSchema;
    tool.handler = std::move(handler);
    return AddTool(std::move(tool));
}

bool Agent::RemoveTool(const std::string& name) {
    auto& tools = impl_->tools;
    for (auto it = tools.begin(); it != tools.end(); ++it) {
        if (it->definition.name == name) {
            tools.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<ToolDefinition> Agent::Tools() const {
    std::vector<ToolDefinition> out;
    out.reserve(impl_->tools.size());
    for (const auto& t : impl_->tools) out.push_back(t.definition);
    return out;
}

AgentResult Agent::Run(const std::string& prompt, AgentEventCallback onEvent) {
    Message m;
    m.role = Role::User;
    m.text = prompt;
    return Run(std::move(m), std::move(onEvent));
}

AgentResult Agent::Run(Message userMessage, AgentEventCallback onEvent) {
    bool expected = false;
    if (!impl_->running.compare_exchange_strong(expected, true)) {
        AgentResult busy;
        busy.stopReason = AgentStopReason::Error;
        busy.error.code = ErrorCode::InvalidRequest;
        busy.error.message = "the agent is already running";
        return busy;
    }
    impl_->cancelRequested.store(false);

    struct Reset {
        Impl* impl;
        ~Reset() { impl->running.store(false); impl->cancelRequested.store(false); }
    } reset{impl_.get()};

    return impl_->RunLoop(std::move(userMessage), onEvent);
}

std::future<AgentResult> Agent::RunAsync(std::string prompt, AgentEventCallback onEvent) {
    return std::async(std::launch::async,
        [this, prompt = std::move(prompt), onEvent = std::move(onEvent)]() {
            return Run(prompt, onEvent);
        });
}

void Agent::Cancel() {
    if (!impl_->running.load()) return;
    impl_->cancelRequested.store(true);
    StreamHandle stream;
    {
        std::lock_guard<std::mutex> lock(impl_->streamMutex);
        stream = impl_->activeStream;
    }
    if (stream) stream->Cancel();
}

bool Agent::IsRunning() const { return impl_->running.load(); }

std::vector<Message> Agent::History() const { return impl_->history; }

void Agent::SetHistory(std::vector<Message> messages) { impl_->history = std::move(messages); }

void Agent::ClearHistory() { impl_->history.clear(); }

const AgentOptions& Agent::Options() const { return impl_->options; }

void Agent::SetOptions(AgentOptions options) { impl_->options = std::move(options); }

std::shared_ptr<ITextLLM> Agent::TextLLM() const { return impl_->llm; }

std::unique_ptr<Agent> CreateAgent(const TextLLMConfig& config, AgentOptions options,
                                   Error* outError) {
    std::unique_ptr<ITextLLM> llm = CreateTextLLM(config, outError);
    if (!llm) return nullptr;
    return std::make_unique<Agent>(std::shared_ptr<ITextLLM>(std::move(llm)),
                                   std::move(options));
}

AgentTool MakeSubagentTool(std::shared_ptr<Agent> subagent,
                           const std::string& name,
                           const std::string& description) {
    AgentTool tool;
    tool.definition.name = name;
    tool.definition.description = description;
    tool.definition.parametersJsonSchema =
        R"({"type":"object","properties":{"task":{"type":"string",)"
        R"("description":"The task for the subagent, with everything it needs to know."}},)"
        R"("required":["task"]})";
    tool.handler = [subagent](const ToolCall& call) -> ToolResult {
        if (!subagent) return ToolResult::Fail("The subagent is not available.");
        ToolArguments args(call.argumentsJson);
        const std::string task = args.String("task").value_or("");
        if (task.empty()) return ToolResult::Fail("The \"task\" argument is missing.");
        subagent->ClearHistory();
        AgentResult r = subagent->Run(task);
        switch (r.stopReason) {
            case AgentStopReason::Completed:
                return ToolResult::Ok(r.text);
            case AgentStopReason::MaxTurns:
            case AgentStopReason::MaxOutputTokens:
                return ToolResult::Fail("The subagent did not finish. Its last reply: " + r.text);
            default:
                return ToolResult::Fail("The subagent failed: " +
                    (r.error.message.empty() ? std::string("it stopped") : r.error.message));
        }
    };
    return tool;
}

} // namespace UltraAI
