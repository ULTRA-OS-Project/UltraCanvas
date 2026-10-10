// Apps/UltraCanvasStart/engine/StartGuide.cpp
// Version: 0.1.1 - AiGuide per assistant
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartGuide.h"

#include "StartSdk.h"

#include <map>

namespace UltraCanvasStart {

namespace {

const char* kRepository = "https://github.com/ULTRA-OS-Project/UltraCanvas";

std::string Link(const std::string& text, const std::string& url) {
    return "[" + text + "](" + url + ")";
}

std::string Code(const std::string& text) {
    return "`" + text + "`";
}

// The SDK step every platform shares: the archive by its real name, linked
// to the release that carries it, and the way the application fetches it.
std::string SdkLine(Platform platform, const std::string& version, const std::string& architecture,
                    const std::string& remark) {
    const std::string archive = SdkArchiveName(platform, version, architecture);
    return "unpack the prebuilt SDK " + Link(archive, SdkReleaseAssetUrl(platform, version, architecture)) +
           (remark.empty() ? "" : " (" + remark + ")") +
           " - the Project page's *Download...* fetches and unpacks it - or";
}

} // namespace

std::string PlatformGuide(Platform platform, const std::string& version,
                          const std::string& architecture) {
    std::string out;
    switch (platform) {
        case Platform::Linux:
            out += "### Linux\n\n";
            out += "1. Install the development packages with your distribution's package manager "
                   "(" + Code("apt") + ", " + Code("dnf") + ", " + Code("pacman") + " or " + Code("zypper") +
                   "); the Install page lists the exact names and installs them for you.\n";
            out += "2. Make sure a C++20 compiler is installed: " + Code("clang") + " 14 or newer, or " +
                   Code("gcc") + " 11 or newer.\n";
            out += "3. Get the framework, one of:\n";
            out += "   - " + SdkLine(platform, version, architecture, "") + "\n";
            out += "   - clone " + Link("the repository", kRepository) + " and build it with " + Code("cmake") + ".\n";
            out += "4. Run an application with " + Code("LD_LIBRARY_PATH") + " pointing at the SDK's " +
                   Code("lib/") + ".\n";
            out += "5. For a standalone package, let " + Code("package-linux.sh") +
                   " bundle the libraries next to the executable.\n";
            break;
        case Platform::MacOS:
            out += "### macOS\n\n";
            out += "1. Install the Xcode command line tools: " + Code("xcode-select --install") + ".\n";
            out += "2. Install Homebrew from " + Link("https://brew.sh", "https://brew.sh") + ".\n";
            out += "3. Install the libraries: " +
                   Code("brew install cmake pkg-config cairo pango harfbuzz vips glib freetype tinyxml2") +
                   " (the Install page lists the optional ones and installs them for you).\n";
            out += "4. Get the framework, one of:\n";
            out += "   - " + SdkLine(platform, version, architecture,
                                     Code("arm64") + " for Apple silicon, " + Code("x86_64") + " for Intel") + "\n";
            out += "   - clone " + Link("the repository", kRepository) + " and build it with " + Code("cmake") + ".\n";
            out += "5. For an application bundle, run " + Code("package-macos.sh") + ".\n";
            break;
        case Platform::Windows:
            out += "### Windows\n\n";
            out += "1. Install MSYS2 from " + Link("https://www.msys2.org", "https://www.msys2.org") + ".\n";
            out += "2. Open the **MSYS2 CLANG64** shell (**CLANGARM64** on an ARM machine).\n";
            out += "3. Update MSYS2: " + Code("pacman -Syu") + " (close and reopen the shell if it asks you to).\n";
            out += "4. Install the packages: " + Code("pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake") +
                   " and the rest of the list (the Install page lists them all, and *Install what is missing* runs the command).\n";
            out += "5. Get the framework, one of:\n";
            out += "   - " + SdkLine(platform, version, architecture,
                                     "the core is " + Code("bin/libUltraCanvas.dll")) + "\n";
            out += "   - clone " + Link("the repository", kRepository) + " and run " + Code("build-win.cmd") + ".\n";
            out += "6. For a standalone zip with every DLL next to the exe, run " + Code("package-win.sh") + ".\n";
            break;
        default:
            out += "Choose the platform the instructions are for.\n";
            break;
    }
    out += "\nThe full text is step 1 of " +
           Link("Docs/GettingStarted.md", std::string(kRepository) + "/blob/main/Docs/GettingStarted.md") + ".\n";
    return out;
}

std::string ChecksMarkdown(const std::vector<CheckResult>& checks) {
    if (checks.empty()) {
        return "Press **Check again** to look for the tools and libraries the chosen features need.\n";
    }
    std::string out;
    bool first = true;
    DependencyGroup current = DependencyGroup::Toolchain;
    for (const auto& c : checks) {
        if (first || c.group != current) {
            if (!first) out += "\n";
            out += "### " + DependencyGroupTitle(c.group) + "\n";
            current = c.group;
            first = false;
        }
        const bool ok = c.present && c.versionOk;
        // U+2713 check mark, U+2717 ballot x, and a question mark for a
        // dependency with no check of its own. The mark is the bullet: the
        // lines are not a Markdown list.
        const char* mark = !c.checked ? "?" : (ok ? "\xE2\x9C\x93" : "\xE2\x9C\x97");
        std::string line = std::string(mark) + " **" + c.title + "**";
        if (!c.version.empty()) line += " " + c.version;
        if (!ok || !c.checked) line += " - " + c.detail;
        if (!c.packageName.empty() && !ok) line += " - install " + Code(c.packageName);
        out += line + "\n";
    }
    return out;
}

std::string AiGuide(const AiStatus& ai, Platform platform, Assistant assistant) {
    const std::string name = AssistantName(assistant);
    std::string out = "### " + (assistant == Assistant::Other ? std::string("Another assistant") : name) + "\n\n";
    if (assistant == Assistant::Other) {
        out += "Cursor, Windsurf, ChatGPT, a Claude.ai Project, a Gemini Gem: any assistant that can read "
               "a file works, because the repository carries its own instructions.\n";
    } else if (const AssistantStatus* status = ai.Status(assistant); status && status->installed) {
        out += name + " is installed" + (status->version.empty() ? "" : " (" + status->version + ")") +
               " at " + Code(status->path) + ".\n";
    } else {
        out += name + " was not found on this computer. To install it:\n\n";
        int n = 0;
        for (const auto& step : AssistantInstallInstructions(assistant, platform)) {
            out += std::to_string(++n) + ". " + step + "\n";
        }
    }
    out += "\n### How it reads the repository's guidance\n\n";
    for (const auto& note : AssistantGuidanceNotes(assistant)) out += "- " + note + "\n";

    out += "\n### How an assistant works on an UltraCanvas application\n\n";
    out += "1. It reads " + Code(AssistantInstructionFile(assistant)) + " in the project, which points at the framework's " +
           Code("AGENTS.md") + " and the element catalogue.\n";
    out += "2. Ask for **one bounded change per session**; name the elements and the docs to read.\n";
    out += "3. It builds and runs the check scripts before it reports back.\n";
    out += "4. Read the " + Code("## Delivery") + " block at the end of every reply.\n";
    out += "5. Every change gets a changelog entry; the version comes from the changelog.\n";

    out += "\n### Without a compiler on this machine\n\n";
    out += "The pull request is the compiler (" +
           Link("Docs/GettingStarted-Cloud.md", std::string(kRepository) + "/blob/main/Docs/GettingStarted-Cloud.md") +
           ")";
    if (assistant == Assistant::ClaudeCode) {
        out += ":\n\n";
        out += "1. Install the Claude GitHub App on the repository: " +
               Link("github.com/apps/claude", "https://github.com/apps/claude/installations/select_target") + ".\n";
        out += "2. Connect GitHub in the Claude app's settings, under *Connectors*.\n";
        out += "3. Start a Claude Code session on the repository; it reads " + Code("CLAUDE.md") + " and " +
               Code("AGENTS.md") + " itself.\n";
        out += "4. First session: the skeleton, a changelog entry, the checks run, pushed, a draft pull request open.\n";
        out += "5. Ask the assistant to watch the pull request and fix CI; a branch with no pull request builds nothing.\n";
        out += "6. Download the workflow artifact for your platform to run the application.\n";
        out += "7. Read the " + Code("## Delivery") + " block at the end of every reply; nothing stays uncommitted.\n";
        out += "8. Merge with the pull request title as the commit message, only after the session said it was done.\n";
    } else {
        out += ", written for Claude Code. The shape is the same with " + name +
               (assistant == Assistant::Copilot ? "'s coding agent on GitHub"
                : assistant == Assistant::Codex ? "'s cloud tasks"
                : assistant == Assistant::Gemini ? " and GitHub Actions" : " and GitHub Actions") +
               ":\n\n";
        out += "1. The assistant works on a branch and opens a draft pull request; CI builds it.\n";
        out += "2. Ask it to watch the pull request and fix what CI reports; a branch with no pull request builds nothing.\n";
        out += "3. Download the workflow artifact for your platform to run the application.\n";
        out += "4. Merge with the pull request title as the commit message, only after the work is done.\n";
    }
    return out;
}

std::string PlainText(const std::string& markdown) {
    std::string out;
    out.reserve(markdown.size());
    size_t i = 0;
    while (i < markdown.size()) {
        const char ch = markdown[i];
        // A heading marker at the start of a line.
        if (ch == '#' && (i == 0 || markdown[i - 1] == '\n')) {
            while (i < markdown.size() && markdown[i] == '#') ++i;
            if (i < markdown.size() && markdown[i] == ' ') ++i;
            continue;
        }
        if (ch == '`') { ++i; continue; }
        if (ch == '*' && i + 1 < markdown.size() && markdown[i + 1] == '*') { i += 2; continue; }
        // [text](url) -> "text (url)"; the link's text alone when both are
        // the same address.
        if (ch == '[') {
            const size_t close = markdown.find("](", i);
            const size_t end = close == std::string::npos ? std::string::npos : markdown.find(')', close);
            if (close != std::string::npos && end != std::string::npos &&
                markdown.find('\n', i) > close) {
                const std::string text = markdown.substr(i + 1, close - i - 1);
                const std::string url = markdown.substr(close + 2, end - close - 2);
                out += text == url ? url : text + " (" + url + ")";
                i = end + 1;
                continue;
            }
        }
        out += ch;
        ++i;
    }
    return out;
}

std::string Linkify(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        const bool http = text.compare(i, 7, "http://") == 0 || text.compare(i, 8, "https://") == 0;
        // An address already inside "](" or "[" is part of a link.
        const bool inLink = i > 0 && (text[i - 1] == '(' || text[i - 1] == '[');
        if (!http || inLink) { out += text[i++]; continue; }
        size_t end = i;
        while (end < text.size() && !isspace(static_cast<unsigned char>(text[end])) &&
               text[end] != ')' && text[end] != ']' && text[end] != '"' && text[end] != '\'') ++end;
        // Trailing punctuation belongs to the sentence.
        while (end > i && (text[end - 1] == '.' || text[end - 1] == ',' || text[end - 1] == ';' ||
                           text[end - 1] == ':')) --end;
        const std::string url = text.substr(i, end - i);
        out += Link(url, url);
        i = end;
    }
    return out;
}

} // namespace UltraCanvasStart
