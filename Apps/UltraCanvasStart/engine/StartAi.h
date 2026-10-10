// Apps/UltraCanvasStart/engine/StartAi.h
// Working with an AI assistant: is Claude Code installed, what to ask it
// first, and the checklist for the no-local-compiler workflow of
// Docs/GettingStarted-Cloud.md.
// Version: 0.2.0 - every assistant: detection, install and guidance notes
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <map>
#include <string>
#include <vector>

namespace UltraCanvasStart {

// One assistant's command-line tool on this machine.
struct AssistantStatus {
    bool installed = false;
    std::string version;
    std::string path;
};

struct AiStatus {
    // Claude Code, kept by name: the System page and the report say it.
    bool claudeInstalled = false;
    std::string claudeVersion;
    std::string claudePath;
    bool gitInstalled = false;
    // Every assistant with a command-line tool (claude, codex, copilot,
    // gemini); Assistant::Other has none and is absent.
    std::map<Assistant, AssistantStatus> assistants;

    const AssistantStatus* Status(Assistant assistant) const;
};

AiStatus DetectAi(const SystemProfile& profile);

// How Claude Code is installed on this platform, as the user does it.
std::vector<std::string> ClaudeInstallInstructions(Platform platform);

// How `assistant` is installed and signed in on this platform, one action
// per line, in Markdown (commands in backticks, addresses linked). Empty
// for Assistant::Other, which is not a program.
std::vector<std::string> AssistantInstallInstructions(Assistant assistant, Platform platform);

// How `assistant` picks up the repository's guidance (CLAUDE.md, AGENTS.md,
// GEMINI.md, pasted instructions), one fact per line, in Markdown.
std::vector<std::string> AssistantGuidanceNotes(Assistant assistant);

// The first prompt to give the assistant for a new application, filled in
// with the application name and the way the framework was set up.
std::string FirstPrompt(const Choices& choices);

// The checklist of GettingStarted-Cloud.md, for a programmer without a
// compiler.
std::vector<std::string> CloudChecklist();

} // namespace UltraCanvasStart
