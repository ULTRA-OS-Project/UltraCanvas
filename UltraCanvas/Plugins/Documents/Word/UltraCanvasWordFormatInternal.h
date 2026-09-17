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

#include "UltraCanvasRichDocument.h"

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

// A picture alone in a paragraph is a STANDALONE image; a picture among words
// is an inline one. The markup cannot tell them apart on its own - ODT anchors
// both as-char and DOCX wraps both in <wp:inline>, because a picture on a line
// of its own really is "inline, with nothing beside it" - so the decision is
// what else the paragraph holds. Returns true, and fills `outImage`, when the
// runs are exactly one picture and nothing but whitespace besides.
inline bool ParagraphIsOneInlineImage(const std::vector<RichTextRun>& runs,
                                      RichDocBlock& outImage) {
    const RichTextRun* picture = nullptr;
    for (const auto& run : runs) {
        if (run.IsInlineImage()) {
            if (picture) return false;          // two pictures: leave them inline
            picture = &run;
            continue;
        }
        for (char c : run.text) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
        }
    }
    if (!picture) return false;

    outImage = RichDocBlock();
    outImage.type = RichBlockType::Image;
    outImage.mediaIndex = picture->mediaIndex;
    outImage.imageWidthPt = picture->imageWidthPt;
    outImage.imageHeightPt = picture->imageHeightPt;
    outImage.imageAltText = picture->imageAltText;
    return true;
}

} // namespace WordFormatInternal
} // namespace UltraCanvas
