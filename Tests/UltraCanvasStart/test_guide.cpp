// Tests/UltraCanvasStart/test_guide.cpp
// The Markdown the pages show: one action per numbered step, links that
// open, the SDK named for real, and the plain-text reduction the report uses.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

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

TEST(ChecksMarkdown_marks_each_result) {
    std::vector<CheckResult> checks(3);
    checks[0].title = "CMake"; checks[0].group = DependencyGroup::Toolchain;
    checks[0].checked = true; checks[0].present = true; checks[0].version = "3.28.3";
    checks[1].title = "Cairo"; checks[1].group = DependencyGroup::Core;
    checks[1].checked = true; checks[1].present = false; checks[1].detail = "pkg-config knows no cairo";
    checks[1].packageName = "libcairo2-dev";
    checks[2].title = "MuPDF"; checks[2].group = DependencyGroup::Pdf;
    checks[2].checked = false; checks[2].detail = "no check possible"; checks[2].packageName = "libmupdf-dev";
    const std::string md = ChecksMarkdown(checks);
    REQUIRE(md.find("### Toolchain\n\xE2\x9C\x93 **CMake** 3.28.3\n") != std::string::npos);
    REQUIRE(md.find("\xE2\x9C\x97 **Cairo** - pkg-config knows no cairo - install `libcairo2-dev`") != std::string::npos);
    REQUIRE(md.find("? **MuPDF** - no check possible") != std::string::npos);
    REQUIRE(ChecksMarkdown({}).find("Check again") != std::string::npos);
}

TEST(AiGuide_says_how_to_install_or_where_it_is) {
    AiStatus missing;
    const std::string install = AiGuide(missing, Platform::Windows);
    REQUIRE(install.find("`irm https://claude.ai/install.ps1 | iex`") != std::string::npos);
    REQUIRE(install.find("https://github.com/apps/claude/installations/select_target") != std::string::npos);
    AiStatus found;
    found.claudeInstalled = true; found.claudeVersion = "2.0.1"; found.claudePath = "/usr/local/bin/claude";
    REQUIRE(AiGuide(found, Platform::Linux).find("installed (2.0.1) at `/usr/local/bin/claude`") != std::string::npos);
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
