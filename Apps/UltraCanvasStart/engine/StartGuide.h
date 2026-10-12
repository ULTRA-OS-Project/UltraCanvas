// Apps/UltraCanvasStart/engine/StartGuide.h
// The prose the pages show, written once as Markdown: the per-platform way
// in, the assistant page, and the words of the Tools step. Markdown so the window can
// render it with links that open, commands that stand out and bold where it
// matters, and so the terminal modes can print the same text plain
// (PlainText). Every numbered step is one action: a reader does it, then
// reads the next line.
// Version: 0.2.0 - CheckDetail and InstallPreview for the Tools step's rows,
//                  instead of ChecksMarkdown
// Version: 0.1.1 - AiGuide per assistant
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

// What the Tools step writes after a check's name: the version found, or
// why it is missing and the package that would provide it, or why it could
// not be checked. The window shows whether it is there with a switch, not a
// mark: a check mark and a cross are not in the bundled UI font.
std::string CheckDetail(const CheckResult& check);

// What "Install what is missing" will do with `plan`: a sentence naming the
// package manager, how many are missing and whether it asks for
// administrator rights, then the command on a line of its own - or one
// sentence saying why it has nothing to do or cannot run here.
std::string InstallPreview(const Plan& plan);

// The assistant page for `assistant`: whether it is installed and how to
// install it, how it picks up the repository's guidance, how an assistant
// works on an UltraCanvas application, and the checklist for a programmer
// without a compiler.
std::string AiGuide(const AiStatus& ai, Platform platform, Assistant assistant);

// Markdown reduced to plain text for the report and the terminal: `code`
// loses its backticks, **bold** its stars, [text](url) becomes "text (url)",
// and a leading "### " goes.
std::string PlainText(const std::string& markdown);

// Every bare http(s) address in `text` wrapped as a Markdown link, so it
// opens on a click. Addresses already inside a link are left alone.
std::string Linkify(const std::string& text);

} // namespace UltraCanvasStart
