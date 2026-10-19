// Apps/UltraCanvasStart/engine/StartAi.cpp
// Version: 0.2.0 - every assistant: detection, install and guidance notes; the first prompt names its file
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartAi.h"

#include "StartChecks.h"
#include "StartProject.h"

namespace UltraCanvasStart {

const AssistantStatus* AiStatus::Status(Assistant assistant) const {
    const auto found = assistants.find(assistant);
    return found == assistants.end() ? nullptr : &found->second;
}

AiStatus DetectAi(const SystemProfile& profile) {
    AiStatus status;
    std::vector<std::string> directories = CheckSearchDirectories(profile);
    if (!profile.homeDirectory.empty()) {
        directories.push_back(profile.homeDirectory + "/.local/bin");
        directories.push_back(profile.homeDirectory + "/.claude/bin");
        directories.push_back(profile.homeDirectory + "/AppData/Local/Programs/claude");
        // npm's global bin directories, where codex, copilot and gemini land.
        directories.push_back(profile.homeDirectory + "/.npm-global/bin");
        directories.push_back(profile.homeDirectory + "/AppData/Roaming/npm");
    }
    const struct { Assistant assistant; const char* tool; } tools[] = {
        { Assistant::ClaudeCode, "claude" }, { Assistant::Codex, "codex" },
        { Assistant::Copilot, "copilot" },   { Assistant::Gemini, "gemini" },
    };
    for (const auto& t : tools) {
        const CheckResult check = CheckTool(t.tool, "", directories);
        AssistantStatus found;
        found.installed = check.present;
        found.version = check.version;
        if (check.present) found.path = check.detail;
        status.assistants[t.assistant] = found;
    }
    const AssistantStatus& claude = status.assistants[Assistant::ClaudeCode];
    status.claudeInstalled = claude.installed;
    status.claudeVersion = claude.version;
    status.claudePath = claude.path;
    status.gitInstalled = CheckTool("git", "", directories).present;
    return status;
}

std::vector<std::string> AssistantInstallInstructions(Assistant assistant, Platform platform) {
    const bool windows = platform == Platform::Windows;
    const std::string terminal = windows ? "Open PowerShell." : (platform == Platform::MacOS ? "Open Terminal." : "Open a terminal.");
    const std::string node = "Node.js 18 or newer must be installed ([https://nodejs.org](https://nodejs.org)).";
    switch (assistant) {
        case Assistant::ClaudeCode:
            return { terminal,
                     windows ? "Run `irm https://claude.ai/install.ps1 | iex` (or, with Node.js, `npm install -g @anthropic-ai/claude-code`)."
                             : "Run `curl -fsSL https://claude.ai/install.sh | bash` (or, with Node.js, `npm install -g @anthropic-ai/claude-code`).",
                     "Run `claude`; it signs you in with your Anthropic account on first start." };
        case Assistant::Codex:
            return { node, terminal,
                     "Run `npm install -g @openai/codex`" + std::string(platform == Platform::MacOS ? " (or `brew install codex`)." : "."),
                     "Run `codex`; it signs you in with your ChatGPT account on first start." };
        case Assistant::Copilot:
            return { node, terminal,
                     "Run `npm install -g @github/copilot`.",
                     "Run `copilot`; it signs you in with your GitHub account, which needs a Copilot subscription." };
        case Assistant::Gemini:
            return { node, terminal,
                     "Run `npm install -g @google/gemini-cli`.",
                     "Run `gemini`; it signs you in with your Google account on first start." };
        default:
            return {};
    }
}

std::vector<std::string> AssistantGuidanceNotes(Assistant assistant) {
    const std::string repo = "https://github.com/ULTRA-OS-Project/UltraCanvas";
    switch (assistant) {
        case Assistant::ClaudeCode:
            return { "Claude Code loads `CLAUDE.md` from the project root by itself; the one this application writes sends it to the framework's `AGENTS.md` and the element catalogue.",
                     "The framework's `.claude/settings.json` carries the hooks and permissions that keep a session honest; leave it in place." };
        case Assistant::Codex:
            return { "Codex reads `AGENTS.md` from the project root by itself; the one this application writes sends it to the framework's `AGENTS.md` and the element catalogue.",
                     "Give it the framework checkout or the SDK's `Docs/` folder as well, so it can open a widget's page before using it." };
        case Assistant::Copilot:
            return { "Copilot reads `AGENTS.md` from the project root (and `.github/copilot-instructions.md`, when present); the one this application writes sends it to the framework's `AGENTS.md`.",
                     "The coding agent on GitHub reads the same file, so the no-local-compiler workflow of [Docs/GettingStarted-Cloud.md](" + repo + "/blob/main/Docs/GettingStarted-Cloud.md) works with it too." };
        case Assistant::Gemini:
            return { "Gemini CLI reads `GEMINI.md` from the project root; the one this application writes sends it to the framework's `AGENTS.md` and the element catalogue.",
                     "To read `AGENTS.md` directly instead, set `context.fileName` to `AGENTS.md` in Gemini's `settings.json`." };
        default:
            return { "Paste the framework's `AGENTS.md` into the assistant's project instructions or system prompt; most tools (Cursor, Windsurf) also pick it up by name from the project root, where this application writes one.",
                     "Upload [llms-full.txt](" + repo + "/blob/main/llms-full.txt), the whole docs corpus in one file, to a Claude.ai Project, a custom GPT or a Gemini Gem, so the chat can answer API questions without the repository open.",
                     "An agent with the Context7 MCP server resolves `ultracanvas` to the real docs (`context7.json` in the repository)." };
    }
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
    text += "Read " + AssistantInstructionFile(choices.assistant) +
            ", then the framework's AGENTS.md and Docs/GettingStarted.md";
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
