// Tests/UltraCanvasStart/test_guide.cpp
// The Markdown the pages show: one action per numbered step, links that
// open, the SDK named for real, and the plain-text reduction the report uses;
// and the words of the Tools step's rows and of what Install will do.
// Version: 0.2.0 - CheckDetail and InstallPreview instead of ChecksMarkdown
// Version: 0.1.1 - the assistant choice
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "StartAi.h"
#include "StartGuide.h"
#include "StartPlan.h"

#include <sstream>

using namespace UltraCanvasStart;

namespace {
// Every "N. ..." line of a guide, without the sub-bullets.
std::vector<std::string> NumberedSteps(const std::string& markdown) {
    std::vector<std::string> steps;
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        // The text after "N. ".
        if (line.size() > 3 && isdigit(static_cast<unsigned char>(line[0])) && line[1] == '.' && line[2] == ' ') {
            steps.push_back(line.substr(3));
        }
    }
    return steps;
}
} // namespace

TEST(PlatformGuide_names_the_real_sdk_archive_and_its_release) {
    const std::string guide = PlatformGuide(Platform::Windows, "0.9.235", "x86_64");
    REQUIRE(guide.find("[UltraCanvas-SDK-Windows-0.9.235-x86_64.zip](https://github.com/ULTRA-OS-Project/UltraCanvas/releases/download/v0.9.235/UltraCanvas-SDK-Windows-0.9.235-x86_64.zip)") != std::string::npos);
    REQUIRE(guide.find("[https://www.msys2.org](https://www.msys2.org)") != std::string::npos);
    REQUIRE(guide.find("`pacman -Syu`") != std::string::npos);
    REQUIRE(PlatformGuide(Platform::Linux, "0.9.235", "arm64").find("UltraCanvas-SDK-Linux-0.9.235-arm64.tar.xz") != std::string::npos);
    REQUIRE(PlatformGuide(Platform::MacOS, "0.9.235", "arm64").find("xcode-select --install") != std::string::npos);
}

TEST(PlatformGuide_steps_are_one_action_each) {
    for (auto platform : { Platform::Linux, Platform::MacOS, Platform::Windows }) {
        const auto steps = NumberedSteps(PlatformGuide(platform, "1.0.0", "x86_64"));
        REQUIRE(steps.size() >= 5);
        for (const auto& step : steps) {
            // One sentence: no sentence break inside the line, and no two
            // imperatives joined by " and " (a sub-bullet list carries the
            // alternatives instead).
            REQUIRE(step.find(". ") == std::string::npos);
            REQUIRE(step.find(" and open ") == std::string::npos);
            REQUIRE(step.find(" and run ") == std::string::npos);
        }
    }
}

TEST(CheckDetail_says_the_version_or_why_not_and_the_package) {
    CheckResult found;
    found.checked = true; found.present = true; found.version = "3.28.3"; found.packageName = "cmake";
    REQUIRE_EQ(CheckDetail(found), std::string("3.28.3"));
    CheckResult missing;
    missing.checked = true; missing.present = false; missing.detail = "pkg-config knows no cairo";
    missing.packageName = "libcairo2-dev";
    REQUIRE_EQ(CheckDetail(missing), std::string("pkg-config knows no cairo - install libcairo2-dev"));
    CheckResult old;
    old.checked = true; old.present = true; old.versionOk = false; old.version = "3.10";
    old.detail = "3.16 or newer is needed"; old.packageName = "cmake";
    REQUIRE_EQ(CheckDetail(old), std::string("3.10 - 3.16 or newer is needed - install cmake"));
    CheckResult unchecked;
    unchecked.checked = false; unchecked.detail = "no check possible"; unchecked.packageName = "libmupdf-dev";
    REQUIRE_EQ(CheckDetail(unchecked), std::string("no check possible - install libmupdf-dev"));
}

TEST(InstallPreview_says_what_Install_does) {
    SystemProfile profile;
    profile.platform = Platform::Linux;
    profile.architecture = "x86_64";
    profile.packageManager = PackageManager::Apt;
    profile.packageManagerPath = "/usr/bin/apt-get";
    Choices choices;
    choices.platform = Platform::Linux;
    std::vector<CheckResult> checks(2);
    checks[0].checked = true; checks[0].present = true; checks[0].packageName = "cmake";
    checks[1].checked = true; checks[1].present = false; checks[1].packageName = "libcairo2-dev";

    REQUIRE(InstallPreview(BuildPlan(profile, choices, {})).find("Check again") != std::string::npos);

    // The manager, how many, then the command on a line of its own.
    const std::string runs = InstallPreview(BuildPlan(profile, choices, checks));
    REQUIRE(runs.find("Install runs " + PackageManagerName(PackageManager::Apt) + " for the 1 missing") == 0);
    const size_t newline = runs.find('\n');
    REQUIRE(newline != std::string::npos);
    REQUIRE(runs.find("libcairo2-dev", newline) != std::string::npos);
    REQUIRE(runs.find(" cmake", newline) == std::string::npos);

    SystemProfile none = profile;
    none.packageManagerPath.clear();
    const std::string cannot = InstallPreview(BuildPlan(none, choices, checks));
    REQUIRE(cannot.find("No package manager was found") == 0);
    REQUIRE(cannot.find("the 1 missing yourself") != std::string::npos);
    REQUIRE(cannot.find('\n') == std::string::npos);

    checks[1].present = true;
    REQUIRE(InstallPreview(BuildPlan(profile, choices, checks)).find("nothing to do") != std::string::npos);
}

TEST(AiGuide_says_how_to_install_or_where_it_is) {
    AiStatus missing;
    const std::string install = AiGuide(missing, Platform::Windows, Assistant::ClaudeCode);
    REQUIRE(install.find("`irm https://claude.ai/install.ps1 | iex`") != std::string::npos);
    REQUIRE(install.find("https://github.com/apps/claude/installations/select_target") != std::string::npos);
    AiStatus found;
    found.assistants[Assistant::ClaudeCode] = { true, "2.0.1", "/usr/local/bin/claude" };
    REQUIRE(AiGuide(found, Platform::Linux, Assistant::ClaudeCode).find("installed (2.0.1) at `/usr/local/bin/claude`") != std::string::npos);
}

TEST(AiGuide_covers_every_assistant) {
    AiStatus none;
    const std::string codex = AiGuide(none, Platform::Linux, Assistant::Codex);
    REQUIRE(codex.find("`npm install -g @openai/codex`") != std::string::npos);
    REQUIRE(codex.find("reads `AGENTS.md`") != std::string::npos);
    const std::string copilot = AiGuide(none, Platform::MacOS, Assistant::Copilot);
    REQUIRE(copilot.find("`npm install -g @github/copilot`") != std::string::npos);
    const std::string gemini = AiGuide(none, Platform::Windows, Assistant::Gemini);
    REQUIRE(gemini.find("`npm install -g @google/gemini-cli`") != std::string::npos);
    REQUIRE(gemini.find("`GEMINI.md`") != std::string::npos);
    const std::string other = AiGuide(none, Platform::Linux, Assistant::Other);
    REQUIRE(other.find("llms-full.txt") != std::string::npos);
    REQUIRE(other.find("was not found") == std::string::npos);
    // Every install step is one action.
    for (auto assistant : { Assistant::ClaudeCode, Assistant::Codex, Assistant::Copilot, Assistant::Gemini }) {
        for (const auto& step : AssistantInstallInstructions(assistant, Platform::Linux)) {
            REQUIRE(step.find(". ") == std::string::npos);
        }
    }
}

TEST(Assistant_names_files_and_cli_words) {
    REQUIRE_EQ(AssistantInstructionFile(Assistant::ClaudeCode), std::string("CLAUDE.md"));
    REQUIRE_EQ(AssistantInstructionFile(Assistant::Codex), std::string("AGENTS.md"));
    REQUIRE_EQ(AssistantInstructionFile(Assistant::Gemini), std::string("GEMINI.md"));
    REQUIRE_EQ(AssistantInstructionFile(Assistant::Other), std::string("AGENTS.md"));
    Assistant a = Assistant::ClaudeCode;
    REQUIRE(AssistantFromName("copilot", a));
    REQUIRE(a == Assistant::Copilot);
    REQUIRE(!AssistantFromName("hal", a));
    REQUIRE_EQ(AssistantName(Assistant::Codex), std::string("Codex"));
}

TEST(PlainText_strips_the_markdown) {
    REQUIRE_EQ(PlainText("### Title\n\nRun `pacman -Syu` **first**, see [the guide](https://x.y/z) and [https://a.b](https://a.b)."),
               std::string("Title\n\nRun pacman -Syu first, see the guide (https://x.y/z) and https://a.b."));
}

TEST(Linkify_wraps_bare_addresses_only) {
    REQUIRE_EQ(Linkify("Install it from https://www.msys2.org, then open the shell."),
               std::string("Install it from [https://www.msys2.org](https://www.msys2.org), then open the shell."));
    REQUIRE_EQ(Linkify("Already [a link](https://a.b/c)."), std::string("Already [a link](https://a.b/c)."));
}

TEST(PlatformNotes_are_one_action_each_and_the_report_is_plain) {
    SystemProfile p;
    p.platform = Platform::Windows;
    p.architecture = "x86_64";
    const auto notes = PlatformNotes(p, Platform::Windows);
    REQUIRE(notes.size() >= 4);
    for (const auto& note : notes) {
        REQUIRE(note.find(". ") == std::string::npos);
        REQUIRE(note.find("; ") == std::string::npos);
    }
    Choices choices;
    choices.platform = Platform::Windows;
    const std::string report = RenderReport(BuildPlan(p, choices, {}));
    REQUIRE(report.find('`') == std::string::npos);
    REQUIRE(report.find("**") == std::string::npos);
    REQUIRE(report.find("https://www.msys2.org") != std::string::npos);
}
