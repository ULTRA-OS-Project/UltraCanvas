// Apps/UltraCanvasStart/engine/StartAi.h
// Working with an AI assistant: is Claude Code installed, what to ask it
// first, and the checklist for the no-local-compiler workflow of
// Docs/GettingStarted-Cloud.md.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <string>
#include <vector>

namespace UltraCanvasStart {

struct AiStatus {
    bool claudeInstalled = false;
    std::string claudeVersion;
    std::string claudePath;
    bool gitInstalled = false;
};

AiStatus DetectAi(const SystemProfile& profile);

// How Claude Code is installed on this platform, as the user does it.
std::vector<std::string> ClaudeInstallInstructions(Platform platform);

// The first prompt to give the assistant for a new application, filled in
// with the application name and the way the framework was set up.
std::string FirstPrompt(const Choices& choices);

// The checklist of GettingStarted-Cloud.md, for a programmer without a
// compiler.
std::vector<std::string> CloudChecklist();

} // namespace UltraCanvasStart
