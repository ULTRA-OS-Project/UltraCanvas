// Apps/UltraClaude/engine/ClaudeStreamParser.cpp
// See ClaudeStreamParser.h.
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS

#include "ClaudeStreamParser.h"

#include "DataFormats/UltraCanvasJSON.h"

using namespace UltraCanvas;

namespace UltraClaude {

namespace {
    constexpr size_t kToolSummaryBytes = 160;
    constexpr size_t kToolResultBytes  = 200;

    // The input field that says what a tool call does, per tool; the first
    // one present wins. Tools not listed show their whole input, shortened.
    const char* const kSummaryFields[] = {
        "command", "file_path", "path", "pattern", "url", "query",
        "description", "prompt", "skill",
    };

    std::string SummarizeToolInput(const JSONValue& input) {
        if (input.IsObject()) {
            for (const char* field : kSummaryFields) {
                const JSONValue& value = input[field];
                if (value.IsString() && !value.GetString().empty())
                    return FirstLineShortened(value.GetString(), kToolSummaryBytes);
            }
        }
        if (input.IsNull()) return std::string();
        return FirstLineShortened(JSON::Serialize(input), kToolSummaryBytes);
    }

    // A tool result's content is a string, or a list of blocks of which the
    // text blocks are joined.
    std::string ToolResultText(const JSONValue& content) {
        if (content.IsString()) return content.GetString();
        std::string text;
        if (content.IsArray()) {
            for (size_t i = 0; i < content.GetSize(); ++i) {
                const JSONValue& block = content[i];
                if (block["type"].GetString() != "text") continue;
                if (!text.empty()) text += '\n';
                text += block["text"].GetString();
            }
        }
        return text;
    }
} // namespace

std::string FirstLineShortened(const std::string& text, size_t maxBytes) {
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return std::string();
    size_t end = text.find('\n', start);
    bool cut = end != std::string::npos && text.find_first_not_of(" \t\r\n", end) != std::string::npos;
    if (end == std::string::npos) end = text.size();
    while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ')) --end;
    if (end - start > maxBytes) {
        end = start + maxBytes;
        // Back up to the start of a UTF-8 sequence: 10xxxxxx bytes continue one.
        while (end > start && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        cut = true;
    }
    std::string out = text.substr(start, end - start);
    if (cut) out += "\xE2\x80\xA6";   // U+2026 HORIZONTAL ELLIPSIS
    return out;
}

void ClaudeStreamParser::Reset() {
    currentMessageId_.clear();
    streamedMessages_.clear();
    turnText_.clear();
    separateNextText_ = false;
}

std::vector<ClaudeStreamEvent> ClaudeStreamParser::ParseLine(const std::string& line) {
    std::vector<ClaudeStreamEvent> events;
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) return events;

    JSONParseResult parsed;
    JSONValue doc = JSON::Parse(line, &parsed);
    if (!parsed.success || !doc.IsObject()) return events;

    const std::string type = doc["type"].GetString();

    if (type == "system") {
        if (doc["subtype"].GetString() == "init") {
            ClaudeStreamEvent e;
            e.kind = ClaudeEventKind::SessionStarted;
            e.sessionId = doc["session_id"].GetString();
            e.model = doc["model"].GetString();
            events.push_back(std::move(e));
        }
        return events;
    }

    if (type == "stream_event") {
        const JSONValue& ev = doc["event"];
        const std::string evType = ev["type"].GetString();
        if (evType == "message_start") {
            currentMessageId_ = ev["message"]["id"].GetString();
            // A later step's text starts a new paragraph instead of running
            // on from the last word of the step before.
            separateNextText_ = !turnText_.empty();
        } else if (evType == "content_block_delta") {
            const JSONValue& delta = ev["delta"];
            if (delta["type"].GetString() == "text_delta") {
                const std::string& text = delta["text"].GetString();
                if (!text.empty()) {
                    streamedMessages_.insert(currentMessageId_);
                    ClaudeStreamEvent e;
                    e.kind = ClaudeEventKind::TextDelta;
                    e.text = separateNextText_ ? "\n\n" + text : text;
                    separateNextText_ = false;
                    turnText_ += text;
                    events.push_back(std::move(e));
                }
            }
        }
        return events;
    }

    if (type == "assistant") {
        const JSONValue& message = doc["message"];
        const bool streamed = streamedMessages_.count(message["id"].GetString()) != 0;
        const JSONValue& content = message["content"];
        for (size_t i = 0; i < content.GetSize(); ++i) {
            const JSONValue& block = content[i];
            const std::string blockType = block["type"].GetString();
            if (blockType == "text" && !streamed) {
                ClaudeStreamEvent e;
                e.kind = ClaudeEventKind::AssistantText;
                e.text = block["text"].GetString();
                turnText_ += e.text;
                if (!e.text.empty()) events.push_back(std::move(e));
            } else if (blockType == "tool_use") {
                ClaudeStreamEvent e;
                e.kind = ClaudeEventKind::ToolUse;
                e.toolName = block["name"].GetString("tool");
                e.text = SummarizeToolInput(block["input"]);
                events.push_back(std::move(e));
            }
        }
        return events;
    }

    if (type == "user") {
        const JSONValue& content = doc["message"]["content"];
        for (size_t i = 0; i < content.GetSize(); ++i) {
            const JSONValue& block = content[i];
            if (block["type"].GetString() != "tool_result") continue;
            ClaudeStreamEvent e;
            e.kind = ClaudeEventKind::ToolResult;
            e.isError = block["is_error"].GetBoolean(false);
            e.text = FirstLineShortened(ToolResultText(block["content"]), kToolResultBytes);
            events.push_back(std::move(e));
        }
        return events;
    }

    if (type == "result") {
        ClaudeStreamEvent e;
        e.kind = ClaudeEventKind::TurnFinished;
        e.sessionId = doc["session_id"].GetString();
        e.isError = doc["is_error"].GetBoolean(false) ||
                    doc["subtype"].GetString("success") != "success";
        e.text = doc["result"].GetString();
        if (e.text.empty() && e.isError) {
            // The error subtypes (error_max_turns, error_during_execution)
            // carry their reasons in "errors", not in "result".
            const JSONValue& errors = doc["errors"];
            for (size_t i = 0; i < errors.GetSize(); ++i) {
                if (!e.text.empty()) e.text += '\n';
                e.text += errors[i].GetString();
            }
            if (e.text.empty()) e.text = doc["subtype"].GetString("error");
        }
        e.textAlreadyShown = !e.text.empty() && turnText_.find(e.text) != std::string::npos;
        e.costUsd = doc["total_cost_usd"].GetNumber(0.0);
        e.durationMs = doc["duration_ms"].GetInteger(0);
        e.numTurns = doc["num_turns"].GetInteger(0);
        e.permissionDenials = static_cast<int64_t>(doc["permission_denials"].GetSize());
        events.push_back(std::move(e));
        return events;
    }

    return events;
}

} // namespace UltraClaude
