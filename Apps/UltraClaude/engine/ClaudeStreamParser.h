// Apps/UltraClaude/engine/ClaudeStreamParser.h
// Turns the Claude Code CLI's stream-json output - one JSON object per line,
// as written by `claude -p --output-format stream-json --verbose
// --include-partial-messages` - into the few events a chat window needs.
//
// The lines that matter:
//   {"type":"system","subtype":"init","session_id":...,"model":...}
//   {"type":"stream_event","event":{"type":"message_start","message":{"id":...}}}
//   {"type":"stream_event","event":{"type":"content_block_delta",
//        "delta":{"type":"text_delta","text":"..."}}}
//   {"type":"assistant","message":{"id":...,"content":[{"type":"text"...},
//        {"type":"tool_use","name":...,"input":{...}}]}}
//   {"type":"user","message":{"content":[{"type":"tool_result",...}]}}
//   {"type":"result","subtype":"success","result":...,"session_id":...,
//        "total_cost_usd":...,"duration_ms":...,"is_error":false}
// Everything else (rate-limit notices, status lines, thinking blocks) is
// ignored. The parser has no UI and no process in it, so the headless
// --print mode and the window share it.
//
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace UltraClaude {

enum class ClaudeEventKind {
    SessionStarted,   // sessionId, model
    TextDelta,        // text: the next piece of the reply, streamed
    AssistantText,    // text: a whole text block that was not streamed
    ToolUse,          // toolName, text: a one-line summary of the call
    ToolResult,       // isError, text: the first line of the result
    TurnFinished,     // isError, text: the final result or the error,
                      // costUsd, durationMs, numTurns, permissionDenials
    ProcessExited,    // exitCode, text: the CLI's standard error
    ProcessError      // text: why the CLI could not be run at all
};

struct ClaudeStreamEvent {
    ClaudeEventKind kind = ClaudeEventKind::TextDelta;
    std::string text;
    std::string sessionId;
    std::string model;
    std::string toolName;
    bool isError = false;
    double costUsd = 0.0;          // what the turn would cost on the API
    int64_t durationMs = 0;
    int64_t numTurns = 0;
    int64_t permissionDenials = 0; // tool calls refused by the permission mode
    // TurnFinished: text is what was already shown as the reply (the CLI
    // streams some errors as reply text and then repeats them in the result).
    bool textAlreadyShown = false;
    int exitCode = 0;
};

class ClaudeStreamParser {
public:
    // Parses one line of output. A line that is not JSON, or carries nothing
    // a chat shows, yields no events; an assistant message with several tool
    // calls yields one event per call.
    std::vector<ClaudeStreamEvent> ParseLine(const std::string& line);

    // Forgets which messages were streamed (a new turn starts afresh).
    void Reset();

private:
    // The message the text deltas belong to (from message_start), and every
    // message that received at least one delta: the complete text block the
    // CLI repeats in its "assistant" line is shown only for messages that
    // were not streamed, so nothing appears twice.
    std::string currentMessageId_;
    std::set<std::string> streamedMessages_;
    std::string turnText_;   // every piece of reply text shown in this turn
    bool separateNextText_ = false;   // a new message began after earlier text
};

// Cuts text to its first line and at most maxBytes bytes, never inside a
// UTF-8 sequence, and marks a cut with an ellipsis.
std::string FirstLineShortened(const std::string& text, size_t maxBytes);

} // namespace UltraClaude
