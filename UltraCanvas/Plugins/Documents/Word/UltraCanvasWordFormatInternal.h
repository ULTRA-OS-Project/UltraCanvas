// Plugins/Documents/Word/UltraCanvasWordFormatInternal.h
// Small helpers shared by the ODT and DOCX readers/writers. Internal to the
// Word document module — not installed, not part of the public API.
// Version: 1.1.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"

namespace UltraCanvas {
namespace WordFormatInternal {

inline std::string EscapeXml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Parses "12pt" / "0.5in" / "1.27cm" / "10mm" style ODF lengths into points.
inline float ParseLengthPt(const std::string& value) {
    if (value.empty()) return 0.0f;
    char* end = nullptr;
    float number = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) return 0.0f;
    std::string unit = ToLower(std::string(end));
    if (unit == "pt" || unit.empty()) return number;
    if (unit == "in") return number * 72.0f;
    if (unit == "cm") return number * 72.0f / 2.54f;
    if (unit == "mm") return number * 72.0f / 25.4f;
    if (unit == "px") return number * 72.0f / 96.0f;
    return number;
}

// The ODT/DOCX writers have no formula writer, so a display-math block is
// written as a centred paragraph holding "$$ source $$" on one line: the
// Markdown pipeline typesets it again after a re-import.
inline RichDocBlock MathBlockAsParagraph(const RichDocBlock& block) {
    RichDocBlock paragraph;
    paragraph.type = RichBlockType::Paragraph;
    paragraph.align = RichTextAlign::Center;
    RichTextRun run;
    run.text = "$$";
    for (char c : UCRichDocument::ConcatenateRunText(block.runs)) {
        run.text.push_back(c == '\n' ? ' ' : c);
    }
    run.text += "$$";
    paragraph.runs.push_back(run);
    return paragraph;
}

} // namespace WordFormatInternal
} // namespace UltraCanvas
