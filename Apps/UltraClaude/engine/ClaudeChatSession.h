// Apps/UltraClaude/engine/ClaudeChatSession.h
// One conversation with Claude through the Claude Code CLI. Each prompt runs
//
//   claude -p --input-format stream-json --output-format stream-json
//          --verbose --include-partial-messages
//          [--model <m>] [--permission-mode <p>] [--resume <session id>]
//
// as a child process, writes the prompt to its standard input as one
// stream-json user message, and turns its output into ClaudeStreamEvents.
// The session id the CLI reports is kept, so the next prompt resumes the
// same conversation; Reset() starts a new one.
//
// The CLI uses whatever account it is signed in with - a Pro or Max
// subscription after `claude login` - so UltraClaude needs no API key and
// never sees a credential: it only starts Anthropic's own client.
//
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ClaudeCliProcess.h"
#include "ClaudeStreamParser.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace UltraClaude {

struct ClaudeChatOptions {
    std::string executable = "claude";   // a name looked up on PATH, or a full path
    std::string model;                   // empty: the CLI's default
    std::string permissionMode;          // empty: the CLI's default
    std::string workingDirectory;        // empty: this program's current folder
};

class ClaudeChatSession {
public:
    // Runs on the process's reader thread.
    using EventCallback = std::function<void(const ClaudeStreamEvent&)>;

    ClaudeChatSession() = default;
    ~ClaudeChatSession();

    // Sends one prompt. Events arrive through onEvent, ending with exactly
    // one ProcessExited (or one ProcessError when the CLI could not be
    // started, in which case this also returns false with outError).
    bool SendPrompt(const std::string& prompt,
                    const ClaudeChatOptions& options,
                    EventCallback onEvent,
                    std::string& outError);

    // Ends the running prompt; its ProcessExited still follows.
    void Stop();
    // Ends the running prompt and waits until its reader thread is gone, so
    // no callback runs after this returns.
    void StopAndWait();
    bool IsBusy() const { return process_.IsRunning(); }

    // Forgets the conversation: the next prompt starts a new one.
    void Reset();
    std::string GetSessionId() const;

    // The command line SendPrompt runs (argv[0] first).
    static std::vector<std::string> BuildArguments(const ClaudeChatOptions& options,
                                                   const std::string& resumeSessionId);
    // The stream-json user message that carries the prompt on standard input.
    static std::string BuildPromptMessage(const std::string& prompt);

private:
    ClaudeCliProcess process_;
    ClaudeStreamParser parser_;   // used only on the reader thread while busy
    mutable std::mutex mutex_;
    std::string sessionId_;
};

} // namespace UltraClaude
