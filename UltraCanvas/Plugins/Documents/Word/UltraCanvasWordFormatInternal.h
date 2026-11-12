// Plugins/Documents/Word/UltraCanvasWordFormatInternal.h
// Small helpers shared by the ODT and DOCX readers/writers. Internal to the
// Word document module — not installed, not part of the public API.
// Version: 1.2.0 - ApplyOfficeArtShapeType: Word 97 / VML shape types as RichShape geometry;
//                  ReadNumberAt / ReadNumbers: dot-decimal shape geometry; ApplyOfficeShade
// Version: 1.1.1 - <cstdlib> for std::strtof, which libc++ no longer brings in
//                  through <string> (MSYS2 CLANG64, 2026-10-09)
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "UltraCanvasRichDocument.h"
#include "UltraCanvasTextUtils.h"

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
            // A floating picture belongs to its paragraph, which text after it
            // wraps round; it does not become a paragraph of its own.
            if (run.IsFloatingImage()) return false;
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

// True when the list item at `index` begins a new list for a writer: no list
// item before it, or the previous item at its level (within the same list,
// before any shallower item) differs in kind, number format, label template
// or bullet, or it restarts the count. Word and ODF lists carry one label
// definition per level, so such an item cannot share its predecessor's list.
inline bool StartsNewList(const std::vector<RichDocBlock>& blocks, size_t index) {
    const RichDocBlock& item = blocks[index];
    if (index == 0 || blocks[index - 1].type != RichBlockType::ListItem) return true;
    for (size_t i = index; i-- > 0 && blocks[i].type == RichBlockType::ListItem;) {
        const RichDocBlock& previous = blocks[i];
        if (previous.listLevel < item.listLevel) return false;    // first of a sublist
        if (previous.listLevel > item.listLevel) continue;
        if (previous.orderedList != item.orderedList) return true;
        if (item.orderedList) {
            if (previous.numberFormat != item.numberFormat || previous.numberTemplate != item.numberTemplate) {
                return true;
            }
            return item.listStartNumber > 0
                && item.listStartNumber != RichDocOrderedItemNumber(blocks, i) + 1;
        }
        return previous.bulletText != item.bulletText;
    }
    return false;
}

// ===== SYMBOL FONTS (UltraCanvasSymbolFonts.cpp) =====
// Unicode for `codepoint` set in `fontFamily` when that is one of the Windows
// symbol fonts (Symbol, Wingdings, Wingdings 2/3, Webdings); accepts the bare
// font code and the U+F000 + code form. 0 = not a symbol font, or unmapped.
// A dot-decimal number starting at text[at] (a leading '+' allowed), in
// `out`; returns where it ends, or `at` when there is none. Shape geometry
// ("1501,0", "0 0 3000 2000", "rotate (-0.52)") is read with it, never
// through the process locale.
inline size_t ReadNumberAt(const std::string& text, size_t at, double& out) {
    size_t from = at;
    if (from < text.size() && text[from] == '+') ++from;
    if (from >= text.size()) return at;
    const char* begin = text.c_str() + from;
    const char* end = ParseFloatClassic(begin, text.c_str() + text.size(), out);
    return end == begin ? at : static_cast<size_t>(end - text.c_str());
}

// Up to `count` numbers from a list separated by spaces or commas; returns
// how many were read.
inline size_t ReadNumbers(const std::string& text, double* out, size_t count) {
    size_t found = 0;
    for (size_t at = 0; at < text.size() && found < count;) {
        const size_t next = ReadNumberAt(text, at, out[found]);
        if (next == at) { ++at; continue; }
        ++found;
        at = next;
    }
    return found;
}

// A Word 97 / VML shade (gradient fill) as a shape's gradient: from `fill`
// to `back` (VML's color2), whose place along the shade `focusPercent`
// gives - 0 at the start, 100 at the end, 50 in the middle with `fill` at
// both edges, negative counting from the end. `angle` turns anticlockwise
// from top-to-bottom; a round shade starts at the centre.
inline void ApplyOfficeShade(RichShape& shape, const std::string& fill, const std::string& back, double focusPercent,
                             double angle, bool radial) {
    double where = focusPercent / 100.0;
    if (where < 0.0) where += 1.0;
    where = std::clamp(where, 0.0, 1.0);
    if (where < 0.25) shape.gradientStops = {{0.0f, back}, {1.0f, fill}};
    else if (where > 0.75) shape.gradientStops = {{0.0f, fill}, {1.0f, back}};
    else shape.gradientStops = {{0.0f, fill}, {0.5f, back}, {1.0f, fill}};
    shape.gradientRadial = radial;
    double turn = std::fmod(90.0 - angle, 360.0);
    if (turn < 0.0) turn += 360.0;
    shape.gradientAngleDeg = static_cast<float>(turn);
    shape.fillColor = shape.gradientStops.front().color;
}

// A Word 97 / VML shape type (MSOSPT: 1 rectangle, 3 ellipse, 20 line,
// ...) as a RichShape's geometry; false for one it does not know (a
// freeform, 0, has its own outline).
inline bool ApplyOfficeArtShapeType(int type, RichShape& shape) {
    static const std::pair<int, const char*> kTypes[] = {
        {1, "rect"}, {2, "roundRect"}, {3, "ellipse"}, {4, "diamond"}, {5, "triangle"}, {6, "rtTriangle"},
        {7, "parallelogram"}, {8, "trapezoid"}, {9, "hexagon"}, {10, "octagon"}, {11, "plus"}, {12, "star5"},
        {13, "rightArrow"}, {15, "homePlate"}, {20, "line"}, {32, "straightConnector1"}, {55, "chevron"},
        {56, "pentagon"}, {66, "leftArrow"}, {67, "downArrow"}, {68, "upArrow"}, {109, "flowChartProcess"},
        {110, "flowChartDecision"}, {111, "flowChartInputOutput"}, {116, "flowChartTerminator"},
        {117, "flowChartPreparation"}, {120, "flowChartConnector"}, {127, "flowChartExtract"},
        {128, "flowChartMerge"}, {176, "flowChartAlternateProcess"}, {187, "star4"}, {202, "rect"}};
    for (const auto& [id, name] : kTypes) {
        if (id == type) return shape.ApplyPreset(name);
    }
    return false;
}

uint32_t SymbolFontCharToUnicode(const std::string& fontFamily, uint32_t codepoint);
// Rewrites every run set in a symbol font (body, table cells, headers,
// footers and text frames) as the Unicode characters it depicts, and drops
// the symbol font from the run.
void MapSymbolFontRuns(UCRichDocument& document);
// Decodes the UTF-8 sequence at text[at] and advances `at` past it.
uint32_t DecodeUtf8(const std::string& text, size_t& at);
// Appends `codepoint` to `out` as UTF-8.
void AppendUtf8(std::string& out, uint32_t codepoint);

} // namespace WordFormatInternal
} // namespace UltraCanvas
