// Apps/UltraCanvasStart/engine/StartGuide.h
// The prose the pages show, written once as Markdown: the per-platform way
// in, the checks as a list, the assistant page. Markdown so the window can
// render it with links that open, commands that stand out and bold where it
// matters, and so the terminal modes can print the same text plain
// (PlainText). Every numbered step is one action: a reader does it, then
// reads the next line.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartAi.h"
#include "StartTypes.h"

#include <string>
#include <vector>

namespace UltraCanvasStart {

// Step 1 of Docs/GettingStarted.md for one platform, as a numbered list with
// one action per line. `version` and `architecture` name the SDK archive the
// reader would download (the framework's own, and the machine's or x86_64).
std::string PlatformGuide(Platform platform, const std::string& version,
                          const std::string& architecture);

// The checks as a Markdown list, one heading per dependency group, a mark
// per line (a check mark, a cross, a question mark for "not checkable"), the
// version found, why not, and the package that would provide it. Empty
// `checks` gives a one-line hint.
std::string ChecksMarkdown(const std::vector<CheckResult>& checks);

// The assistant page: whether Claude Code is installed and how to install
// it, how the assistant works on an UltraCanvas application, and the
// checklist for a programmer without a compiler.
std::string AiGuide(const AiStatus& ai, Platform platform);

// Markdown reduced to plain text for the report and the terminal: `code`
// loses its backticks, **bold** its stars, [text](url) becomes "text (url)",
// and a leading "### " goes.
std::string PlainText(const std::string& markdown);

// Every bare http(s) address in `text` wrapped as a Markdown link, so it
// opens on a click. Addresses already inside a link are left alone.
std::string Linkify(const std::string& text);

} // namespace UltraCanvasStart
