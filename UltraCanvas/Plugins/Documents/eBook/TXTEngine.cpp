// Plugins/Documents/eBook/TXTEngine.cpp
// Plain-text engine implementation + builtin engine registration.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "TXTEngine.h"
#include "EPUBEngine.h"

#include <cctype>

namespace UltraCanvas {

namespace {

std::string EscapeHTML(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool IsBlankLine(const std::string& line) {
    for (char c : line) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

} // namespace

std::string TXTEngine::TextToHTML(const std::string& text) {
    std::string html = "<html><body>\n";

    std::string paragraph;
    auto flush = [&]() {
        if (paragraph.empty()) return;
        html += "<p>" + EscapeHTML(paragraph) + "</p>\n";
        paragraph.clear();
    };

    std::string line;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = (i < text.size()) ? text[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            if (IsBlankLine(line)) {
                flush();
            } else {
                if (!paragraph.empty()) paragraph += ' ';
                // Trim trailing whitespace of the joined line.
                size_t end = line.find_last_not_of(" \t");
                paragraph += (end == std::string::npos) ? line : line.substr(0, end + 1);
            }
            line.clear();
        } else {
            line += c;
        }
    }
    flush();

    html += "</body></html>";
    return html;
}

bool TXTEngine::LoadFromMemory(std::vector<uint8_t> data,
                               const std::string& /*password*/) {
    Close();

    std::string text(data.begin(), data.end());
    // Skip UTF-8 BOM.
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text.erase(0, 3);
    }

    // Form feed splits chapters; most files end up as a single chapter.
    std::vector<std::string> rawChapters;
    size_t start = 0;
    while (start <= text.size()) {
        size_t ff = text.find('\f', start);
        std::string part = (ff == std::string::npos)
                               ? text.substr(start)
                               : text.substr(start, ff - start);
        if (!IsBlankLine(part)) rawChapters.push_back(std::move(part));
        if (ff == std::string::npos) break;
        start = ff + 1;
    }
    if (rawChapters.empty()) {
        Fail("File contains no text");
        return false;
    }

    for (size_t i = 0; i < rawChapters.size(); ++i) {
        EBookChapter chapter;
        chapter.title = rawChapters.size() > 1
                            ? "Part " + std::to_string(i + 1) : "";
        chapter.href = "chapter" + std::to_string(i + 1);
        chapter.content = TextToHTML(rawChapters[i]);
        chapters.push_back(std::move(chapter));

        EBookTOCEntry entry;
        entry.title = chapters.back().title.empty() ? "Text" : chapters.back().title;
        entry.href = chapters.back().href;
        entry.pageNumber = static_cast<int>(i);
        entry.index = static_cast<int>(i);
        tableOfContents.push_back(std::move(entry));
    }

    metadata.format = EBookFormat::TXT;
    metadata.chapterCount = static_cast<int>(chapters.size());

    loaded = true;
    return true;
}

void TXTEngine::Close() {
    ResetBaseState();
    chapters.clear();
}

EBookChapter TXTEngine::GetChapter(int index) const {
    if (index < 0 || index >= static_cast<int>(chapters.size())) return {};
    return chapters[static_cast<size_t>(index)];
}

// ============================================================================
// REGISTRATION
// ============================================================================

void RegisterTXTEngine() {
    RegisterEBookEngine({"txt"}, [] {
        return std::static_pointer_cast<IEBookEngine>(std::make_shared<TXTEngine>());
    });
}

void RegisterBuiltinEBookEngines() {
    RegisterEPUBEngine();
    RegisterTXTEngine();
}

} // namespace UltraCanvas
