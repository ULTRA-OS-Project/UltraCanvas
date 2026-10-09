// Apps/UltraClaude/engine/ClaudeChatSession.cpp
// See ClaudeChatSession.h.
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS

#include "ClaudeChatSession.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <memory>
#include <utility>

using namespace UltraCanvas;

namespace UltraClaude {

ClaudeChatSession::~ClaudeChatSession() {
    process_.StopAndWait();
}

std::vector<std::string> ClaudeChatSession::BuildArguments(const ClaudeChatOptions& options,
                                                           const std::string& resumeSessionId) {
    std::vector<std::string> argv = {
        options.executable.empty() ? std::string("claude") : options.executable,
        "-p",
        "--input-format", "stream-json",
        "--output-format", "stream-json",
        "--verbose",                    // stream-json output requires it
        "--include-partial-messages",   // the reply as it is written, not at the end
    };
    if (!options.model.empty()) { argv.push_back("--model"); argv.push_back(options.model); }
    if (!options.permissionMode.empty()) {
        argv.push_back("--permission-mode");
        argv.push_back(options.permissionMode);
    }
    if (!resumeSessionId.empty()) { argv.push_back("--resume"); argv.push_back(resumeSessionId); }
    return argv;
}

std::string ClaudeChatSession::BuildPromptMessage(const std::string& prompt) {
    JSONValue text = JSONValue::MakeObject();
    text.Set("type", "text");
    text.Set("text", prompt);
    JSONValue content = JSONValue::MakeArray();
    content.Append(text);
    JSONValue message = JSONValue::MakeObject();
    message.Set("role", "user");
    message.Set("content", content);
    JSONValue envelope = JSONValue::MakeObject();
    envelope.Set("type", "user");
    envelope.Set("message", message);
    return JSON::Serialize(envelope) + "\n";
}

bool ClaudeChatSession::SendPrompt(const std::string& prompt,
                                   const ClaudeChatOptions& options,
                                   EventCallback onEvent,
                                   std::string& outError) {
    if (process_.IsRunning()) { outError = "Claude is still answering."; return false; }
    parser_.Reset();

    // Shared by the two callbacks: whether the CLI reported the end of the
    // turn itself, so an exit without one can be reported as a failure.
    auto sawResult = std::make_shared<bool>(false);

    auto onLine = [this, onEvent, sawResult](const std::string& line) {
        for (ClaudeStreamEvent& e : parser_.ParseLine(line)) {
            if (!e.sessionId.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                sessionId_ = e.sessionId;
            }
            if (e.kind == ClaudeEventKind::TurnFinished) *sawResult = true;
            if (onEvent) onEvent(e);
        }
    };
    auto onExit = [onEvent, sawResult](int exitCode, const std::string& standardError) {
        ClaudeStreamEvent e;
        e.kind = ClaudeEventKind::ProcessExited;
        e.exitCode = exitCode;
        e.text = standardError;
        e.isError = !*sawResult;
        if (onEvent) onEvent(e);
    };

    const std::vector<std::string> argv = BuildArguments(options, GetSessionId());
    if (!process_.Start(argv, options.workingDirectory, BuildPromptMessage(prompt),
                        std::move(onLine), std::move(onExit), outError)) {
        ClaudeStreamEvent e;
        e.kind = ClaudeEventKind::ProcessError;
        e.text = outError;
        if (onEvent) onEvent(e);
        return false;
    }
    return true;
}

void ClaudeChatSession::Stop() {
    process_.Stop();
}

void ClaudeChatSession::StopAndWait() {
    process_.StopAndWait();
}

void ClaudeChatSession::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionId_.clear();
}

void ClaudeChatSession::SetSessionId(const std::string& sessionId) {
    if (process_.IsRunning()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    sessionId_ = sessionId;
}

std::string ClaudeChatSession::GetSessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionId_;
}

} // namespace UltraClaude
