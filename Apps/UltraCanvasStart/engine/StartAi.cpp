// Apps/UltraCanvasStart/engine/StartAi.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartAi.h"

#include "StartChecks.h"
#include "StartProject.h"

namespace UltraCanvasStart {

AiStatus DetectAi(const SystemProfile& profile) {
    AiStatus status;
    std::vector<std::string> directories = CheckSearchDirectories(profile);
    if (!profile.homeDirectory.empty()) {
        directories.push_back(profile.homeDirectory + "/.local/bin");
        directories.push_back(profile.homeDirectory + "/.claude/bin");
        directories.push_back(profile.homeDirectory + "/AppData/Local/Programs/claude");
    }
    const CheckResult claude = CheckTool("claude", "", directories);
    status.claudeInstalled = claude.present;
    status.claudeVersion = claude.version;
    if (claude.present) status.claudePath = claude.detail;
    status.gitInstalled = CheckTool("git", "", directories).present;
    return status;
}

std::vector<std::string> ClaudeInstallInstructions(Platform platform) {
    switch (platform) {
        case Platform::MacOS:
            return { "In Terminal: curl -fsSL https://claude.ai/install.sh | bash",
                     "or, with Node.js: npm install -g @anthropic-ai/claude-code",
                     "Then: claude   (it signs you in on first start)" };
        case Platform::Windows:
            return { "In PowerShell: irm https://claude.ai/install.ps1 | iex",
                     "or, with Node.js: npm install -g @anthropic-ai/claude-code",
                     "Then open a terminal and run: claude" };
        case Platform::Linux:
        default:
            return { "In a terminal: curl -fsSL https://claude.ai/install.sh | bash",
                     "Then: claude   (it signs you in on first start)" };
    }
}

std::string FirstPrompt(const Choices& choices) {
    const std::string app = IdentifierFrom(choices.appName);
    std::string text;
    text += "Read CLAUDE.md, then the framework's AGENTS.md and Docs/GettingStarted.md";
    if (choices.useSdk) {
        text += " (the framework is the prebuilt UltraCanvas SDK; its README is Docs/UltraCanvasSDK.md)";
    } else {
        text += " (the framework checkout is in ../UltraCanvas)";
    }
    text += ".\n\n";
    text += "This folder is " + app + ", a new UltraCanvas application with one window. ";
    text += "Turn it into a skeleton the way Docs/GettingStarted.md step 3 describes: ";
    text += "a headless core library plus the GUI target, --version from a CHANGELOG.md, ";
    text += "the version in the window title, an UltraCanvasToolbar and an UltraCanvasSplitPane. ";
    text += "Use only elements from Docs/UltraCanvas/UltraCanvasUIElements.md and read each ";
    text += "element's page before using it. Build with `cmake --preset default && cmake --build --preset default` ";
    text += "and fix every warning before you report back.";
    if (choices.cloudOnly) {
        text += "\n\nI have no compiler here: commit, push and open a draft pull request so GitHub ";
        text += "Actions builds it, then watch the PR and fix what CI reports.";
    }
    return text;
}

std::vector<std::string> CloudChecklist() {
    return {
        "Install the Claude GitHub App on the repository: https://github.com/apps/claude/installations/select_target",
        "Connect GitHub in the Claude app's settings, under Connectors",
        "Start a Claude Code session on the repository; it reads CLAUDE.md and AGENTS.md itself",
        "First session: skeleton, changelog entry, checks run, pushed, draft pull request open",
        "Ask the assistant to watch the PR and fix CI; a branch with no PR builds nothing",
        "Download the workflow artifact for your platform to run the application",
        "Read the ## Delivery block at the end of every reply; nothing left uncommitted",
        "Merge with the PR title as the commit message, only after the session said it was done",
    };
}

} // namespace UltraCanvasStart
