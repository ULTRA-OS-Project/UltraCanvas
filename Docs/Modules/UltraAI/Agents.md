# UltraAI Agents — a model that uses tools until the task is done

**Version:** 0.1.0
**Last Modified:** 2026-11-09

## Overview

`ITextLLM` answers one request. When the model asks for a tool, someone
has to run the tool, send the result back and ask again, and repeat until
the model has its answer. That is the agent loop. `UltraAI::Agent` is that
loop, written once for every application: register the tools as C++
functions, call `Run` with a task, and the agent takes it from there.

The agent works over any `ITextLLM`: `anthropic`, `openai`, `qwen`,
`local`, `llama-cpp` or `mock`. So the same agent runs against a cloud model
or one on this computer. It is part of `UltraAI_Core`, needs nothing beyond
the capability interfaces, and builds with the module's default options.

Header: `UltraAIAgent.h` (included by `UltraAI.h`). Tests:
`UltraAI/tests/test_agent.cpp`.

```cpp
#include <UltraAI.h>
#include <iostream>
using namespace UltraAI;

void Example() {
    TextLLMConfig cfg;                  // empty providerId -> the default route
    AgentOptions opts;
    opts.systemPrompt = "You help with the user's files. Be brief.";

    Error err;
    std::unique_ptr<Agent> agent = CreateAgent(cfg, opts, &err);
    if (!agent) { std::cerr << err.message << '\n'; return; }

    agent->AddTool("word_count", "Count the words in a text.",
        R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"]})",
        [](const ToolCall& call) {
            ToolArguments args(call.argumentsJson);
            std::string text = args.String("text").value_or("");
            int words = 0;
            bool inWord = false;
            for (char c : text) {
                bool space = (c == ' ' || c == '\n' || c == '\t');
                if (!space && !inWord) ++words;
                inWord = !space;
            }
            return ToolResult::Ok(std::to_string(words));
        });

    AgentResult r = agent->Run("How many words are in 'the quick brown fox'?");
    if (r.Succeeded()) std::cout << r.text << '\n';
}
```

## Contents

- [Overview](#overview)
- [How a run goes](#how-a-run-goes)
- [Tools](#tools)
  - [Reading the arguments](#reading-the-arguments)
  - [Results and errors](#results-and-errors)
- [Options](#options)
- [Permission and after-call hooks](#permission-and-after-call-hooks)
- [Progress events](#progress-events)
- [Streaming](#streaming)
- [Running off the UI thread, and cancelling](#running-off-the-ui-thread-and-cancelling)
- [Conversations](#conversations)
- [How a run ends](#how-a-run-ends)
- [Subagents](#subagents)
- [Limits of this version](#limits-of-this-version)

## How a run goes

1. The prompt becomes the next user message in the agent's history.
2. The agent sends the system prompt, the history and the tool definitions
   to the model. This is one *turn*.
3. The model's reply (its text and any tool calls) is added to the history.
4. If the reply asks for no tool, the run is over. Otherwise each tool call
   runs in the order the model gave, through `canUseTool` and
   `afterToolCall`, and its result is added to the history as a `Role::Tool`
   message with the call's id. Then the next turn starts at step 2.

Every tool call the model makes gets a result in the history: one that
ran, one that was denied, one for a tool that does not exist, and the
remaining calls of a run that was stopped or cancelled. Providers refuse a
conversation in which a tool call has no result, so the history stays one
the model can carry on from.

## Tools

A tool is a `ToolDefinition` (name, description, JSON Schema of its
arguments) and a `ToolHandler`. `AddTool` refuses an empty or duplicate
name and a missing handler. An empty schema becomes
`{"type":"object","properties":{}}`, a tool that takes no arguments.

```cpp
#include <UltraAI.h>
using namespace UltraAI;

void AddTools(Agent& agent) {
    AgentTool tool;
    tool.definition.name = "get_time";
    tool.definition.description = "The current local time, as HH:MM.";
    tool.handler = [](const ToolCall&) { return ToolResult::Ok("14:05"); };
    agent.AddTool(tool);

    agent.RemoveTool("get_time");
    std::vector<ToolDefinition> offered = agent.Tools();
    (void)offered;
}
```

Handlers run on the thread that called `Run`, one at a time.

### Reading the arguments

The model writes a tool's arguments as a JSON object
(`ToolCall::argumentsJson`). `ToolArguments` reads it, so a handler needs no
JSON library:

| Getter | Returns |
|---|---|
| `IsValid()` | the text is one JSON object (blank text counts as `{}`) |
| `Has(key)`, `Keys()` | whether a key is there; all keys in order |
| `String(key)` | a string value |
| `Number(key)` | any number |
| `Integer(key)` | a whole number (`2.5` gives nullopt) |
| `Boolean(key)` | `true` / `false` |
| `StringArray(key)` | an array whose every element is a string |
| `Json(key)` | any value, written back as compact JSON (for nested objects) |

Each getter returns `std::nullopt` when the key is missing or its value has
another type. Models do sometimes send the wrong type, so check what you get.
`JsonQuote(text)` gives a quoted JSON string, for a tool that answers in
JSON.

### Results and errors

A handler returns `ToolResult::Ok(text)` or `ToolResult::Fail(text)`. The
model reads a failed result as `Error: <text>` and can react: try other
arguments, ask the user, or give up. An exception thrown by a handler
becomes a failed result (`Error: The tool failed: <what()>`), and a call to
a tool the agent does not have gets `Error: There is no tool named "x".`.
Neither one ends the run.

## Options

`AgentOptions`, given to the constructor or `CreateAgent` and changed with
`SetOptions` between runs:

| Field | Default | Meaning |
|---|---|---|
| `model` | empty | model id; empty uses the provider's default |
| `systemPrompt` | empty | sent first on every request; not kept in `History()` |
| `sampling` | unset | temperature, max output tokens, ... (`SamplingParams`) |
| `options` | empty | provider extras on every request (`ChatRequest::options`), e.g. `thinking` for `anthropic` |
| `maxTurns` | 20 | model calls one run may make; 0 means no limit |
| `stream` | false | ask through `ChatStream` and report the text as it arrives |
| `canUseTool` | unset | sees each call before it runs (see below) |
| `afterToolCall` | unset | sees and may change each result before the model does |

## Permission and after-call hooks

`canUseTool` decides about every call before it runs:

- `ToolPermission::Allow()` runs the tool.
- `ToolPermission::Deny(why)` does not run it. The model reads `why`
  (default: *The user denied this tool call.*) and can carry on without it.
- `ToolPermission::Stop()` does not run it and ends the run with
  `AgentStopReason::Stopped`.

`afterToolCall` sees every result of a tool that ran and may change it:
trim a long output, remove a secret, or write it to a log.

```cpp
#include <UltraAI.h>
using namespace UltraAI;

AgentOptions SafeOptions() {
    AgentOptions opts;
    opts.canUseTool = [](const ToolCall& call) {
        if (call.name == "delete_file") return ToolPermission::Deny("Deleting is not allowed.");
        if (call.name == "format_disk") return ToolPermission::Stop();
        return ToolPermission::Allow();
    };
    opts.afterToolCall = [](const ToolCall&, ToolResult& result) {
        if (result.content.size() > 20000) result.content.resize(20000);
    };
    return opts;
}
```

A program that asks the user before a tool runs does it inside
`canUseTool`. The run waits for the answer, because the hook runs on the
run's own thread.

## Progress events

`Run` takes an optional `AgentEventCallback`. Every event comes on the
thread that called `Run`, streamed text included, never on the adapter's
thread:

| `AgentEventKind` | Fields set | When |
|---|---|---|
| `TurnStarted` | `turn` | the model is about to be called |
| `TextDelta` | `turn`, `textDelta` | streaming only: text as it arrives |
| `AssistantMessage` | `turn`, `message`, `usage` | the model's whole reply for the turn |
| `ToolCallStarted` | `turn`, `toolCall` | a tool is about to run |
| `ToolCallDenied` | `turn`, `toolCall`, `toolResult` | `canUseTool` denied or stopped it |
| `ToolCallFinished` | `turn`, `toolCall`, `toolResult` | a tool has run |
| `Finished` | `turn`, `usage`, `result` | the run is over; `result` is valid during the call |

## Streaming

With `AgentOptions::stream` set, each turn goes through `ChatStream`. The
text arrives as `TextDelta` events, the tool calls are taken from the
stream's `ToolCallEnd` events, and the usage comes from `Done` (or from the
last `UsageUpdate` when `Done` has none). The result is the same as without
streaming.

## Running off the UI thread, and cancelling

`RunAsync(prompt, onEvent)` runs on a thread of its own and returns a
`std::future<AgentResult>`. The agent must outlive the future. Events then
arrive on that thread, so a window posts them to its own thread before it
touches a widget.

`Cancel()` may be called from any thread. A streaming reply stops at once;
otherwise the run ends after the model call or tool that is running. The
remaining tool calls of that turn are given a *cancelled* result. A second
`Run` while one is going fails at once with `ErrorCode::InvalidRequest`.

```cpp
#include <UltraAI.h>
using namespace UltraAI;

void Background(Agent& agent) {
    std::future<AgentResult> pending = agent.RunAsync("Summarise the log.");
    // ... later, when the user presses Stop:
    agent.Cancel();
    AgentResult r = pending.get();       // r.stopReason == AgentStopReason::Cancelled
    (void)r;
}
```

## Conversations

An agent keeps its conversation between runs, so it is a session: the
second `Run` sees the first one's messages. `History()` returns the
messages so far, without the system prompt. `SetHistory` restores a saved
conversation and `ClearHistory` starts over. A run that fails or is
cancelled before the model has answered leaves the history as it was, so
the same prompt can be sent again. `AgentResult::messages` holds what the
run added.

## How a run ends

| `AgentStopReason` | Why |
|---|---|
| `Completed` | the model answered without asking for a tool (`Succeeded()` is true) |
| `MaxTurns` | `maxTurns` model calls were made |
| `MaxOutputTokens` | the reply was cut off at the model's token limit |
| `ContentFiltered` | the provider refused the reply |
| `Stopped` | `canUseTool` returned `Stop` |
| `Cancelled` | `Cancel()` was called (`error.code == ErrorCode::Cancelled`) |
| `Error` | the model call failed; `error` says how (rate limit, no key, ...) |

`AgentResult` also has the last reply `text`, the number of `turns` and
`toolCalls`, and `usage` summed over the run.

## Subagents

`MakeSubagentTool(subagent, name, description)` turns an agent into a tool
of another one. The tool takes `{"task": "..."}`, runs the subagent on that
task with an empty conversation, and returns its answer. A subagent that
did not finish returns an error result. Use it to give one job its own
system prompt, tools, model or token budget: a cheap local model for
lookups under a cloud model that plans.

```cpp
#include <UltraAI.h>
using namespace UltraAI;

void Team(Agent& lead, std::shared_ptr<ITextLLM> localModel) {
    AgentOptions researcherOptions;
    researcherOptions.systemPrompt = "You find facts in the user's notes and quote them.";
    researcherOptions.maxTurns = 6;
    auto researcher = std::make_shared<Agent>(localModel, researcherOptions);

    lead.AddTool(MakeSubagentTool(researcher, "researcher",
        "Finds facts in the user's notes. Give it one question at a time."));
}
```

The parent's `Cancel()` does not reach the subagent, so give the subagent a
small `maxTurns`.

## Limits of this version

- Tools run one after another, never in parallel.
- No built-in tools (files, shell, web): an application decides what its
  agent may touch and writes those tools itself.
- Providers' server-side tools (Anthropic web search, code execution) are
  not offered; the `anthropic` adapter skips their blocks.
- `llama-cpp` has no tool calls yet, so an agent over it can only answer
  in text.
- The model's thinking text is not reported.
