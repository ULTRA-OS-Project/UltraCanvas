// UltraAI/include/UltraAIAgent.h
// Agents: a model that uses tools until the task is done.
//
// ITextLLM answers one request. When the model asks for a tool, the caller
// has to run the tool, send the result back and ask again - the agent loop.
// UltraAI::Agent is that loop, written once: the application registers its
// tools as C++ functions, calls Run with a task, and the agent calls the
// model, runs the tools the model asks for, feeds the results back and
// repeats until the model answers without a tool (or a limit is reached).
//
// It works over any ITextLLM - anthropic, openai, qwen, local, llama-cpp,
// mock - so the same agent runs against a cloud model or one on this
// computer. It is part of UltraAI_Core and needs nothing beyond the
// capability interfaces.
//
//   auto agent = CreateAgent(cfg, opts);            // or Agent(llm, opts)
//   agent->AddTool("get_time", "The current local time.",
//       R"({"type":"object","properties":{}})",
//       [](const ToolCall&) { return ToolResult::Ok("14:05"); });
//   AgentResult r = agent->Run("What time is it?");
//
// What it gives an application:
//   - tools: name, description, JSON Schema of the arguments, a handler;
//     ToolArguments reads the arguments the model wrote;
//   - a conversation that carries over between runs (History / SetHistory /
//     ClearHistory), so an agent is a session;
//   - limits: AgentOptions::maxTurns caps the model calls of one run;
//   - permission: AgentOptions::canUseTool sees every call before it runs
//     and allows it, denies it (the model is told) or stops the run;
//   - afterToolCall sees (and may change) every result before the model does;
//   - progress events (turns, text as it streams, tool calls and results)
//     on the thread that called Run;
//   - cancellation from any thread; RunAsync for a run off the UI thread;
//   - subagents: MakeSubagentTool turns one agent into a tool of another.
//
// Version: 0.1.0 - first version
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAITextLLM.h"

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraAI {

// =====================================================================
// Tools
// =====================================================================

// What a tool hands back to the model. `content` is the text the model
// reads; a failed tool sets isError, and the model sees the content
// prefixed with "Error: " so it can react (retry, ask, give up).
struct ToolResult {
    std::string content;
    bool isError = false;

    static ToolResult Ok(std::string text) { return ToolResult{std::move(text), false}; }
    static ToolResult Fail(std::string text) { return ToolResult{std::move(text), true}; }
};

// Runs one tool call. It is called on the thread that called Agent::Run.
// An exception it throws becomes an error result; it does not end the run.
using ToolHandler = std::function<ToolResult(const ToolCall& call)>;

struct AgentTool {
    ToolDefinition definition;   // name, description, parametersJsonSchema
    ToolHandler handler;
};

// Reads the JSON object the model wrote as a tool's arguments
// (ToolCall::argumentsJson), so a handler needs no JSON library.
//
//   ToolArguments args(call.argumentsJson);
//   if (!args.IsValid()) return ToolResult::Fail("arguments are not a JSON object");
//   std::string city = args.String("city").value_or("");
//   double days = args.Number("days").value_or(1);
//
// Each getter returns nullopt when the key is missing or holds another
// type. Nested values come back as their JSON text (Json("filter")).
class ToolArguments {
public:
    explicit ToolArguments(const std::string& json);
    ~ToolArguments();
    ToolArguments(const ToolArguments& other);
    ToolArguments& operator=(const ToolArguments& other);

    // True when the text is one JSON object (an empty text counts as {}).
    bool IsValid() const;
    bool Has(const std::string& key) const;
    std::vector<std::string> Keys() const;

    std::optional<std::string> String(const std::string& key) const;
    std::optional<double> Number(const std::string& key) const;
    std::optional<int64_t> Integer(const std::string& key) const;   // whole numbers only
    std::optional<bool> Boolean(const std::string& key) const;
    // An array whose every element is a string.
    std::optional<std::vector<std::string>> StringArray(const std::string& key) const;
    // The value of any type, written back as compact JSON text.
    std::optional<std::string> Json(const std::string& key) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// `text` as a quoted JSON string - for a tool that answers in JSON.
std::string JsonQuote(const std::string& text);

// =====================================================================
// Permission
// =====================================================================

enum class ToolDecision {
    Allow,   // run the tool
    Deny,    // do not run it; the model is told why (ToolPermission::message)
    Stop     // do not run it and end the run (AgentStopReason::Stopped)
};

struct ToolPermission {
    ToolDecision decision = ToolDecision::Allow;
    std::string message;     // Deny: what the model reads (default: "The user denied this tool call.")

    static ToolPermission Allow() { return {}; }
    static ToolPermission Deny(std::string why = {}) { return {ToolDecision::Deny, std::move(why)}; }
    static ToolPermission Stop() { return {ToolDecision::Stop, {}}; }
};

// =====================================================================
// Events
// =====================================================================

enum class AgentEventKind {
    TurnStarted,       // the model is about to be called (turn = 1, 2, ...)
    TextDelta,         // AgentOptions::stream only: assistant text as it arrives
    AssistantMessage,  // the model's whole reply for the turn (message, usage)
    ToolCallStarted,   // a tool is about to run (toolCall)
    ToolCallDenied,    // canUseTool denied or stopped it (toolCall, toolResult = what the model reads)
    ToolCallFinished,  // a tool has run (toolCall, toolResult)
    Finished           // the run is over (result)
};

struct AgentResult;

struct AgentEvent {
    AgentEventKind kind = AgentEventKind::TurnStarted;
    int turn = 0;
    std::string textDelta;
    Message message;
    ToolCall toolCall;
    ToolResult toolResult;
    TokenUsage usage;
    const AgentResult* result = nullptr;   // Finished only; valid during the callback
};

// Every event is delivered on the thread that called Run (for RunAsync, the
// run's own thread) - streamed text too, never the adapter's thread.
using AgentEventCallback = std::function<void(const AgentEvent& event)>;

// =====================================================================
// Options and result
// =====================================================================

struct AgentOptions {
    std::string model;           // empty -> the provider's default model
    std::string systemPrompt;    // sent first on every request; not kept in History()
    SamplingParams sampling;
    OptionsMap options;          // provider extras, on every request (see ChatRequest::options)

    // Model calls one Run may make; 0 = no limit. A run that reaches it
    // ends with AgentStopReason::MaxTurns.
    int maxTurns = 20;

    // Ask the model through ChatStream and report its text as TextDelta
    // events while it arrives. Off: one Chat call per turn.
    bool stream = false;

    // Seen before every tool call; unset allows all. Called on Run's thread.
    std::function<ToolPermission(const ToolCall& call)> canUseTool;

    // Seen after every tool call, before the model sees the result; may
    // change it (trim a long output, redact a secret, log it).
    std::function<void(const ToolCall& call, ToolResult& result)> afterToolCall;
};

enum class AgentStopReason {
    Completed,        // the model answered without asking for a tool
    MaxTurns,         // AgentOptions::maxTurns reached
    MaxOutputTokens,  // the model's reply was cut off at its token limit
    ContentFiltered,  // the provider refused the reply
    Stopped,          // canUseTool returned ToolDecision::Stop
    Cancelled,        // Cancel() was called
    Error             // the model call failed (AgentResult::error)
};

struct AgentResult {
    std::string text;                    // the model's last reply text
    AgentStopReason stopReason = AgentStopReason::Completed;
    int turns = 0;                       // model calls made
    int toolCalls = 0;                   // tool calls the model asked for (run, denied or not)
    TokenUsage usage;                    // summed over the run's model calls
    std::vector<Message> messages;       // what this run added to History()
    Error error;                         // set when stopReason == Error (or Cancelled)

    bool Succeeded() const { return stopReason == AgentStopReason::Completed; }
};

// =====================================================================
// Agent
// =====================================================================

class Agent {
public:
    // The agent shares `llm`; null makes every Run fail with InvalidRequest.
    explicit Agent(std::shared_ptr<ITextLLM> llm, AgentOptions options = {});
    ~Agent();

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // ----- tools -----
    // False when the name is empty or taken, or the handler is unset.
    bool AddTool(AgentTool tool);
    bool AddTool(const std::string& name, const std::string& description,
                 const std::string& parametersJsonSchema, ToolHandler handler);
    bool RemoveTool(const std::string& name);
    std::vector<ToolDefinition> Tools() const;

    // ----- running -----
    // Send `prompt` as the next user message and loop until the model is
    // done. Blocks. A second Run while one is going fails at once with
    // ErrorCode::InvalidRequest. A run that fails or is cancelled before the
    // model has answered leaves History() as it was.
    AgentResult Run(const std::string& prompt, AgentEventCallback onEvent = nullptr);
    AgentResult Run(Message userMessage, AgentEventCallback onEvent = nullptr);

    // Run on a thread of its own. The agent must outlive the future.
    std::future<AgentResult> RunAsync(std::string prompt, AgentEventCallback onEvent = nullptr);

    // Ask the current run to end: it stops a streaming reply at once, and
    // otherwise after the model call or tool that is running. Any thread;
    // no effect when nothing runs.
    void Cancel();
    bool IsRunning() const;

    // ----- conversation -----
    // The messages so far, without the system prompt. Not while running.
    std::vector<Message> History() const;
    void SetHistory(std::vector<Message> messages);
    void ClearHistory();

    // ----- configuration (not while running) -----
    const AgentOptions& Options() const;
    void SetOptions(AgentOptions options);
    std::shared_ptr<ITextLLM> TextLLM() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Create the text LLM from `config` (as CreateTextLLM does) and an agent
// over it. Returns nullptr and fills outError when the LLM can't be made.
std::unique_ptr<Agent> CreateAgent(const TextLLMConfig& config,
                                   AgentOptions options = {},
                                   Error* outError = nullptr);

// A tool that hands a task to another agent - a subagent with its own
// system prompt, tools and model - and returns that agent's answer. The
// tool takes one argument, {"task": "<what to do>"}. Each call starts the
// subagent on an empty conversation. The parent's Cancel() does not reach
// the subagent; give it a small maxTurns.
AgentTool MakeSubagentTool(std::shared_ptr<Agent> subagent,
                           const std::string& name,
                           const std::string& description);

} // namespace UltraAI
