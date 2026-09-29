// core/UltraCanvasRichDocument.cpp
// UCRichDocument serializers: Markdown (editable round-trip), HTML
// (read-only rich view), plain text — plus media helpers shared by the
// ODT/DOCX readers and writers.
// Version: 1.1.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "UltraCanvasRichDocument.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <algorithm>
#include <locale>
#include <map>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

namespace {

// ===== SMALL SHARED HELPERS =====

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Base64Encode(const std::vector<uint8_t>& data) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(alphabet[(v >> 6) & 63]);
        out.push_back(alphabet[v & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t v = data[i] << 16;
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out += "==";
    } else if (i + 2 == data.size()) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(alphabet[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string EscapeHtml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// ===== MARKDOWN EMISSION =====

// Escapes characters that would otherwise start emphasis/code/link markup.
// Kept minimal on purpose: the escaped text is what the user sees and edits
// in MarkdownHybrid mode, so noise must stay low.
std::string EscapeMarkdownText(const std::string& text, bool inTableCell) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        // An embedded equation (OMML / MathML import) is LaTeX between $...$;
        // it is copied verbatim so the Markdown pipeline can typeset it.
        if (c == '$') {
            const size_t close = text.find('$', i + 1);
            if (close != std::string::npos && close > i + 1) {
                out.append(text, i, close - i + 1);
                i = close;
                continue;
            }
        }
        if (c == '\\' || c == '*' || c == '`' || c == '[') {
            out.push_back('\\');
        } else if (inTableCell && c == '|') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// Merges adjacent runs whose formatting is identical so per-run markers do
// not produce "**a****b**".
std::vector<RichTextRun> MergeAdjacentRuns(const std::vector<RichTextRun>& runs) {
    std::vector<RichTextRun> merged;
    for (const auto& run : runs) {
        if (!merged.empty() && !run.lineBreakBefore
            && merged.back().HasSameFormatting(run)) {
            merged.back().text += run.text;
        } else {
            merged.push_back(run);
        }
    }
    return merged;
}

// Emphasis markers only work when they hug non-space characters, so leading/
// trailing whitespace is emitted outside the markers.
void SplitEdgeWhitespace(const std::string& text, std::string& lead,
                         std::string& core, std::string& trail) {
    size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        lead = text;
        core.clear();
        trail.clear();
        return;
    }
    size_t end = text.find_last_not_of(" \t");
    lead = text.substr(0, begin);
    core = text.substr(begin, end - begin + 1);
    trail = text.substr(end + 1);
}

// The concatenated run text with each inline-image placeholder replaced by its
// alt text in brackets. ConcatenateRunText() must keep returning the raw
// placeholder - positions index into it - so anything meant for a human reads
// through here instead.
std::string RunsToReadableText(const std::vector<RichTextRun>& runs) {
    std::string out;
    for (const auto& run : runs) {
        if (run.lineBreakBefore) out += '\n';
        if (run.IsDeleted()) continue;          // a tracked deletion: the text as it will be
        if (run.IsInlineImage()) {
            out += "[" + (run.imageAltText.empty() ? std::string("image") : run.imageAltText) + "]";
        } else if (run.IsNoteReference()) {
            out += "[" + run.text + "]";
        } else {
            out += run.text;
        }
    }
    return out;
}

std::string RunToMarkdown(const RichTextRun& run, bool inTableCell,
                          const std::vector<std::string>* mediaPaths = nullptr) {
    // An inline picture is a markdown image reference sitting in the line, not
    // a paragraph of its own. With no media directory to write files into there
    // is no path to point at, so it degrades to its alt text in brackets -
    // the same way a block image does.
    if (run.IsInlineImage()) {
        const std::string alt = run.imageAltText.empty() ? std::string("image") : run.imageAltText;
        if (mediaPaths && run.mediaIndex >= 0
            && run.mediaIndex < static_cast<int>(mediaPaths->size())
            && !(*mediaPaths)[static_cast<size_t>(run.mediaIndex)].empty()) {
            return "![" + alt + "](" + (*mediaPaths)[static_cast<size_t>(run.mediaIndex)] + ")";
        }
        return "[" + alt + "]";
    }

    // A note reference is Markdown's footnote reference.
    if (run.IsNoteReference()) return "[^" + run.text + "]";

    std::string lead, core, trail;
    SplitEdgeWhitespace(run.text, lead, core, trail);
    if (core.empty()) return run.text;

    std::string body;
    if (run.math) {
        // LaTeX source between single dollars, unescaped: the Markdown pipeline
        // hands it to the math engine. Hard breaks would end the formula, so
        // the source is flattened to one line.
        body = "$";
        for (char c : core) body.push_back(c == '\n' ? ' ' : c);
        body += "$";
    } else if (run.code) {
        // Inline code swallows other emphasis; pick a fence that is not in the text.
        std::string fence = "`";
        while (core.find(fence) != std::string::npos) fence += "`";
        body = fence + core + fence;
    } else {
        body = EscapeMarkdownText(core, inTableCell);
        if (run.bold && run.italic) body = "***" + body + "***";
        else if (run.bold)          body = "**" + body + "**";
        else if (run.italic)        body = "*" + body + "*";
        if (run.strikethrough)      body = "~~" + body + "~~";
        // ^x^ / ~x~ are the TextArea's super-/subscript markers; they only
        // work around a single word, so longer spans stay plain text.
        const bool oneWord = body.find_first_of(" \t^~") == std::string::npos;
        if (run.superscript && oneWord)     body = "^" + body + "^";
        else if (run.subscript && oneWord)  body = "~" + body + "~";
    }
    if (!run.linkTarget.empty()) {
        body = "[" + body + "](" + run.linkTarget + ")";
    }
    return lead + body + trail;
}

std::string RunsToMarkdown(const std::vector<RichTextRun>& runs, bool inTableCell,
                           const std::vector<std::string>* mediaPaths = nullptr) {
    std::string out;
    for (const auto& run : MergeAdjacentRuns(runs)) {
        if (run.lineBreakBefore && !out.empty()) {
            // Hard break inside a paragraph: markdown two-space line break in
            // normal flow, "<br>" is avoided; inside table cells fall back to a space.
            out += inTableCell ? " " : "  \n";
        }
        if (run.IsDeleted()) continue;
        out += RunToMarkdown(run, inTableCell, mediaPaths);
    }
    return out;
}

// ===== MARKDOWN PARSING (inline) =====

struct InlineStyleState {
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    std::string linkTarget;
};

void AppendTextRun(std::vector<RichTextRun>& runs, const std::string& text,
                   const InlineStyleState& style) {
    if (text.empty()) return;
    RichTextRun run;
    run.text = text;
    run.bold = style.bold;
    run.italic = style.italic;
    run.strikethrough = style.strikethrough;
    run.linkTarget = style.linkTarget;
    runs.push_back(run);
}

// Finds the matching closing marker (e.g. "**") starting the search at from.
size_t FindClosingMarker(const std::string& text, size_t from, const std::string& marker) {
    while (from < text.size()) {
        size_t pos = text.find(marker, from);
        if (pos == std::string::npos) return std::string::npos;
        if (pos > 0 && text[pos - 1] == '\\') {
            from = pos + 1;
            continue;
        }
        return pos;
    }
    return std::string::npos;
}

void ParseInlineMarkdown(const std::string& text, const InlineStyleState& style,
                         std::vector<RichTextRun>& runs);

// Footnote labels met while FromMarkdown reads a document, in order: a
// reference's noteIndex is its label's place here. Null outside that.
thread_local std::vector<std::string>* gMarkdownNoteLabels = nullptr;

// Handles a *...* / **...** / ***...*** / ~~...~~ span. Returns true and
// advances pos past the span when a well-formed closing marker exists.
bool TryParseEmphasis(const std::string& text, size_t& pos, const InlineStyleState& style,
                      std::vector<RichTextRun>& runs, std::string& pending) {
    char c = text[pos];
    size_t markerLen = 1;
    while (markerLen < 3 && pos + markerLen < text.size() && text[pos + markerLen] == c) {
        ++markerLen;
    }
    if (c == '~' && markerLen < 2) return false;   // single ~ is literal
    if (c == '~') markerLen = 2;
    std::string marker = std::string(markerLen, c);

    size_t close = FindClosingMarker(text, pos + markerLen, marker);
    if (close == std::string::npos) return false;
    std::string inner = text.substr(pos + markerLen, close - pos - markerLen);
    if (inner.empty() || inner.front() == ' ' || inner.back() == ' ') return false;

    AppendTextRun(runs, pending, style);
    pending.clear();

    InlineStyleState innerStyle = style;
    if (c == '~') {
        innerStyle.strikethrough = true;
    } else {
        if (markerLen >= 2) innerStyle.bold = true;
        if (markerLen == 1 || markerLen == 3) innerStyle.italic = true;
    }
    ParseInlineMarkdown(inner, innerStyle, runs);
    pos = close + markerLen;
    return true;
}

// Handles [text](url). Returns true and advances pos on success.
bool TryParseLink(const std::string& text, size_t& pos, const InlineStyleState& style,
                  std::vector<RichTextRun>& runs, std::string& pending) {
    size_t closeBracket = FindClosingMarker(text, pos + 1, "]");
    if (closeBracket == std::string::npos) return false;
    if (closeBracket + 1 >= text.size() || text[closeBracket + 1] != '(') return false;
    size_t closeParen = text.find(')', closeBracket + 2);
    if (closeParen == std::string::npos) return false;

    AppendTextRun(runs, pending, style);
    pending.clear();

    InlineStyleState innerStyle = style;
    innerStyle.linkTarget = text.substr(closeBracket + 2, closeParen - closeBracket - 2);
    ParseInlineMarkdown(text.substr(pos + 1, closeBracket - pos - 1), innerStyle, runs);
    pos = closeParen + 1;
    return true;
}

void ParseInlineMarkdown(const std::string& text, const InlineStyleState& style,
                         std::vector<RichTextRun>& runs) {
    std::string pending;
    size_t pos = 0;
    while (pos < text.size()) {
        char c = text[pos];
        if (c == '\\' && pos + 1 < text.size()) {
            pending.push_back(text[pos + 1]);
            pos += 2;
            continue;
        }
        if (c == '`') {
            size_t fenceLen = 1;
            while (pos + fenceLen < text.size() && text[pos + fenceLen] == '`') ++fenceLen;
            std::string fence(fenceLen, '`');
            size_t close = text.find(fence, pos + fenceLen);
            if (close != std::string::npos) {
                AppendTextRun(runs, pending, style);
                pending.clear();
                RichTextRun codeRun;
                codeRun.text = text.substr(pos + fenceLen, close - pos - fenceLen);
                codeRun.code = true;
                codeRun.bold = style.bold;
                codeRun.italic = style.italic;
                codeRun.strikethrough = style.strikethrough;
                codeRun.linkTarget = style.linkTarget;
                runs.push_back(codeRun);
                pos = close + fenceLen;
                continue;
            }
        }
        if ((c == '*' || c == '_' || c == '~')
            && TryParseEmphasis(text, pos, style, runs, pending)) {
            continue;
        }
        if (c == '!' && pos + 1 < text.size() && text[pos + 1] == '[') {
            // Inline image inside mixed content: degrade to its alt text.
            size_t closeBracket = FindClosingMarker(text, pos + 2, "]");
            if (closeBracket != std::string::npos && closeBracket + 1 < text.size()
                && text[closeBracket + 1] == '(') {
                size_t closeParen = text.find(')', closeBracket + 2);
                if (closeParen != std::string::npos) {
                    pending += text.substr(pos + 2, closeBracket - pos - 2);
                    pos = closeParen + 1;
                    continue;
                }
            }
        }
        // A footnote reference "[^label]", while a document is being read.
        if (c == '[' && gMarkdownNoteLabels && pos + 2 < text.size() && text[pos + 1] == '^') {
            const size_t close = text.find(']', pos + 2);
            if (close != std::string::npos && close > pos + 2 && text.find(' ', pos + 2) > close) {
                AppendTextRun(runs, pending, style);
                pending.clear();
                const std::string label = text.substr(pos + 2, close - pos - 2);
                auto found = std::find(gMarkdownNoteLabels->begin(), gMarkdownNoteLabels->end(), label);
                RichTextRun reference;
                reference.noteIndex = static_cast<int>(found - gMarkdownNoteLabels->begin());
                if (found == gMarkdownNoteLabels->end()) gMarkdownNoteLabels->push_back(label);
                reference.text = label;
                reference.superscript = true;
                runs.push_back(reference);
                pos = close + 1;
                continue;
            }
        }
        if (c == '[' && TryParseLink(text, pos, style, runs, pending)) {
            continue;
        }
        pending.push_back(c);
        ++pos;
    }
    AppendTextRun(runs, pending, style);
}

// ===== MARKDOWN PARSING (blocks) =====

struct ListMarkerInfo {
    bool isList = false;
    bool ordered = false;
    int level = 0;
    std::string content;
};

ListMarkerInfo ParseListMarker(const std::string& line) {
    ListMarkerInfo info;
    size_t indent = 0;
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
        indent += (line[pos] == '\t') ? 4 : 1;
        ++pos;
    }
    if (pos >= line.size()) return info;

    char c = line[pos];
    if ((c == '-' || c == '*' || c == '+') && pos + 1 < line.size() && line[pos + 1] == ' ') {
        info.isList = true;
        info.ordered = false;
        info.content = line.substr(pos + 2);
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
        size_t digitEnd = pos;
        while (digitEnd < line.size() && std::isdigit(static_cast<unsigned char>(line[digitEnd]))) {
            ++digitEnd;
        }
        if (digitEnd < line.size() && (line[digitEnd] == '.' || line[digitEnd] == ')')
            && digitEnd + 1 < line.size() && line[digitEnd + 1] == ' ') {
            info.isList = true;
            info.ordered = true;
            info.content = line.substr(digitEnd + 2);
        }
    }
    info.level = static_cast<int>(indent / 2);
    return info;
}

bool IsHorizontalRule(const std::string& line) {
    size_t begin = line.find_first_not_of(" \t");
    if (begin == std::string::npos) return false;
    char c = line[begin];
    if (c != '-' && c != '*' && c != '_') return false;
    int count = 0;
    for (size_t i = begin; i < line.size(); ++i) {
        if (line[i] == c) ++count;
        else if (line[i] != ' ' && line[i] != '\t') return false;
    }
    return count >= 3;
}

bool IsTableSeparatorLine(const std::string& line) {
    // e.g. | --- |:---:| --- |
    bool sawDash = false;
    for (char c : line) {
        if (c == '-') sawDash = true;
        else if (c != '|' && c != ':' && c != ' ' && c != '\t') return false;
    }
    return sawDash && line.find('|') != std::string::npos;
}

std::vector<std::string> SplitTableCells(const std::string& line) {
    std::vector<std::string> cells;
    std::string current;
    size_t pos = 0;
    // Skip the leading pipe.
    if (pos < line.size() && line[pos] == '|') ++pos;
    for (; pos < line.size(); ++pos) {
        char c = line[pos];
        if (c == '\\' && pos + 1 < line.size()) {
            current.push_back(c);
            current.push_back(line[pos + 1]);
            ++pos;
        } else if (c == '|') {
            cells.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    // Trailing text after the last pipe (tables normally end with |).
    if (!current.empty() && current.find_first_not_of(" \t") != std::string::npos) {
        cells.push_back(current);
    }
    for (auto& cell : cells) {
        size_t b = cell.find_first_not_of(" \t");
        size_t e = cell.find_last_not_of(" \t");
        cell = (b == std::string::npos) ? std::string() : cell.substr(b, e - b + 1);
    }
    return cells;
}

// Matches a line that is exactly one image reference: ![alt](path)
bool ParseStandaloneImage(const std::string& line, std::string& alt, std::string& path) {
    size_t begin = line.find_first_not_of(" \t");
    if (begin == std::string::npos || line[begin] != '!') return false;
    if (begin + 1 >= line.size() || line[begin + 1] != '[') return false;
    size_t closeBracket = FindClosingMarker(line, begin + 2, "]");
    if (closeBracket == std::string::npos) return false;
    if (closeBracket + 1 >= line.size() || line[closeBracket + 1] != '(') return false;
    size_t closeParen = line.find(')', closeBracket + 2);
    if (closeParen == std::string::npos) return false;
    if (line.find_first_not_of(" \t", closeParen + 1) != std::string::npos) return false;
    alt = line.substr(begin + 2, closeBracket - begin - 2);
    path = line.substr(closeBracket + 2, closeParen - closeBracket - 2);
    return true;
}

} // namespace

// ===== TABLE GRID =====

const RichTableGridSlot RichTableGrid::kEmpty{};

const RichTableGridSlot& RichTableGrid::At(int row, int column) const {
    if (row < 0 || row >= rowCount || column < 0 || column >= columnCount) return kEmpty;
    return slots[static_cast<size_t>(row) * static_cast<size_t>(columnCount) +
                 static_cast<size_t>(column)];
}

bool RichTableGrid::OriginOf(int row, int cellIndex, int& outRow, int& outColumn) const {
    for (int c = 0; c < columnCount; ++c) {
        const RichTableGridSlot& slot = At(row, c);
        if (slot.origin && slot.row == row && slot.cellIndex == cellIndex) {
            outRow = row;
            outColumn = c;
            return true;
        }
    }
    return false;
}

bool RichTableGrid::CellAt(int row, int column, int& outRow, int& outCellIndex) const {
    const RichTableGridSlot& slot = At(row, column);
    if (!slot.Occupied()) return false;
    outRow = slot.row;
    outCellIndex = slot.cellIndex;
    return true;
}

RichTableGrid BuildTableGrid(const RichDocBlock& table) {
    RichTableGrid grid;
    if (table.type != RichBlockType::Table || table.tableRows.empty()) return grid;

    grid.rowCount = static_cast<int>(table.tableRows.size());

    // The grid is as wide as the widest row counting column spans, not as the
    // row with the most cells: one cell spanning three columns is three wide.
    for (const RichTableRow& row : table.tableRows) {
        int width = 0;
        for (const RichTableCell& cell : row.cells) width += std::max(1, cell.columnSpan);
        grid.columnCount = std::max(grid.columnCount, width);
    }
    if (grid.columnCount == 0) {
        grid.rowCount = 0;
        return grid;
    }
    grid.slots.assign(static_cast<size_t>(grid.rowCount) * static_cast<size_t>(grid.columnCount),
                      RichTableGridSlot{});

    auto slotAt = [&grid](int row, int column) -> RichTableGridSlot& {
        return grid.slots[static_cast<size_t>(row) * static_cast<size_t>(grid.columnCount) +
                          static_cast<size_t>(column)];
    };

    for (int r = 0; r < grid.rowCount; ++r) {
        const RichTableRow& row = table.tableRows[static_cast<size_t>(r)];
        int cellIndex = 0;
        for (int column = 0; column < grid.columnCount; ) {
            if (slotAt(r, column).Occupied()) {
                column++;                       // taken by a cell spanning down from above
                continue;
            }
            if (cellIndex >= static_cast<int>(row.cells.size())) break;   // ragged row
            const RichTableCell& cell = row.cells[static_cast<size_t>(cellIndex)];
            // Clamp both spans to what the grid can hold: a document claiming a
            // rowSpan past the last row is malformed, and every caller after
            // this point would otherwise index out of bounds.
            const int columnSpan = std::min(std::max(1, cell.columnSpan), grid.columnCount - column);
            const int rowSpan = std::min(std::max(1, cell.rowSpan), grid.rowCount - r);
            for (int dr = 0; dr < rowSpan; ++dr) {
                for (int dc = 0; dc < columnSpan; ++dc) {
                    RichTableGridSlot& slot = slotAt(r + dr, column + dc);
                    slot.row = r;
                    slot.cellIndex = cellIndex;
                    slot.origin = (dr == 0 && dc == 0);
                }
            }
            column += columnSpan;
            cellIndex++;
        }
    }
    return grid;
}

// ===== MEDIA HELPERS =====

int UCRichDocument::AddMedia(std::string name, std::string mimeType, std::vector<uint8_t> data) {
    for (size_t i = 0; i < media.size(); ++i) {
        if (media[i].data == data && media[i].mimeType == mimeType) {
            return static_cast<int>(i);
        }
    }
    RichDocMedia entry;
    entry.name = std::move(name);
    entry.mimeType = std::move(mimeType);
    entry.data = std::move(data);
    media.push_back(std::move(entry));
    return static_cast<int>(media.size()) - 1;
}

std::string UCRichDocument::MimeTypeForImageName(const std::string& fileName) {
    std::string ext = ToLowerCopy(PathToUtf8(PathFromUtf8(fileName).extension()));
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".jfif") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".webp") return "image/webp";
    if (ext == ".tif" || ext == ".tiff") return "image/tiff";
    if (ext == ".emf") return "image/x-emf";
    if (ext == ".wmf") return "image/x-wmf";
    return "application/octet-stream";
}

std::string UCRichDocument::FileExtensionForMimeType(const std::string& mimeType) {
    if (mimeType == "image/png") return "png";
    if (mimeType == "image/jpeg") return "jpg";
    if (mimeType == "image/gif") return "gif";
    if (mimeType == "image/svg+xml") return "svg";
    if (mimeType == "image/bmp") return "bmp";
    if (mimeType == "image/webp") return "webp";
    if (mimeType == "image/tiff") return "tif";
    if (mimeType == "image/x-emf") return "emf";
    if (mimeType == "image/x-wmf") return "wmf";
    return "bin";
}

bool UCRichDocument::SniffImagePixelSize(const std::vector<uint8_t>& data, int& width, int& height) {
    width = height = 0;
    if (data.size() >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        width  = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
        height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
        return width > 0 && height > 0;
    }
    if (data.size() >= 10 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        width  = data[6] | (data[7] << 8);
        height = data[8] | (data[9] << 8);
        return width > 0 && height > 0;
    }
    if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        // JPEG: walk the marker chain until a start-of-frame marker.
        size_t pos = 2;
        while (pos + 9 < data.size()) {
            if (data[pos] != 0xFF) { ++pos; continue; }
            uint8_t marker = data[pos + 1];
            if (marker == 0xFF) { ++pos; continue; }
            if (marker >= 0xD0 && marker <= 0xD9) { pos += 2; continue; }
            size_t segLen = (data[pos + 2] << 8) | data[pos + 3];
            bool isSOF = (marker >= 0xC0 && marker <= 0xCF)
                         && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
            if (isSOF && pos + 8 < data.size()) {
                height = (data[pos + 5] << 8) | data[pos + 6];
                width  = (data[pos + 7] << 8) | data[pos + 8];
                return width > 0 && height > 0;
            }
            if (segLen < 2) break;
            pos += 2 + segLen;
        }
    }
    return false;
}

std::string UCRichDocument::ConcatenateRunText(const std::vector<RichTextRun>& runs) {
    std::string out;
    for (const auto& run : runs) {
        if (run.lineBreakBefore && !out.empty()) out.push_back('\n');
        out += run.text;
    }
    return out;
}

// ===== MARKDOWN SERIALIZER =====

UCRichDocument UCRichDocument::WithFirstPageFurnitureInline() const {
    UCRichDocument flat = *this;
    flat.pageFurniture = RichPageFurniture{};
    flat.firstPageFurniture = RichPageFurniture{};
    flat.firstPageDiffers = false;
    const RichPageFurniture& furniture = FurnitureForPage(0);
    std::vector<RichDocBlock> blocksInline;
    RichDocBlock rule;
    rule.type = RichBlockType::HorizontalRule;
    if (!furniture.header.empty()) {
        blocksInline.insert(blocksInline.end(), furniture.header.begin(), furniture.header.end());
        blocksInline.push_back(rule);
    }
    blocksInline.insert(blocksInline.end(), blocks.begin(), blocks.end());
    if (!furniture.footer.empty()) {
        blocksInline.push_back(rule);
        blocksInline.insert(blocksInline.end(), furniture.footer.begin(), furniture.footer.end());
    }
    flat.blocks = std::move(blocksInline);
    return flat;
}

namespace {
const char* const kBallotBox = "\xE2\x98\x90";          // ☐ U+2610
const char* const kBallotBoxChecked = "\xE2\x98\x92";   // ☒ U+2612
const char* const kBallotBoxTicked = "\xE2\x98\x91";    // ☑ U+2611
const char* const kNoBreakSpace = "\xC2\xA0";
} // namespace

int UCRichDocument::ReadCheckboxPrefixes() {
    int converted = 0;
    for (RichDocBlock& block : blocks) {
        if (block.checkbox) continue;
        if (block.type != RichBlockType::ListItem && block.type != RichBlockType::Paragraph) continue;
        if (block.type == RichBlockType::ListItem && block.orderedList) continue;
        // The box is the first visible character; a picture or a break first
        // means it is not a check box.
        size_t first = 0;
        while (first < block.runs.size() && block.runs[first].text.empty()
               && !block.runs[first].lineBreakBefore && !block.runs[first].IsInlineImage()) {
            ++first;
        }
        if (first >= block.runs.size()) continue;
        RichTextRun& run = block.runs[first];
        if (run.IsInlineImage() || run.lineBreakBefore || run.text.size() < 3) continue;
        const std::string box = run.text.substr(0, 3);
        const bool ticked = box == kBallotBoxChecked || box == kBallotBoxTicked;
        if (!ticked && box != kBallotBox) continue;
        size_t cut = 3;
        // The space Word puts between the box and the text goes with it.
        while (cut < run.text.size() && (run.text[cut] == ' ' || run.text[cut] == '\t')) ++cut;
        if (run.text.compare(cut, 2, kNoBreakSpace) == 0) cut += 2;
        run.text.erase(0, cut);
        if (run.text.empty() && block.runs.size() > 1) block.runs.erase(block.runs.begin() + static_cast<long>(first));
        if (block.type == RichBlockType::Paragraph) {
            block.type = RichBlockType::ListItem;
            block.listLevel = 0;
        }
        block.orderedList = false;
        block.checkbox = true;
        block.checked = ticked;
        if (block.bulletText == kNoBreakSpace) block.bulletText.clear();
        ++converted;
    }
    return converted;
}

UCRichDocument UCRichDocument::WithCheckboxesAsPrefixes() const {
    UCRichDocument out = *this;
    for (RichDocBlock& block : out.blocks) {
        if (!block.checkbox || block.type != RichBlockType::ListItem) continue;
        RichTextRun box;
        if (!block.runs.empty() && !block.runs.front().IsInlineImage()) {
            box = block.runs.front();       // the text's own font and size
            box.linkTarget.clear();
            box.lineBreakBefore = false;
            box.field = RichTextRun::Field::Plain;
        }
        box.text = std::string(block.checked ? kBallotBoxChecked : kBallotBox) + " ";
        block.runs.insert(block.runs.begin(), box);
        // The box stands where the bullet would; a bullet beside it would be
        // two markers for one item.
        block.bulletText = kNoBreakSpace;
        block.checkbox = false;
        block.checked = false;
    }
    return out;
}

std::string UCRichDocument::ToMarkdown(const RichDocumentMarkdownOptions& options) const {
    // Text output has no pages: the first page's header and footer go before
    // and after the body, set off by rules.
    if (!FurnitureForPage(0).IsEmpty()) return WithFirstPageFurnitureInline().ToMarkdown(options);
    // Write referenced media to disk once, remembering the path per index.
    std::vector<std::string> mediaPaths(media.size());
    if (!options.imageDirectory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.imageDirectory, ec);
        for (size_t i = 0; i < media.size(); ++i) {
            std::string name = media[i].name.empty()
                ? "image" + std::to_string(i + 1) + "." + FileExtensionForMimeType(media[i].mimeType)
                : PathToUtf8(PathFromUtf8(media[i].name).filename());
            std::filesystem::path target = PathFromUtf8(options.imageDirectory) / name;
            std::ofstream out(target, std::ios::binary);
            if (out.is_open()) {
                out.write(reinterpret_cast<const char*>(media[i].data.data()),
                          static_cast<std::streamsize>(media[i].data.size()));
                mediaPaths[i] = PathToUtf8(target);
            }
        }
    }

    std::ostringstream md;
    bool first = true;
    auto blockSeparator = [&]() {
        if (!first) md << "\n";
        first = false;
    };

    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const RichDocBlock& block = blocks[bi];
        switch (block.type) {
            case RichBlockType::Heading: {
                blockSeparator();
                int level = std::clamp(block.headingLevel, 1, 6);
                // A heading is bold already: a heading style's bold on its runs
                // would otherwise come out as "# **Title**".
                std::vector<RichTextRun> runs = block.runs;
                for (RichTextRun& run : runs) run.bold = false;
                md << std::string(level, '#') << ' ' << RunsToMarkdown(runs, false, &mediaPaths) << "\n";
                break;
            }
            case RichBlockType::ListItem: {
                // Consecutive list items form one list without blank lines.
                bool previousIsListItem = bi > 0 && blocks[bi - 1].type == RichBlockType::ListItem;
                if (!previousIsListItem) blockSeparator();
                else first = false;
                md << std::string(static_cast<size_t>(std::max(0, block.listLevel)) * 2, ' ')
                   // A list that starts at N (or runs on past an interruption)
                   // spells N on its item: Markdown starts a list at its first
                   // number and counts on from there.
                   << (!block.orderedList ? std::string(block.checkbox ? (block.checked ? "- [x] " : "- [ ] ") : "- ")
                       : std::to_string(block.listStartNumber > 0 ? block.listStartNumber : 1) + ". ")
                   << RunsToMarkdown(block.runs, false, &mediaPaths) << "\n";
                break;
            }
            case RichBlockType::CodeBlock: {
                blockSeparator();
                md << "```" << block.codeLanguage << "\n"
                   << ConcatenateRunText(block.runs) << "\n```\n";
                break;
            }
            case RichBlockType::BlockQuote: {
                blockSeparator();
                md << "> " << RunsToMarkdown(block.runs, false, &mediaPaths) << "\n";
                break;
            }
            case RichBlockType::Table: {
                blockSeparator();
                for (size_t r = 0; r < block.tableRows.size(); ++r) {
                    const auto& row = block.tableRows[r];
                    md << "|";
                    size_t columns = 0;
                    for (const auto& cell : row.cells) {
                        md << ' ' << RunsToMarkdown(cell.runs, true, &mediaPaths) << " |";
                        // Markdown has no column spans: a spanning cell is
                        // followed by empty cells so the grid stays aligned.
                        for (int span = 1; span < cell.columnSpan; ++span) md << " |";
                        columns += static_cast<size_t>(std::max(1, cell.columnSpan));
                    }
                    md << "\n";
                    if (r == 0) {
                        // Markdown tables require a header separator after row one.
                        md << "|";
                        for (size_t c = 0; c < columns; ++c) md << " --- |";
                        md << "\n";
                    }
                }
                break;
            }
            case RichBlockType::Image: {
                blockSeparator();
                std::string alt = block.imageAltText.empty() ? "image" : block.imageAltText;
                if (block.mediaIndex >= 0 && block.mediaIndex < static_cast<int>(mediaPaths.size())
                    && !mediaPaths[block.mediaIndex].empty()) {
                    md << "![" << alt << "](" << mediaPaths[block.mediaIndex] << ")\n";
                } else {
                    md << "[" << alt << "]\n";
                }
                break;
            }
            case RichBlockType::HorizontalRule:
            case RichBlockType::PageBreak: {
                blockSeparator();
                md << "---\n";
                break;
            }
            case RichBlockType::MathBlock: {
                // A `$$` fence pair: the Markdown renderer typesets the lines
                // between them as one centred display formula.
                blockSeparator();
                md << "$$\n" << ConcatenateRunText(block.runs) << "\n$$\n";
                break;
            }
            case RichBlockType::Paragraph:
            default: {
                blockSeparator();
                md << RunsToMarkdown(block.runs, false, &mediaPaths) << "\n";
                break;
            }
        }
    }
    // The notes, as footnote definitions after the body - footnotes, then
    // endnotes, each once, in the order of their first reference. A note of
    // several paragraphs continues indented.
    const std::vector<std::string> marks = NoteMarks();
    const std::vector<NoteReference> references = NoteReferences();
    std::vector<bool> written(notes.size(), false);
    bool firstNote = true;
    for (RichNote::Kind kind : {RichNote::Kind::Footnote, RichNote::Kind::Endnote}) {
        for (const NoteReference& reference : references) {
            const size_t index = static_cast<size_t>(reference.noteIndex);
            const RichNote& note = notes[index];
            if (note.kind != kind || written[index] || marks[index].empty()) continue;
            written[index] = true;
            md << (firstNote ? "\n" : "") << "[^" << marks[index] << "]: ";
            firstNote = false;
            bool firstParagraph = true;
            for (const RichDocBlock& noteBlock : note.blocks) {
                if (!firstParagraph) md << "\n    ";
                md << RunsToMarkdown(noteBlock.runs, false, &mediaPaths);
                firstParagraph = false;
            }
            md << "\n";
        }
    }
    return md.str();
}

// ===== MARKDOWN PARSER =====

UCRichDocument UCRichDocument::FromMarkdown(const std::string& markdown,
                                            const std::string& baseDirectory) {
    UCRichDocument doc;

    std::vector<std::string> lines;
    {
        std::string normalized;
        normalized.reserve(markdown.size());
        for (char c : markdown) {
            if (c != '\r') normalized.push_back(c);
        }
        size_t start = 0;
        while (start <= normalized.size()) {
            size_t nl = normalized.find('\n', start);
            if (nl == std::string::npos) {
                lines.push_back(normalized.substr(start));
                break;
            }
            lines.push_back(normalized.substr(start, nl - start));
            start = nl + 1;
        }
    }

    // Footnote definitions - "[^label]: text", continued by indented lines -
    // are taken out of the flow first; references anywhere then find them.
    std::vector<std::pair<std::string, std::vector<std::string>>> noteDefinitions;
    {
        std::vector<std::string> kept;
        for (size_t li = 0; li < lines.size(); ++li) {
            const std::string& line = lines[li];
            const size_t close = line.find("]:");
            if (line.rfind("[^", 0) == 0 && close != std::string::npos && close > 2
                && line.find(' ', 2) > close) {
                std::vector<std::string> paragraphs{line.substr(close + 2)};
                while (li + 1 < lines.size()
                       && (lines[li + 1].rfind("    ", 0) == 0 || lines[li + 1].rfind("\t", 0) == 0)) {
                    ++li;
                    const size_t text = lines[li].find_first_not_of(" \t");
                    paragraphs.push_back(text == std::string::npos ? std::string() : lines[li].substr(text));
                }
                for (std::string& paragraph : paragraphs) {
                    const size_t text = paragraph.find_first_not_of(" \t");
                    paragraph = text == std::string::npos ? std::string() : paragraph.substr(text);
                }
                noteDefinitions.emplace_back(line.substr(2, close - 2), std::move(paragraphs));
                continue;
            }
            kept.push_back(line);
        }
        lines = std::move(kept);
    }
    std::vector<std::string> noteLabels;
    for (const auto& definition : noteDefinitions) noteLabels.push_back(definition.first);
    struct LabelScope {
        explicit LabelScope(std::vector<std::string>* labels) { gMarkdownNoteLabels = labels; }
        ~LabelScope() { gMarkdownNoteLabels = nullptr; }
    } labelScope(&noteLabels);

    auto parseInlineToBlock = [](const std::string& text, RichDocBlock& block) {
        ParseInlineMarkdown(text, InlineStyleState{}, block.runs);
    };

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        std::string trimmed = line;
        {
            size_t b = trimmed.find_first_not_of(" \t");
            trimmed = (b == std::string::npos) ? std::string() : trimmed.substr(b);
        }
        if (trimmed.empty()) continue;

        // Display-math fence: `$$` alone on a line up to the next such line.
        if (trimmed == "$$") {
            RichDocBlock block;
            block.type = RichBlockType::MathBlock;
            bool firstLine = true;
            while (++li < lines.size()) {
                std::string mathLine = lines[li];
                size_t b = mathLine.find_first_not_of(" \t");
                if (b != std::string::npos && mathLine.substr(b) == "$$") break;
                RichTextRun run;
                run.text = mathLine;
                run.lineBreakBefore = !firstLine;
                firstLine = false;
                block.runs.push_back(run);
            }
            doc.blocks.push_back(std::move(block));
            continue;
        }

        // Fenced code block.
        if (trimmed.rfind("```", 0) == 0) {
            RichDocBlock block;
            block.type = RichBlockType::CodeBlock;
            block.codeLanguage = trimmed.substr(3);
            bool firstLine = true;
            while (++li < lines.size()) {
                std::string codeLine = lines[li];
                std::string codeTrimmed = codeLine;
                size_t b = codeTrimmed.find_first_not_of(" \t");
                if (b != std::string::npos && codeTrimmed.substr(b).rfind("```", 0) == 0) break;
                RichTextRun run;
                run.text = codeLine;
                run.code = true;
                run.lineBreakBefore = !firstLine;
                firstLine = false;
                block.runs.push_back(run);
            }
            doc.blocks.push_back(std::move(block));
            continue;
        }

        // ATX heading.
        if (trimmed[0] == '#') {
            size_t level = 0;
            while (level < trimmed.size() && trimmed[level] == '#' && level < 6) ++level;
            if (level < trimmed.size() && trimmed[level] == ' ') {
                RichDocBlock block;
                block.type = RichBlockType::Heading;
                block.headingLevel = static_cast<int>(level);
                parseInlineToBlock(trimmed.substr(level + 1), block);
                doc.blocks.push_back(std::move(block));
                continue;
            }
        }

        // Horizontal rule (checked before list markers so "---" is not a list).
        if (IsHorizontalRule(line)) {
            RichDocBlock block;
            block.type = RichBlockType::HorizontalRule;
            doc.blocks.push_back(std::move(block));
            continue;
        }

        // Block quote.
        if (trimmed[0] == '>') {
            RichDocBlock block;
            block.type = RichBlockType::BlockQuote;
            std::string content = trimmed.substr(1);
            if (!content.empty() && content[0] == ' ') content = content.substr(1);
            parseInlineToBlock(content, block);
            doc.blocks.push_back(std::move(block));
            continue;
        }

        // Standalone image line.
        {
            std::string alt, path;
            if (ParseStandaloneImage(line, alt, path)) {
                std::filesystem::path resolved(path);
                if (resolved.is_relative() && !baseDirectory.empty()) {
                    resolved = PathFromUtf8(baseDirectory) / resolved;
                }
                std::ifstream in(resolved, std::ios::binary);
                if (in.is_open()) {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                               std::istreambuf_iterator<char>());
                    RichDocBlock block;
                    block.type = RichBlockType::Image;
                    block.imageAltText = alt;
                    block.mediaIndex = doc.AddMedia(PathToUtf8(resolved.filename()),
                                                    MimeTypeForImageName(PathToUtf8(resolved)),
                                                    std::move(bytes));
                    int w = 0, h = 0;
                    if (SniffImagePixelSize(doc.media[block.mediaIndex].data, w, h)) {
                        // Screen pixels to points at the conventional 96 DPI.
                        block.imageWidthPt = static_cast<float>(w) * 72.0f / 96.0f;
                        block.imageHeightPt = static_cast<float>(h) * 72.0f / 96.0f;
                    }
                    doc.blocks.push_back(std::move(block));
                    continue;
                }
                // Unreadable image file: keep the raw markdown so nothing is lost.
                RichDocBlock block;
                block.type = RichBlockType::Paragraph;
                RichTextRun run;
                run.text = line;
                block.runs.push_back(run);
                doc.blocks.push_back(std::move(block));
                continue;
            }
        }

        // List item.
        {
            ListMarkerInfo info = ParseListMarker(line);
            if (info.isList) {
                RichDocBlock block;
                block.type = RichBlockType::ListItem;
                block.orderedList = info.ordered;
                block.listLevel = info.level;
                // GitHub task list: "- [ ] todo" / "- [x] done".
                std::string content = info.content;
                if (!info.ordered && content.size() >= 3 && content[0] == '['
                    && (content[1] == ' ' || content[1] == 'x' || content[1] == 'X') && content[2] == ']'
                    && (content.size() == 3 || content[3] == ' ')) {
                    block.checkbox = true;
                    block.checked = content[1] != ' ';
                    content.erase(0, std::min<size_t>(4, content.size()));
                }
                parseInlineToBlock(content, block);
                doc.blocks.push_back(std::move(block));
                continue;
            }
        }

        // Pipe table (requires a separator line right below the header).
        if (trimmed[0] == '|' && li + 1 < lines.size() && IsTableSeparatorLine(lines[li + 1])) {
            RichDocBlock block;
            block.type = RichBlockType::Table;
            RichTableRow headerRow;
            headerRow.header = true;
            for (const auto& cellText : SplitTableCells(trimmed)) {
                RichTableCell cell;
                ParseInlineMarkdown(cellText, InlineStyleState{}, cell.runs);
                headerRow.cells.push_back(std::move(cell));
            }
            block.tableRows.push_back(std::move(headerRow));
            ++li; // skip separator
            while (li + 1 < lines.size()) {
                std::string next = lines[li + 1];
                size_t b = next.find_first_not_of(" \t");
                if (b == std::string::npos || next[b] != '|') break;
                ++li;
                RichTableRow row;
                for (const auto& cellText : SplitTableCells(next.substr(b))) {
                    RichTableCell cell;
                    ParseInlineMarkdown(cellText, InlineStyleState{}, cell.runs);
                    row.cells.push_back(std::move(cell));
                }
                block.tableRows.push_back(std::move(row));
            }
            doc.blocks.push_back(std::move(block));
            continue;
        }

        // Plain paragraph (one source line = one paragraph, matching the
        // line-oriented editing model of the TextArea).
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        parseInlineToBlock(line, block);
        doc.blocks.push_back(std::move(block));
    }

    // One note per label, in the order labels were met; a definition's text
    // becomes its paragraphs (a reference inside a note stays text).
    gMarkdownNoteLabels = nullptr;
    for (const std::string& label : noteLabels) {
        RichNote note;
        for (const auto& [defined, paragraphs] : noteDefinitions) {
            if (defined != label) continue;
            for (const std::string& paragraph : paragraphs) {
                RichDocBlock block;
                ParseInlineMarkdown(paragraph, InlineStyleState{}, block.runs);
                note.blocks.push_back(std::move(block));
            }
            break;
        }
        if (note.blocks.empty()) note.blocks.emplace_back();
        doc.notes.push_back(std::move(note));
    }
    if (!doc.notes.empty()) doc.UpdateNoteMarks();
    return doc;
}

// ===== HTML PARSER =====

namespace {

std::vector<uint8_t> DecodeBase64Data(const std::string& in) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    unsigned int buffer = 0;
    int bits = 0;
    for (char c : in) {
        const int v = value(c);
        if (v < 0) continue;              // padding, whitespace
        buffer = (buffer << 6) | static_cast<unsigned int>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

void AppendCodePoint(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string LowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string TrimAscii(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// &amp; &#233; &#x2014; and the named entities pasted HTML commonly uses.
std::string DecodeEntities(const std::string& text) {
    static const std::map<std::string, uint32_t> named = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"copy", 0xA9},
        {"reg", 0xAE}, {"trade", 0x2122}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5},
        {"cent", 0xA2}, {"sect", 0xA7}, {"para", 0xB6}, {"deg", 0xB0}, {"middot", 0xB7},
        {"bull", 0x2022}, {"times", 0xD7}, {"divide", 0xF7}, {"plusmn", 0xB1}, {"shy", 0xAD},
        {"auml", 0xE4}, {"ouml", 0xF6}, {"uuml", 0xFC}, {"Auml", 0xC4}, {"Ouml", 0xD6},
        {"Uuml", 0xDC}, {"szlig", 0xDF}, {"eacute", 0xE9}, {"egrave", 0xE8}, {"agrave", 0xE0},
        {"ccedil", 0xE7}, {"ecirc", 0xEA}, {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA},
        {"ntilde", 0xF1}, {"aacute", 0xE1}, {"zwnj", 0x200C}, {"zwj", 0x200D}, {"ensp", 0x2002},
        {"emsp", 0x2003}, {"thinsp", 0x2009}, {"larr", 0x2190}, {"rarr", 0x2192}};
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out += '&';
            continue;
        }
        const std::string entity = text.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        bool ok = false;
        if (!entity.empty() && entity[0] == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            for (size_t k = hex ? 2 : 1; k < entity.size(); k++) {
                const char c = entity[k];
                int digit = -1;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (hex && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (digit < 0) { ok = false; break; }
                cp = cp * (hex ? 16u : 10u) + static_cast<uint32_t>(digit);
                ok = cp < 0x110000;
                if (!ok) break;
            }
        } else {
            auto found = named.find(entity);
            if (found != named.end()) {
                cp = found->second;
                ok = true;
            }
        }
        if (!ok) {
            out += '&';
            continue;
        }
        AppendCodePoint(out, cp);
        i = semi;
    }
    return out;
}

struct HtmlToken {
    enum class Kind { Text, Open, Close } kind = Kind::Text;
    std::string name;
    std::string text;                      // Text: raw character data
    std::map<std::string, std::string> attributes;
    bool selfClosing = false;
};

std::vector<HtmlToken> TokenizeHtml(const std::string& html) {
    std::vector<HtmlToken> tokens;
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            const size_t next = html.find('<', i);
            HtmlToken text;
            text.text = html.substr(i, next == std::string::npos ? std::string::npos : next - i);
            tokens.push_back(std::move(text));
            i = next == std::string::npos ? html.size() : next;
            continue;
        }
        if (html.compare(i, 4, "<!--") == 0) {
            const size_t end = html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
            // Doctype, processing instructions, Word's <![if ...]> markers.
            const size_t end = html.find('>', i);
            i = end == std::string::npos ? html.size() : end + 1;
            continue;
        }
        // A tag, up to its '>' (outside quoted attribute values).
        size_t j = i + 1;
        char quote = 0;
        for (; j < html.size(); j++) {
            if (quote) { if (html[j] == quote) quote = 0; continue; }
            if (html[j] == '"' || html[j] == '\'') quote = html[j];
            else if (html[j] == '>') break;
        }
        const std::string inner = html.substr(i + 1, j - i - 1);
        i = j < html.size() ? j + 1 : html.size();
        HtmlToken tag;
        size_t k = 0;
        if (!inner.empty() && inner[0] == '/') {
            tag.kind = HtmlToken::Kind::Close;
            k = 1;
        } else {
            tag.kind = HtmlToken::Kind::Open;
        }
        while (k < inner.size() && (std::isalnum(static_cast<unsigned char>(inner[k])) || inner[k] == ':' || inner[k] == '-')) {
            tag.name += static_cast<char>(std::tolower(static_cast<unsigned char>(inner[k])));
            k++;
        }
        if (tag.name.empty()) {
            // Not a tag after all ("a < b"): the text it was.
            HtmlToken text;
            text.text = "<" + inner + (j < html.size() ? ">" : "");
            tokens.push_back(std::move(text));
            continue;
        }
        tag.selfClosing = !inner.empty() && inner.back() == '/';
        // Attributes: name, name=value, name="value", name='value'.
        while (k < inner.size()) {
            while (k < inner.size() && (std::isspace(static_cast<unsigned char>(inner[k])) || inner[k] == '/')) k++;
            std::string name;
            while (k < inner.size() && !std::isspace(static_cast<unsigned char>(inner[k])) && inner[k] != '='
                   && inner[k] != '/' && inner[k] != '>') {
                name += static_cast<char>(std::tolower(static_cast<unsigned char>(inner[k])));
                k++;
            }
            while (k < inner.size() && std::isspace(static_cast<unsigned char>(inner[k]))) k++;
            std::string value;
            if (k < inner.size() && inner[k] == '=') {
                k++;
                while (k < inner.size() && std::isspace(static_cast<unsigned char>(inner[k]))) k++;
                if (k < inner.size() && (inner[k] == '"' || inner[k] == '\'')) {
                    const char q = inner[k++];
                    const size_t end = inner.find(q, k);
                    value = inner.substr(k, end == std::string::npos ? std::string::npos : end - k);
                    k = end == std::string::npos ? inner.size() : end + 1;
                } else {
                    while (k < inner.size() && !std::isspace(static_cast<unsigned char>(inner[k]))) value += inner[k++];
                }
            }
            if (!name.empty()) tag.attributes[name] = DecodeEntities(value);
        }
        // Whatever script, style and head hold is not text.
        if (tag.kind == HtmlToken::Kind::Open && !tag.selfClosing
            && (tag.name == "script" || tag.name == "style" || tag.name == "head" || tag.name == "title"
                || tag.name == "xml" || tag.name == "template")) {
            const std::string lower = LowerAscii(html.substr(i));
            const size_t end = lower.find("</" + tag.name);
            if (end == std::string::npos) break;
            i += end;
            const size_t close = html.find('>', i);
            i = close == std::string::npos ? html.size() : close + 1;
            continue;
        }
        tokens.push_back(std::move(tag));
    }
    return tokens;
}

// A CSS number with its unit, read without the process locale.
bool ParseCssLength(const std::string& value, float& number, std::string& unit) {
    size_t k = 0;
    bool negative = false;
    if (k < value.size() && (value[k] == '-' || value[k] == '+')) negative = value[k++] == '-';
    double whole = 0.0, fraction = 0.0, scale = 1.0;
    bool digits = false;
    while (k < value.size() && std::isdigit(static_cast<unsigned char>(value[k]))) {
        whole = whole * 10.0 + (value[k++] - '0');
        digits = true;
    }
    if (k < value.size() && value[k] == '.') {
        k++;
        while (k < value.size() && std::isdigit(static_cast<unsigned char>(value[k]))) {
            scale /= 10.0;
            fraction += (value[k++] - '0') * scale;
            digits = true;
        }
    }
    if (!digits) return false;
    number = static_cast<float>((whole + fraction) * (negative ? -1.0 : 1.0));
    unit = LowerAscii(TrimAscii(value.substr(k)));
    return true;
}

// "#rgb", "#rrggbb", "rgb(r, g, b)" and the basic colour names, as "#RRGGBB";
// "" for anything else (including "transparent" and "inherit").
std::string CssColor(std::string value) {
    value = LowerAscii(TrimAscii(value));
    static const std::map<std::string, std::string> names = {
        {"black", "#000000"}, {"white", "#FFFFFF"}, {"red", "#FF0000"}, {"green", "#008000"},
        {"blue", "#0000FF"}, {"yellow", "#FFFF00"}, {"gray", "#808080"}, {"grey", "#808080"},
        {"silver", "#C0C0C0"}, {"maroon", "#800000"}, {"purple", "#800080"}, {"fuchsia", "#FF00FF"},
        {"magenta", "#FF00FF"}, {"lime", "#00FF00"}, {"olive", "#808000"}, {"navy", "#000080"},
        {"teal", "#008080"}, {"aqua", "#00FFFF"}, {"cyan", "#00FFFF"}, {"orange", "#FFA500"}};
    auto named = names.find(value);
    if (named != names.end()) return named->second;
    auto hexDigit = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
    if (!value.empty() && value[0] == '#') {
        std::string hex = value.substr(1);
        if (hex.size() == 3 && hexDigit(hex[0]) && hexDigit(hex[1]) && hexDigit(hex[2])) {
            hex = std::string{hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        }
        if (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), hexDigit)) return "";
        for (char& c : hex) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return "#" + hex;
    }
    if (value.rfind("rgb", 0) == 0) {
        const size_t open = value.find('('), close = value.find(')');
        if (open == std::string::npos || close == std::string::npos) return "";
        std::string inner = value.substr(open + 1, close - open - 1);
        int channels[3] = {0, 0, 0};
        int count = 0;
        size_t start = 0;
        while (count < 3 && start <= inner.size()) {
            const size_t comma = inner.find_first_of(", ", start);
            const std::string part = TrimAscii(inner.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!part.empty()) {
                float number = 0.0f;
                std::string unit;
                if (!ParseCssLength(part, number, unit)) return "";
                if (unit == "%") number = number * 255.0f / 100.0f;
                channels[count++] = std::clamp(static_cast<int>(number + 0.5f), 0, 255);
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (count < 3) return "";
        static const char* digits = "0123456789ABCDEF";
        std::string out = "#";
        for (int c : channels) {
            out += digits[c >> 4];
            out += digits[c & 15];
        }
        return out;
    }
    return "";
}

struct HtmlFormat {
    bool bold = false, italic = false, underline = false, strike = false, code = false;
    bool subscript = false, superscript = false;
    std::string color, highlight, fontFamily, link;
    float fontSizePt = 0.0f;
};

// The declarations of a style attribute that set character formatting or a
// paragraph's alignment.
void ApplyCss(const std::string& css, HtmlFormat& format, RichTextAlign* align) {
    size_t start = 0;
    while (start < css.size()) {
        const size_t semi = css.find(';', start);
        const std::string declaration = css.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        start = semi == std::string::npos ? css.size() : semi + 1;
        const size_t colon = declaration.find(':');
        if (colon == std::string::npos) continue;
        const std::string property = LowerAscii(TrimAscii(declaration.substr(0, colon)));
        std::string value = TrimAscii(declaration.substr(colon + 1));
        const std::string lower = LowerAscii(value);
        if (lower.find("!important") != std::string::npos) value = TrimAscii(value.substr(0, lower.find("!important")));
        if (property == "font-weight") {
            float weight = 0.0f;
            std::string unit;
            if (lower == "bold" || lower == "bolder") format.bold = true;
            else if (lower == "normal" || lower == "lighter") format.bold = false;
            else if (ParseCssLength(lower, weight, unit)) format.bold = weight >= 600.0f;
        } else if (property == "font-style") {
            format.italic = lower == "italic" || lower == "oblique";
        } else if (property == "text-decoration" || property == "text-decoration-line") {
            if (lower.find("underline") != std::string::npos) format.underline = true;
            if (lower.find("line-through") != std::string::npos) format.strike = true;
            if (lower == "none") format.underline = format.strike = false;
        } else if (property == "color") {
            const std::string color = CssColor(value);
            if (!color.empty()) format.color = color == "#000000" ? "" : color;
        } else if (property == "background-color" || property == "background" || property == "mso-highlight") {
            const std::string color = CssColor(value);
            if (!color.empty() && color != "#FFFFFF") format.highlight = color;
        } else if (property == "font-family") {
            std::string family = value.substr(0, value.find(','));
            family.erase(std::remove(family.begin(), family.end(), '"'), family.end());
            family.erase(std::remove(family.begin(), family.end(), '\''), family.end());
            family = TrimAscii(family);
            const std::string generic = LowerAscii(family);
            if (generic == "monospace") format.code = true;
            else if (generic != "serif" && generic != "sans-serif" && generic != "inherit" && !family.empty()) {
                format.fontFamily = family;
            }
        } else if (property == "font-size") {
            float size = 0.0f;
            std::string unit;
            if (ParseCssLength(lower, size, unit)) {
                if (unit == "pt") format.fontSizePt = size;
                else if (unit == "px") format.fontSizePt = size * 0.75f;
            }
        } else if (property == "vertical-align") {
            if (lower == "super") format.superscript = true;
            else if (lower == "sub") format.subscript = true;
        } else if (property == "text-align" && align) {
            if (lower == "center") *align = RichTextAlign::Center;
            else if (lower == "right" || lower == "end") *align = RichTextAlign::Right;
            else if (lower == "justify") *align = RichTextAlign::Justify;
            else if (lower == "left" || lower == "start") *align = RichTextAlign::Default;
        }
    }
}

class HtmlReader {
public:
    explicit HtmlReader(UCRichDocument& doc) : doc_(doc) {}

    void Read(const std::string& html) {
        formats_.push_back(HtmlFormat{});
        for (const HtmlToken& token : TokenizeHtml(html)) {
            if (token.kind == HtmlToken::Kind::Text) Text(token.text);
            else if (token.kind == HtmlToken::Kind::Open) Open(token);
            else Close(token.name);
        }
        while (!tables_.empty()) EndTable();
        Flush();
    }

private:
    UCRichDocument& doc_;
    std::vector<HtmlFormat> formats_;
    std::vector<std::string> formatTags_;          // the element each pushed format belongs to
    RichDocBlock block_;
    bool blockOpen_ = false;
    bool pre_ = false;
    bool lastSpace_ = true;                          // collapse leading / repeated white space
    bool pendingBreak_ = false;
    int quoteDepth_ = 0;
    std::vector<bool> lists_;                        // ordered?
    struct Table {
        RichDocBlock block;
        bool inCell = false;
        bool inHead = false;
    };
    std::vector<Table> tables_;
    RichTextAlign align_ = RichTextAlign::Default;

    static bool IsBlockTag(const std::string& name) {
        static const char* tags[] = {"p", "div", "section", "article", "header", "footer", "main", "aside",
                                     "nav", "center", "address", "figure", "figcaption", "dt", "dd", "dl",
                                     "form", "fieldset", "details", "summary", "body", "html"};
        for (const char* tag : tags) if (name == tag) return true;
        return false;
    }

    static int HeadingLevel(const std::string& name) {
        return name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6' ? name[1] - '0' : 0;
    }

    RichTableCell* CurrentCell() {
        if (tables_.empty() || !tables_.back().inCell) return nullptr;
        auto& rows = tables_.back().block.tableRows;
        if (rows.empty() || rows.back().cells.empty()) return nullptr;
        return &rows.back().cells.back();
    }

    std::vector<RichTextRun>& Runs() {
        if (RichTableCell* cell = CurrentCell()) return cell->runs;
        if (!blockOpen_) StartBlock(DefaultBlockType());
        return block_.runs;
    }

    RichBlockType DefaultBlockType() const {
        if (!lists_.empty()) return RichBlockType::ListItem;
        return quoteDepth_ > 0 ? RichBlockType::BlockQuote : RichBlockType::Paragraph;
    }

    void StartBlock(RichBlockType type) {
        Flush();
        block_ = RichDocBlock{};
        block_.type = type;
        block_.align = align_;
        if (type == RichBlockType::ListItem) {
            block_.listLevel = std::max(0, static_cast<int>(lists_.size()) - 1);
            block_.orderedList = !lists_.empty() && lists_.back();
        }
        blockOpen_ = true;
        lastSpace_ = true;
        pendingBreak_ = false;
    }

    void Flush() {
        if (!blockOpen_) return;
        blockOpen_ = false;
        // Trailing white space of the last line goes.
        while (!block_.runs.empty()) {
            std::string& text = block_.runs.back().text;
            if (block_.runs.back().IsInlineImage()) break;
            const size_t end = text.find_last_not_of(' ');
            if (end == std::string::npos && !block_.runs.back().lineBreakBefore) {
                block_.runs.pop_back();
                continue;
            }
            text.erase(end == std::string::npos ? 0 : end + 1);
            break;
        }
        const bool empty = block_.runs.empty();
        if (empty && block_.type != RichBlockType::HorizontalRule) return;
        doc_.blocks.push_back(std::move(block_));
        block_ = RichDocBlock{};
    }

    RichTextRun RunWithFormat() const {
        const HtmlFormat& f = formats_.back();
        RichTextRun run;
        run.bold = f.bold;
        run.italic = f.italic;
        run.underline = f.underline;
        run.strikethrough = f.strike;
        run.code = f.code;
        run.subscript = f.subscript;
        run.superscript = f.superscript;
        run.color = f.color;
        run.highlightColor = f.highlight;
        run.fontFamily = f.fontFamily;
        run.fontSizePt = f.fontSizePt;
        run.linkTarget = f.link;
        return run;
    }

    void Text(const std::string& raw) {
        std::string text;
        if (pre_) {
            text = raw;
        } else {
            // White space collapses; a space at the start of a line goes.
            for (char c : raw) {
                const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
                if (space) {
                    if (!lastSpace_) text += ' ';
                    lastSpace_ = true;
                } else {
                    text += c;
                    lastSpace_ = false;
                }
            }
        }
        if (text.empty()) return;
        text = DecodeEntities(text);
        // A no-break space is a space here.
        for (size_t at; (at = text.find("\xC2\xA0")) != std::string::npos;) text.replace(at, 2, " ");
        if (text.find_first_not_of(' ') == std::string::npos && Runs().empty() && !pendingBreak_) return;
        std::vector<RichTextRun>& runs = Runs();
        // Preformatted text: its line feeds are line breaks.
        size_t start = 0;
        while (start <= text.size()) {
            const size_t newline = pre_ ? text.find('\n', start) : std::string::npos;
            std::string piece = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
            if (!piece.empty() && piece.back() == '\r') piece.pop_back();
            if (!piece.empty() || pendingBreak_) {
                RichTextRun run = RunWithFormat();
                run.text = piece;
                run.lineBreakBefore = pendingBreak_ && !runs.empty();
                pendingBreak_ = false;
                runs.push_back(std::move(run));
            }
            if (newline == std::string::npos) break;
            pendingBreak_ = true;
            start = newline + 1;
        }
    }

    void PushFormat(const std::string& tag, const HtmlFormat& format) {
        formats_.push_back(format);
        formatTags_.push_back(tag);
    }

    void Open(const HtmlToken& token) {
        const std::string& name = token.name;
        auto attribute = [&](const char* key) -> std::string {
            auto found = token.attributes.find(key);
            return found == token.attributes.end() ? "" : found->second;
        };
        RichTextAlign align = align_;
        const std::string dirAttribute = LowerAscii(attribute("dir"));
        const std::string alignAttribute = LowerAscii(attribute("align"));
        if (alignAttribute == "center") align = RichTextAlign::Center;
        else if (alignAttribute == "right") align = RichTextAlign::Right;
        else if (alignAttribute == "justify") align = RichTextAlign::Justify;
        HtmlFormat format = formats_.back();
        const std::string style = attribute("style");
        // Word marks its list numbers and other layout-only text this way.
        const bool ignored = LowerAscii(style).find("mso-list:ignore") != std::string::npos
                          || LowerAscii(style).find("display:none") != std::string::npos;
        ApplyCss(style, format, &align);

        if (name == "br") {
            if (RichTableCell* cell = CurrentCell()) {
                if (!cell->runs.empty()) pendingBreak_ = true;
            } else if (blockOpen_) {
                pendingBreak_ = true;
            }
            lastSpace_ = true;
            return;
        }
        if (name == "hr") {
            Flush();
            RichDocBlock rule;
            rule.type = RichBlockType::HorizontalRule;
            doc_.blocks.push_back(std::move(rule));
            return;
        }
        if (name == "img") {
            Image(token);
            return;
        }
        if (name == "table") {
            Flush();
            Table table;
            table.block.type = RichBlockType::Table;
            tables_.push_back(std::move(table));
            return;
        }
        if (!tables_.empty()) {
            Table& table = tables_.back();
            if (name == "thead") { table.inHead = true; return; }
            if (name == "tr") {
                table.block.tableRows.emplace_back();
                table.block.tableRows.back().header = table.inHead;
                table.inCell = false;
                return;
            }
            if (name == "td" || name == "th") {
                if (table.block.tableRows.empty()) table.block.tableRows.emplace_back();
                RichTableCell cell;
                const std::string span = attribute("colspan"), rows = attribute("rowspan");
                if (!span.empty() && std::isdigit(static_cast<unsigned char>(span[0]))) cell.columnSpan = std::clamp(std::atoi(span.c_str()), 1, 64);
                if (!rows.empty() && std::isdigit(static_cast<unsigned char>(rows[0]))) cell.rowSpan = std::clamp(std::atoi(rows.c_str()), 1, 1000);
                cell.align = align;
                table.block.tableRows.back().cells.push_back(std::move(cell));
                table.inCell = true;
                lastSpace_ = true;
                pendingBreak_ = false;
                HtmlFormat cellFormat = format;
                if (name == "th") cellFormat.bold = true;
                PushFormat(name, cellFormat);
                return;
            }
        }
        const int heading = HeadingLevel(name);
        if (heading > 0 || IsBlockTag(name) || name == "li" || name == "pre" || name == "blockquote"
            || name == "ul" || name == "ol") {
            if (RichTableCell* cell = CurrentCell()) {
                // Paragraphs inside a cell: lines of the cell.
                if (!cell->runs.empty()) pendingBreak_ = true;
                lastSpace_ = true;
                PushFormat(name, format);
                return;
            }
            if (name == "ul" || name == "ol") {
                Flush();
                lists_.push_back(name == "ol");
                PushFormat(name, format);
                return;
            }
            if (name == "blockquote") {
                Flush();
                quoteDepth_++;
                PushFormat(name, format);
                return;
            }
            if (name == "pre") {
                StartBlock(RichBlockType::CodeBlock);
                pre_ = true;
                format.code = false;              // the code block is monospaced already
                PushFormat(name, format);
                return;
            }
            if (name == "li") {
                align_ = align;
                StartBlock(RichBlockType::ListItem);
                PushFormat(name, format);
                return;
            }
            if (heading > 0) {
                align_ = align;
                StartBlock(RichBlockType::Heading);
                block_.headingLevel = heading;
                block_.rightToLeft = dirAttribute == "rtl";
                format.bold = false;              // a heading is bold by being one
                PushFormat(name, format);
                return;
            }
            // A paragraph inside a list item continues the item.
            if (!(blockOpen_ && block_.type == RichBlockType::ListItem && block_.runs.empty())) {
                const RichTextAlign saved = align_;
                align_ = align;
                if (name != "body" && name != "html") StartBlock(DefaultBlockType());
                align_ = saved;
                if (name != "body" && name != "html") {
                    block_.align = align;
                    block_.rightToLeft = dirAttribute == "rtl";
                }
            }
            PushFormat(name, format);
            return;
        }
        if (token.selfClosing) return;
        // Inline elements.
        if (name == "b" || name == "strong") format.bold = true;
        else if (name == "i" || name == "em" || name == "cite" || name == "dfn" || name == "var") format.italic = true;
        else if (name == "u" || name == "ins") format.underline = true;
        else if (name == "s" || name == "strike" || name == "del") format.strike = true;
        else if (name == "code" || name == "tt" || name == "kbd" || name == "samp") format.code = true;
        else if (name == "sub") format.subscript = true;
        else if (name == "sup") format.superscript = true;
        else if (name == "mark") format.highlight = "#FFFF00";
        else if (name == "a") {
            const std::string href = attribute("href");
            if (!href.empty() && LowerAscii(href).rfind("javascript:", 0) != 0) format.link = href;
        } else if (name == "font") {
            const std::string color = CssColor(attribute("color"));
            if (!color.empty()) format.color = color;
            const std::string face = attribute("face");
            if (!face.empty()) format.fontFamily = TrimAscii(face.substr(0, face.find(',')));
        }
        if (ignored) format.color = "\x01";         // marks text to drop
        PushFormat(name, format);
    }

    void Close(const std::string& name) {
        // Pop back to the matching element (tolerating unclosed inner ones).
        auto it = std::find(formatTags_.rbegin(), formatTags_.rend(), name);
        if (it != formatTags_.rend()) {
            const size_t keep = static_cast<size_t>(formatTags_.rend() - it) - 1;
            formatTags_.resize(keep);
            formats_.resize(keep + 1);
        }
        if (name == "table") {
            if (!tables_.empty()) EndTable();
            return;
        }
        if (!tables_.empty()) {
            if (name == "thead") tables_.back().inHead = false;
            if (name == "td" || name == "th") tables_.back().inCell = false;
            if (name == "td" || name == "th" || name == "tr" || name == "thead" || name == "tbody") return;
            if (tables_.back().inCell) return;       // block ends inside a cell: nothing to do
        }
        if (name == "ul" || name == "ol") {
            Flush();
            if (!lists_.empty()) lists_.pop_back();
        } else if (name == "blockquote") {
            Flush();
            quoteDepth_ = std::max(0, quoteDepth_ - 1);
        } else if (name == "pre") {
            Flush();
            pre_ = false;
        } else if (name == "li" || HeadingLevel(name) > 0 || IsBlockTag(name)) {
            if (name != "body" && name != "html") Flush();
            align_ = RichTextAlign::Default;
        }
    }

    void EndTable() {
        Table table = std::move(tables_.back());
        tables_.pop_back();
        // Rows without cells go; a header row is one whose cells are all th.
        auto& rows = table.block.tableRows;
        rows.erase(std::remove_if(rows.begin(), rows.end(), [](const RichTableRow& r) { return r.cells.empty(); }),
                   rows.end());
        if (rows.empty()) return;
        if (RichTableCell* outer = CurrentCell()) {
            // A table inside a cell: its text, row by row.
            for (const RichTableRow& row : rows) {
                for (const RichTableCell& cell : row.cells) {
                    for (RichTextRun run : cell.runs) outer->runs.push_back(std::move(run));
                    RichTextRun gap;
                    gap.text = " ";
                    outer->runs.push_back(gap);
                }
                pendingBreak_ = true;
            }
            return;
        }
        Flush();
        doc_.blocks.push_back(std::move(table.block));
    }

    void Image(const HtmlToken& token) {
        auto attribute = [&](const char* key) -> std::string {
            auto found = token.attributes.find(key);
            return found == token.attributes.end() ? "" : found->second;
        };
        const std::string src = attribute("src");
        if (LowerAscii(src).rfind("data:", 0) != 0) return;   // only pictures carried along
        const size_t comma = src.find(',');
        const size_t semicolon = src.find(';');
        if (comma == std::string::npos) return;
        const std::string mime = LowerAscii(src.substr(5, (semicolon < comma ? semicolon : comma) - 5));
        if (mime.rfind("image/", 0) != 0 || src.find(";base64", 0) == std::string::npos) return;
        std::vector<uint8_t> data = DecodeBase64Data(src.substr(comma + 1));
        if (data.empty()) return;
        const std::string extension = mime.substr(6) == "jpeg" ? "jpg" : mime.substr(6);
        const int media = doc_.AddMedia("pasted." + extension, mime, std::move(data));
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        run.mediaIndex = media;
        run.imageAltText = attribute("alt");
        float value = 0.0f;
        std::string unit;
        if (ParseCssLength(attribute("width"), value, unit)) run.imageWidthPt = value * 0.75f;
        if (ParseCssLength(attribute("height"), value, unit)) run.imageHeightPt = value * 0.75f;
        Runs().push_back(std::move(run));
        lastSpace_ = false;
    }
};

} // namespace

UCRichDocument UCRichDocument::FromHTML(const std::string& html) {
    UCRichDocument doc;
    HtmlReader reader(doc);
    reader.Read(html);
    // Text Word marked as layout only (its list numbers) is dropped.
    auto clean = [](std::vector<RichTextRun>& runs) {
        runs.erase(std::remove_if(runs.begin(), runs.end(), [](const RichTextRun& r) { return r.color == "\x01"; }),
                   runs.end());
        if (!runs.empty()) {
            runs.front().lineBreakBefore = false;
            const size_t lead = runs.front().text.find_first_not_of(' ');
            if (!runs.front().IsInlineImage()) runs.front().text.erase(0, lead == std::string::npos ? runs.front().text.size() : lead);
        }
    };
    for (RichDocBlock& block : doc.blocks) {
        clean(block.runs);
        for (RichTableRow& row : block.tableRows) {
            for (RichTableCell& cell : row.cells) clean(cell.runs);
        }
    }
    doc.blocks.erase(std::remove_if(doc.blocks.begin(), doc.blocks.end(), [](const RichDocBlock& b) {
                         return b.type == RichBlockType::Paragraph && b.runs.empty();
                     }), doc.blocks.end());
    return doc;
}

// ===== HTML SERIALIZER =====

namespace {

std::string RunsToHtml(const std::vector<RichTextRun>& runs,
                       const std::vector<RichDocMedia>* media = nullptr) {
    std::string out;
    for (const auto& run : MergeAdjacentRuns(runs)) {
        if (run.lineBreakBefore && !out.empty()) out += "<br/>";
        if (run.IsDeleted()) continue;
        if (run.IsNoteReference()) {
            const std::string mark = EscapeHtml(run.text);
            out += "<sup><a href=\"#note-" + mark + "\">" + mark + "</a></sup>";
            continue;
        }
        if (run.IsInlineImage()) {
            const std::string alt = EscapeHtml(run.imageAltText);
            if (media && run.mediaIndex >= 0
                && run.mediaIndex < static_cast<int>(media->size())) {
                const RichDocMedia& m = (*media)[static_cast<size_t>(run.mediaIndex)];
                // A floating picture floats in HTML too, at its side.
                std::string floatStyle;
                if (run.IsFloatingImage() && run.imageWrap == RichTextRun::ImageWrap::Square) {
                    floatStyle = run.imageFloatAlign == RichTextAlign::Right
                        ? " style=\"float:right;margin:0 0 0.5em 1em\""
                        : " style=\"float:left;margin:0 1em 0.5em 0\"";
                } else if (run.IsFloatingImage() && run.imageWrap == RichTextRun::ImageWrap::TopAndBottom) {
                    floatStyle = " style=\"display:block\"";
                }
                out += "<img alt=\"" + alt + "\"" + floatStyle + " src=\"data:" + m.mimeType
                     + ";base64," + Base64Encode(m.data) + "\"/>";
            } else {
                out += "[" + alt + "]";
            }
            continue;
        }
        std::string body = EscapeHtml(run.text);
        if (run.math) body = "<span class=\"math\">$" + body + "$</span>";
        if (run.code) body = "<code>" + body + "</code>";
        if (run.bold) body = "<b>" + body + "</b>";
        if (run.italic) body = "<i>" + body + "</i>";
        if (run.underline) body = "<u>" + body + "</u>";
        if (run.strikethrough) body = "<s>" + body + "</s>";
        if (run.subscript) body = "<sub>" + body + "</sub>";
        if (run.superscript) body = "<sup>" + body + "</sup>";
        std::string style;
        if (!run.color.empty()) style += "color:" + run.color + ";";
        if (!run.highlightColor.empty()) style += "background-color:" + run.highlightColor + ";";
        if (!run.fontFamily.empty()) style += "font-family:'" + run.fontFamily + "';";
        if (run.fontSizePt > 0) style += "font-size:" + std::to_string(run.fontSizePt) + "pt;";
        if (!style.empty()) body = "<span style=\"" + style + "\">" + body + "</span>";
        if (!run.linkTarget.empty()) {
            body = "<a href=\"" + EscapeHtml(run.linkTarget) + "\">" + body + "</a>";
        }
        out += body;
    }
    return out;
}

// border-* and background-color declarations for a cell's document frame.
// Numbers go out dot-decimal whatever the process locale.
std::string CellFrameCss(const RichTableCell& cell) {
    std::ostringstream css;
    css.imbue(std::locale::classic());
    auto side = [&](const char* name, const RichBorder& border) {
        css << "border-" << name << ":";
        if (border.IsVisible()) {
            // CSS names the four line styles as ODF does.
            css << border.widthPt << "pt " << RichBorderStyleOdfName(border.style) << " " << (border.color.empty() ? "#000000" : border.color) << ";";
        } else {
            css << "none;";
        }
    };
    side("top", cell.borderTop);
    side("bottom", cell.borderBottom);
    side("left", cell.borderLeft);
    side("right", cell.borderRight);
    if (!cell.backgroundColor.empty()) css << "background-color:" << cell.backgroundColor << ";";
    return css.str();
}

const char* AlignCss(RichTextAlign align) {
    switch (align) {
        case RichTextAlign::Center: return "center";
        case RichTextAlign::Right: return "right";
        case RichTextAlign::Justify: return "justify";
        default: return nullptr;
    }
}

} // namespace

std::string UCRichDocument::ToHTML() const {
    if (!FurnitureForPage(0).IsEmpty()) return WithFirstPageFurnitureInline().ToHTML();
    std::ostringstream html;
    int openListLevel = -1;   // -1 = no list open
    std::vector<bool> listOrderedStack;

    auto closeListsTo = [&](int level) {
        while (openListLevel > level) {
            html << (listOrderedStack.back() ? "</ol>\n" : "</ul>\n");
            listOrderedStack.pop_back();
            --openListLevel;
        }
    };

    for (const auto& block : blocks) {
        if (block.type != RichBlockType::ListItem) closeListsTo(-1);

        switch (block.type) {
            case RichBlockType::Heading: {
                int level = std::clamp(block.headingLevel, 1, 6);
                html << "<h" << level << (block.rightToLeft ? " dir=\"rtl\"" : "") << ">" << RunsToHtml(block.runs, &media)
                     << "</h" << level << ">\n";
                break;
            }
            case RichBlockType::ListItem: {
                closeListsTo(block.listLevel);
                // A kind change (ul <-> ol) at the same level starts a new list.
                if (openListLevel == block.listLevel && !listOrderedStack.empty()
                    && listOrderedStack.back() != block.orderedList) {
                    closeListsTo(block.listLevel - 1);
                }
                while (openListLevel < block.listLevel) {
                    if (!block.orderedList) {
                        html << "<ul>\n";
                    } else {
                        // HTML spells letters and Roman numerals itself.
                        const char* type = block.numberFormat == RichNumberFormat::LowerLetter ? "a"
                                         : block.numberFormat == RichNumberFormat::UpperLetter ? "A"
                                         : block.numberFormat == RichNumberFormat::LowerRoman ? "i"
                                         : block.numberFormat == RichNumberFormat::UpperRoman ? "I" : nullptr;
                        html << (type ? std::string("<ol type=\"") + type + "\">\n" : std::string("<ol>\n"));
                    }
                    listOrderedStack.push_back(block.orderedList);
                    ++openListLevel;
                }
                // <li value> carries a number the item holds itself, which
                // also keeps a list running on after an interruption.
                if (block.orderedList && block.listStartNumber > 0) {
                    html << "<li value=\"" << block.listStartNumber << "\">";
                } else {
                    html << "<li>";
                }
                if (block.checkbox) {
                    html << (block.checked ? "<input type=\"checkbox\" disabled checked> "
                                           : "<input type=\"checkbox\" disabled> ");
                }
                html << RunsToHtml(block.runs, &media) << "</li>\n";
                break;
            }
            case RichBlockType::CodeBlock:
                html << "<pre><code>" << EscapeHtml(ConcatenateRunText(block.runs))
                     << "</code></pre>\n";
                break;
            case RichBlockType::BlockQuote:
                html << "<blockquote><p>" << RunsToHtml(block.runs, &media) << "</p></blockquote>\n";
                break;
            case RichBlockType::Table: {
                // A document's own frames become CSS on the cells; otherwise
                // the plain bordered table.
                html << (block.tableBordersFromDocument
                             ? "<table style=\"border-collapse:collapse\">\n" : "<table border=\"1\">\n");
                for (const auto& row : block.tableRows) {
                    const char* tag = row.header ? "th" : "td";
                    html << "<tr>";
                    for (const auto& cell : row.cells) {
                        html << "<" << tag;
                        if (cell.columnSpan > 1) html << " colspan=\"" << cell.columnSpan << "\"";
                        if (cell.rowSpan > 1) html << " rowspan=\"" << cell.rowSpan << "\"";
                        std::string css;
                        if (const char* alignCss = AlignCss(cell.align)) css += std::string("text-align:") + alignCss + ";";
                        if (block.tableBordersFromDocument) css += CellFrameCss(cell);
                        if (!css.empty()) html << " style=\"" << EscapeHtml(css) << "\"";
                        html << ">" << RunsToHtml(cell.runs, &media) << "</" << tag << ">";
                    }
                    html << "</tr>\n";
                }
                html << "</table>\n";
                break;
            }
            case RichBlockType::Image: {
                if (block.mediaIndex >= 0 && block.mediaIndex < static_cast<int>(media.size())) {
                    const RichDocMedia& m = media[block.mediaIndex];
                    html << "<p><img alt=\"" << EscapeHtml(block.imageAltText)
                         << "\" src=\"data:" << m.mimeType << ";base64,"
                         << Base64Encode(m.data) << "\"/></p>\n";
                } else {
                    html << "<p>[" << EscapeHtml(block.imageAltText) << "]</p>\n";
                }
                break;
            }
            case RichBlockType::HorizontalRule:
            case RichBlockType::PageBreak:
                html << "<hr/>\n";
                break;
            case RichBlockType::MathBlock:
                html << "<p class=\"math\" style=\"text-align:center\">$$"
                     << EscapeHtml(ConcatenateRunText(block.runs)) << "$$</p>\n";
                break;
            case RichBlockType::Paragraph:
            default: {
                std::string css;
                if (const char* alignCss = AlignCss(block.align)) css += std::string("text-align:") + alignCss + ";";
                if (block.HasParagraphFrame()) {
                    // Same declarations as a cell frame.
                    RichTableCell frame;
                    frame.borderTop = block.paragraphBorderTop;
                    frame.borderBottom = block.paragraphBorderBottom;
                    frame.borderLeft = block.paragraphBorderLeft;
                    frame.borderRight = block.paragraphBorderRight;
                    frame.backgroundColor = block.paragraphBackground;
                    css += CellFrameCss(frame);
                }
                const char* dir = block.rightToLeft ? " dir=\"rtl\"" : "";
                if (!css.empty()) html << "<p" << dir << " style=\"" << EscapeHtml(css) << "\">";
                else html << "<p" << dir << ">";
                html << RunsToHtml(block.runs, &media) << "</p>\n";
                break;
            }
        }
    }
    closeListsTo(-1);
    // The notes after the body, each with the mark its references link to.
    const std::vector<std::string> marks = NoteMarks();
    std::vector<bool> written(notes.size(), false);
    bool opened = false;
    for (RichNote::Kind kind : {RichNote::Kind::Footnote, RichNote::Kind::Endnote}) {
        for (const NoteReference& reference : NoteReferences()) {
            const size_t index = static_cast<size_t>(reference.noteIndex);
            if (notes[index].kind != kind || written[index] || marks[index].empty()) continue;
            written[index] = true;
            if (!opened) {
                html << "<section class=\"notes\"><hr/>\n";
                opened = true;
            }
            const std::string mark = EscapeHtml(marks[index]);
            html << "<div id=\"note-" << mark << "\"><sup>" << mark << "</sup> ";
            for (const RichDocBlock& noteBlock : notes[index].blocks) {
                html << "<p>" << RunsToHtml(noteBlock.runs, &media) << "</p>";
            }
            html << "</div>\n";
        }
    }
    if (opened) html << "</section>\n";
    return html.str();
}

// ===== PLAIN TEXT SERIALIZER =====

std::string UCRichDocument::ToPlainText() const {
    if (!FurnitureForPage(0).IsEmpty()) return WithFirstPageFurnitureInline().ToPlainText();
    std::ostringstream text;
    bool first = true;
    for (const auto& block : blocks) {
        if (!first) text << "\n";
        first = false;
        switch (block.type) {
            case RichBlockType::Table:
                for (const auto& row : block.tableRows) {
                    bool firstCell = true;
                    for (const auto& cell : row.cells) {
                        if (!firstCell) text << "\t";
                        firstCell = false;
                        text << RunsToReadableText(cell.runs);
                    }
                    text << "\n";
                }
                break;
            case RichBlockType::Image:
                text << "[" << (block.imageAltText.empty() ? "image" : block.imageAltText) << "]\n";
                break;
            case RichBlockType::HorizontalRule:
            case RichBlockType::PageBreak:
                text << "----------\n";
                break;
            default:
                if (block.checkbox) text << (block.checked ? "[x] " : "[ ] ");
                text << RunsToReadableText(block.runs) << "\n";
                break;
        }
    }
    // The notes after a rule: "[1] text".
    const std::vector<std::string> marks = NoteMarks();
    std::vector<bool> written(notes.size(), false);
    bool ruled = false;
    for (RichNote::Kind kind : {RichNote::Kind::Footnote, RichNote::Kind::Endnote}) {
        for (const NoteReference& reference : NoteReferences()) {
            const size_t index = static_cast<size_t>(reference.noteIndex);
            if (notes[index].kind != kind || written[index] || marks[index].empty()) continue;
            written[index] = true;
            if (!ruled) {
                text << "\n----------\n";
                ruled = true;
            }
            text << "[" << marks[index] << "] ";
            bool firstParagraph = true;
            for (const RichDocBlock& noteBlock : notes[index].blocks) {
                if (!firstParagraph) text << "\n    ";
                text << RunsToReadableText(noteBlock.runs);
                firstParagraph = false;
            }
            text << "\n";
        }
    }
    return text.str();
}

// ===== LIST NUMBERING =====

int RichDocOrderedItemNumber(const std::vector<RichDocBlock>& blocks, size_t index) {
    if (index >= blocks.size()) return 0;
    const RichDocBlock& block = blocks[index];
    if (block.type != RichBlockType::ListItem || !block.orderedList) return 0;
    if (block.listStartNumber > 0) return block.listStartNumber;
    int number = 1;
    for (size_t i = index; i-- > 0;) {
        const RichDocBlock& previous = blocks[i];
        if (previous.type != RichBlockType::ListItem) break;
        if (previous.listLevel < block.listLevel) break;
        if (previous.listLevel > block.listLevel) continue;
        if (previous.orderedList != block.orderedList) break;
        if (previous.listStartNumber > 0) return previous.listStartNumber + number;
        number++;
    }
    return number;
}

// ===== NOTES =====

std::vector<UCRichDocument::NoteReference> UCRichDocument::NoteReferences() const {
    std::vector<NoteReference> out;
    auto scan = [&](const std::vector<RichTextRun>& runs, int block, int row, int cell) {
        for (size_t r = 0; r < runs.size(); r++) {
            if (runs[r].noteIndex >= 0 && runs[r].noteIndex < static_cast<int>(notes.size())) {
                out.push_back({block, row, cell, static_cast<int>(r), runs[r].noteIndex});
            }
        }
    };
    for (size_t b = 0; b < blocks.size(); b++) {
        const RichDocBlock& block = blocks[b];
        scan(block.runs, static_cast<int>(b), -1, -1);
        for (size_t r = 0; r < block.tableRows.size(); r++) {
            for (size_t c = 0; c < block.tableRows[r].cells.size(); c++) {
                scan(block.tableRows[r].cells[c].runs, static_cast<int>(b), static_cast<int>(r), static_cast<int>(c));
            }
        }
    }
    return out;
}

std::vector<std::string> UCRichDocument::NoteMarks() const {
    std::vector<std::string> marks(notes.size());
    int footnotes = 0, endnotes = 0;
    for (const NoteReference& reference : NoteReferences()) {
        std::string& mark = marks[static_cast<size_t>(reference.noteIndex)];
        if (!mark.empty()) continue;            // referred to again: same mark
        if (notes[static_cast<size_t>(reference.noteIndex)].kind == RichNote::Kind::Endnote) {
            mark = FormatListNumber(++endnotes, RichNumberFormat::LowerRoman);
        } else {
            mark = std::to_string(++footnotes);
        }
    }
    return marks;
}

bool UCRichDocument::UpdateNoteMarks() {
    const std::vector<std::string> marks = NoteMarks();
    bool changed = false;
    auto update = [&](std::vector<RichTextRun>& runs) {
        for (RichTextRun& run : runs) {
            if (run.noteIndex < 0 || run.noteIndex >= static_cast<int>(marks.size())) continue;
            const std::string& mark = marks[static_cast<size_t>(run.noteIndex)];
            if (!mark.empty() && run.text != mark) {
                run.text = mark;
                changed = true;
            }
            run.superscript = true;
        }
    };
    for (RichDocBlock& block : blocks) {
        update(block.runs);
        for (RichTableRow& row : block.tableRows) {
            for (RichTableCell& cell : row.cells) update(cell.runs);
        }
    }
    return changed;
}

// ===== BOOKMARKS, CAPTIONS, CROSS-REFERENCES, CONTENTS =====

namespace {

// Every run list of the body (paragraphs and table cells) with its block.
template <typename Blocks, typename Visit>
void ForEachBodyRuns(Blocks& blocks, Visit visit) {
    for (size_t b = 0; b < blocks.size(); b++) {
        visit(blocks[b].runs, static_cast<int>(b));
        for (auto& row : blocks[b].tableRows) {
            for (auto& cell : row.cells) visit(cell.runs, static_cast<int>(b));
        }
    }
}

// A paragraph's text as a reference or a contents entry shows it: no object
// placeholders, no line breaks, trimmed.
std::string ShownText(const std::vector<RichTextRun>& runs) {
    std::string text;
    for (const RichTextRun& run : runs) {
        if (run.IsInlineImage() || run.IsNoteReference()) continue;
        if (run.lineBreakBefore && !text.empty()) text += ' ';
        text += run.text;
    }
    for (size_t at; (at = text.find('\t')) != std::string::npos;) text[at] = ' ';
    const size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

} // namespace

const RichSectionSetup& UCRichDocument::SectionFor(int index) const {
    for (int i = std::min(index, static_cast<int>(blocks.size()) - 1); i >= 0; i--) {
        if (blocks[static_cast<size_t>(i)].sectionStart) return blocks[static_cast<size_t>(i)].section;
    }
    return firstSection;
}

bool UCRichDocument::HasColumns() const {
    if (firstSection.columns > 1) return true;
    for (const RichDocBlock& block : blocks) {
        if (block.sectionStart && block.section.columns > 1) return true;
    }
    return false;
}

bool UCRichDocument::HasTrackedChanges() const {
    bool any = false;
    ForEachBodyRuns(blocks, [&](const std::vector<RichTextRun>& runs, int) {
        for (const RichTextRun& run : runs) any = any || run.change != RichTextRun::Change::Unchanged;
    });
    return any;
}

std::vector<int> UCRichDocument::ActiveComments() const {
    std::vector<int> out;
    std::vector<bool> seen(comments.size(), false);
    ForEachBodyRuns(blocks, [&](const std::vector<RichTextRun>& runs, int) {
        for (const RichTextRun& run : runs) {
            for (int id : run.commentIds) {
                if (id < 0 || id >= static_cast<int>(comments.size()) || seen[static_cast<size_t>(id)]) continue;
                seen[static_cast<size_t>(id)] = true;
                out.push_back(id);
            }
        }
    });
    return out;
}

std::vector<UCRichDocument::BookmarkInfo> UCRichDocument::Bookmarks() const {
    std::vector<BookmarkInfo> out;
    for (size_t b = 0; b < blocks.size(); b++) {
        for (const std::string& name : blocks[b].bookmarks) out.push_back({name, static_cast<int>(b)});
    }
    return out;
}

int UCRichDocument::FindBookmark(const std::string& name) const {
    if (name.empty()) return -1;
    for (size_t b = 0; b < blocks.size(); b++) {
        for (const std::string& mark : blocks[b].bookmarks) {
            if (mark == name) return static_cast<int>(b);
        }
    }
    return -1;
}

std::string UCRichDocument::UniqueBookmarkName(const std::string& base) const {
    if (!base.empty() && FindBookmark(base) < 0) return base;
    for (int n = 1;; n++) {
        const std::string name = base + std::to_string(n);
        if (FindBookmark(name) < 0) return name;
    }
}

bool UCRichDocument::UpdateFields() {
    bool changed = false;
    std::map<std::string, int> counters;
    std::map<int, std::string> captions;          // block -> "Figure 3"
    ForEachBodyRuns(blocks, [&](std::vector<RichTextRun>& runs, int block) {
        for (RichTextRun& run : runs) {
            if (run.field != RichTextRun::Field::Sequence) continue;
            const std::string number = std::to_string(++counters[run.fieldArgument]);
            if (run.text != number) {
                run.text = number;
                changed = true;
            }
            if (!captions.count(block)) captions[block] = run.fieldArgument + " " + number;
        }
    });
    ForEachBodyRuns(blocks, [&](std::vector<RichTextRun>& runs, int) {
        for (RichTextRun& run : runs) {
            if (run.field != RichTextRun::Field::Reference) continue;
            const int target = FindBookmark(run.fieldArgument);
            if (target < 0) continue;              // left as last shown
            auto caption = captions.find(target);
            std::string value = caption != captions.end() ? caption->second
                                                           : ShownText(blocks[static_cast<size_t>(target)].runs);
            if (value.empty()) value = run.fieldArgument;
            if (run.text != value) {
                run.text = value;
                changed = true;
            }
        }
    });
    return changed;
}

bool UCRichDocument::UpdatePageReferences(const std::vector<int>& blockPages) {
    bool changed = false;
    ForEachBodyRuns(blocks, [&](std::vector<RichTextRun>& runs, int) {
        for (RichTextRun& run : runs) {
            if (run.field != RichTextRun::Field::PageReference) continue;
            const int target = FindBookmark(run.fieldArgument);
            if (target < 0 || target >= static_cast<int>(blockPages.size())) continue;
            const std::string page = std::to_string(blockPages[static_cast<size_t>(target)]);
            if (run.text != page) {
                run.text = page;
                changed = true;
            }
        }
    });
    return changed;
}

std::vector<RichDocBlock> UCRichDocument::BuildTableOfContents(int maxLevel) {
    std::vector<RichDocBlock> entries;
    // The page numbers line up at the text column's right edge.
    const float columnPt = page.HasPage() ? page.widthPt - page.marginLeftPt - page.marginRightPt
                                          : 595.3f - 2.0f * 56.7f;
    for (RichDocBlock& block : blocks) {
        if (block.type != RichBlockType::Heading || block.tocLevel > 0) continue;
        if (block.headingLevel < 1 || block.headingLevel > maxLevel) continue;
        const std::string text = ShownText(block.runs);
        if (text.empty()) continue;
        if (block.bookmarks.empty()) block.bookmarks.push_back(UniqueBookmarkName("_Toc"));
        const std::string target = block.bookmarks.front();

        RichDocBlock entry;
        entry.tocLevel = block.headingLevel;
        const std::string style = "TOC" + std::to_string(block.headingLevel);
        if (FindStyle(style)) entry.styleId = style;
        entry.leftIndentPt = 12.0f * static_cast<float>(block.headingLevel - 1);
        entry.tabStops.push_back({std::max(columnPt, 72.0f), RichTabKind::Right});
        RichTextRun title;
        title.text = text;
        RichTextRun tab;
        tab.text = "\t";
        RichTextRun pageNumber;
        pageNumber.field = RichTextRun::Field::PageReference;
        pageNumber.fieldArgument = target;
        pageNumber.text = "1";
        entry.runs = {title, tab, pageNumber};
        entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        // Kept as a one-line placeholder, so the next update finds its place.
        RichDocBlock entry;
        entry.tocLevel = 1;
        RichTextRun note;
        note.text = "No headings found.";
        entry.runs = {note};
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool UCRichDocument::UpdateTableOfContents(int maxLevel) {
    size_t first = 0;
    while (first < blocks.size() && blocks[first].tocLevel <= 0) first++;
    if (first == blocks.size()) return false;
    size_t end = first;
    while (end < blocks.size() && blocks[end].tocLevel > 0) end++;
    std::vector<RichDocBlock> entries = BuildTableOfContents(maxLevel);
    blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(first), blocks.begin() + static_cast<std::ptrdiff_t>(end));
    blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(first), entries.begin(), entries.end());
    return true;
}

// ===== NAMED STYLES =====

void RichStyleCharacter::ApplyTo(RichTextRun& run) const {
    if (bold) run.bold = *bold;
    if (italic) run.italic = *italic;
    if (underline) run.underline = *underline;
    if (strikethrough) run.strikethrough = *strikethrough;
    if (code) run.code = *code;
    if (fontFamily) run.fontFamily = *fontFamily;
    if (fontSizePt) run.fontSizePt = *fontSizePt;
    if (color) run.color = *color;
    if (highlightColor) run.highlightColor = *highlightColor;
}

void RichStyleCharacter::Overlay(const RichStyleCharacter& over) {
    if (over.bold) bold = over.bold;
    if (over.italic) italic = over.italic;
    if (over.underline) underline = over.underline;
    if (over.strikethrough) strikethrough = over.strikethrough;
    if (over.code) code = over.code;
    if (over.fontFamily) fontFamily = over.fontFamily;
    if (over.fontSizePt) fontSizePt = over.fontSizePt;
    if (over.color) color = over.color;
    if (over.highlightColor) highlightColor = over.highlightColor;
}

void RichStyleParagraph::ApplyTo(RichDocBlock& block) const {
    if (headingLevel && (block.type == RichBlockType::Paragraph || block.type == RichBlockType::Heading)) {
        if (*headingLevel >= 1 && *headingLevel <= 6) {
            block.type = RichBlockType::Heading;
            block.headingLevel = *headingLevel;
        } else {
            block.type = RichBlockType::Paragraph;
            block.headingLevel = 0;
        }
    }
    if (align) block.align = *align;
    if (leftIndentPt) block.leftIndentPt = *leftIndentPt;
    if (rightIndentPt) block.rightIndentPt = *rightIndentPt;
    if (firstLineIndentPt) block.firstLineIndentPt = *firstLineIndentPt;
    if (spaceBeforePt) block.spaceBeforePt = *spaceBeforePt;
    if (spaceAfterPt) block.spaceAfterPt = *spaceAfterPt;
    if (lineSpacing) block.lineSpacing = *lineSpacing;
}

void RichStyleParagraph::Overlay(const RichStyleParagraph& over) {
    if (over.headingLevel) headingLevel = over.headingLevel;
    if (over.align) align = over.align;
    if (over.leftIndentPt) leftIndentPt = over.leftIndentPt;
    if (over.rightIndentPt) rightIndentPt = over.rightIndentPt;
    if (over.firstLineIndentPt) firstLineIndentPt = over.firstLineIndentPt;
    if (over.spaceBeforePt) spaceBeforePt = over.spaceBeforePt;
    if (over.spaceAfterPt) spaceAfterPt = over.spaceAfterPt;
    if (over.lineSpacing) lineSpacing = over.lineSpacing;
}

const RichStyle* UCRichDocument::FindStyle(const std::string& id) const {
    for (const RichStyle& style : styles) {
        if (style.id == id) return &style;
    }
    return nullptr;
}

RichStyle UCRichDocument::ResolveStyle(const std::string& id) const {
    // The chain from the root down, then folded root first so the style's own
    // properties win. Bounded: a cycle in a file's basedOn cannot hang it.
    std::vector<const RichStyle*> chain;
    for (const RichStyle* style = FindStyle(id); style && chain.size() < 16;
         style = style->basedOn.empty() || style->basedOn == style->id ? nullptr : FindStyle(style->basedOn)) {
        chain.push_back(style);
    }
    RichStyle resolved;
    if (chain.empty()) {
        resolved.id = id;
        return resolved;
    }
    resolved = *chain.front();
    resolved.character = RichStyleCharacter{};
    resolved.paragraph = RichStyleParagraph{};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        resolved.character.Overlay((*it)->character);
        resolved.paragraph.Overlay((*it)->paragraph);
    }
    return resolved;
}

bool UCRichDocument::StyleDerivesFrom(const std::string& id, const std::string& ancestorId) const {
    std::string current = id;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        if (current == ancestorId) return true;
        const RichStyle* style = FindStyle(current);
        if (!style || style->basedOn == current) return false;
        current = style->basedOn;
    }
    return false;
}

std::vector<RichStyle> UCRichDocument::DefaultStyles() {
    std::vector<RichStyle> out;
    auto paragraph = [&](const std::string& id, const std::string& name, const std::string& basedOn) -> RichStyle& {
        RichStyle style;
        style.id = id;
        style.name = name;
        style.basedOn = basedOn;
        out.push_back(style);
        return out.back();
    };
    paragraph("Normal", "Normal", "");
    {
        RichStyle& title = paragraph("Title", "Title", "Normal");
        title.character.fontSizePt = 26.0f;
        title.character.bold = true;
        title.paragraph.spaceAfterPt = 6.0f;
        title.nextStyle = "Normal";
    }
    {
        RichStyle& subtitle = paragraph("Subtitle", "Subtitle", "Normal");
        subtitle.character.fontSizePt = 15.0f;
        subtitle.character.color = "#595959";
        subtitle.nextStyle = "Normal";
    }
    static const float headingSizes[6] = {20.0f, 16.0f, 14.0f, 12.0f, 11.0f, 11.0f};
    for (int level = 1; level <= 6; level++) {
        RichStyle& heading = paragraph("Heading" + std::to_string(level), "Heading " + std::to_string(level), "Normal");
        heading.paragraph.headingLevel = level;
        heading.paragraph.spaceBeforePt = level <= 2 ? 12.0f : 8.0f;
        heading.paragraph.spaceAfterPt = 4.0f;
        heading.character.bold = true;
        heading.character.fontSizePt = headingSizes[level - 1];
        heading.nextStyle = "Normal";
    }
    {
        RichStyle& quote = paragraph("Quote", "Quote", "Normal");
        quote.character.italic = true;
        quote.paragraph.leftIndentPt = 36.0f;
        quote.paragraph.rightIndentPt = 36.0f;
    }
    {
        RichStyle& code = paragraph("CodeBlock", "Code", "Normal");
        code.character.code = true;
        code.character.fontFamily = "Courier New";
    }
    {
        RichStyle& caption = paragraph("Caption", "Caption", "Normal");
        caption.character.italic = true;
        caption.character.fontSizePt = 9.0f;
        caption.paragraph.spaceAfterPt = 10.0f;
    }
    auto character = [&](const std::string& id, const std::string& name) -> RichStyle& {
        RichStyle style;
        style.id = id;
        style.name = name;
        style.kind = RichStyle::Kind::Character;
        out.push_back(style);
        return out.back();
    };
    character("Strong", "Strong").character.bold = true;
    character("Emphasis", "Emphasis").character.italic = true;
    {
        RichStyle& code = character("SourceText", "Source Text");
        code.character.code = true;
    }
    return out;
}

const char* RichBorderStyleWordName(RichBorderStyle style) {
    switch (style) {
        case RichBorderStyle::Dotted: return "dotted";
        case RichBorderStyle::Dashed: return "dashed";
        case RichBorderStyle::Double: return "double";
        default:                      return "single";
    }
}

const char* RichBorderStyleOdfName(RichBorderStyle style) {
    switch (style) {
        case RichBorderStyle::Dotted: return "dotted";
        case RichBorderStyle::Dashed: return "dashed";
        case RichBorderStyle::Double: return "double";
        default:                      return "solid";
    }
}

RichBorderStyle RichBorderStyleFromName(const std::string& name) {
    const std::string lower = ToLowerCopy(name);
    if (lower.find("double") != std::string::npos || lower.find("triple") != std::string::npos
        || lower.find("thinthick") != std::string::npos || lower.find("thickthin") != std::string::npos) {
        return RichBorderStyle::Double;
    }
    // "dotDash" and "dotDotDash" read as dashes: the dash is what the eye sees.
    if (lower.find("dash") != std::string::npos) return RichBorderStyle::Dashed;
    if (lower.find("dot") != std::string::npos) return RichBorderStyle::Dotted;
    return RichBorderStyle::Solid;
}

std::string FormatListNumber(int number, RichNumberFormat format) {
    switch (format) {
        case RichNumberFormat::NoNumber:
            return "";
        case RichNumberFormat::DecimalZero:
            return (number >= 0 && number < 10 ? "0" : "") + std::to_string(number);
        case RichNumberFormat::LowerLetter:
        case RichNumberFormat::UpperLetter: {
            // Word and Writer repeat the letter past z: y, z, aa, bb, ...
            if (number < 1) return std::to_string(number);
            const char base = format == RichNumberFormat::LowerLetter ? 'a' : 'A';
            return std::string(static_cast<size_t>((number - 1) / 26 + 1),
                               static_cast<char>(base + (number - 1) % 26));
        }
        case RichNumberFormat::LowerRoman:
        case RichNumberFormat::UpperRoman: {
            if (number < 1 || number > 3999) return std::to_string(number);
            static const std::pair<int, const char*> numerals[] = {
                {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
                {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
            std::string out;
            for (const auto& [value, text] : numerals) {
                while (number >= value) { out += text; number -= value; }
            }
            if (format == RichNumberFormat::LowerRoman) {
                for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }
        case RichNumberFormat::Decimal:
        default:
            return std::to_string(number);
    }
}

std::string RichDocListLabel(const std::vector<RichDocBlock>& blocks, size_t index) {
    if (index >= blocks.size()) return "";
    const RichDocBlock& block = blocks[index];
    if (block.type != RichBlockType::ListItem || !block.orderedList) return "";
    const std::string templ = block.numberTemplate.empty()
            ? "%" + std::to_string(std::clamp(block.listLevel, 0, 8) + 1) + "."
            : block.numberTemplate;
    // The number of level `level` as seen from this item.
    auto levelNumber = [&](int level) -> std::string {
        if (level == block.listLevel) {
            return FormatListNumber(RichDocOrderedItemNumber(blocks, index), block.numberFormat);
        }
        for (size_t i = index; i-- > 0;) {
            const RichDocBlock& previous = blocks[i];
            if (previous.type != RichBlockType::ListItem) break;
            if (previous.listLevel == level) {
                return previous.orderedList
                        ? FormatListNumber(RichDocOrderedItemNumber(blocks, i), previous.numberFormat) : "";
            }
            if (previous.listLevel < level) break;
        }
        return "1";
    };
    std::string label;
    for (size_t i = 0; i < templ.size(); ++i) {
        if (templ[i] == '%' && i + 1 < templ.size() && templ[i + 1] >= '1' && templ[i + 1] <= '9') {
            label += levelNumber(templ[i + 1] - '1');
            ++i;
        } else {
            label.push_back(templ[i]);
        }
    }
    return label;
}

std::vector<RichListNumbering::Counter>& RichListNumbering::LevelsOf(const std::string& listKey) {
    for (auto& entry : lists_) {
        if (entry.first == listKey) return entry.second;
    }
    lists_.emplace_back(listKey, std::vector<Counter>(10));
    return lists_.back().second;
}

int RichListNumbering::Next(const std::string& listKey, int level, int startAt) {
    std::vector<Counter>& levels = LevelsOf(listKey);
    const size_t l = static_cast<size_t>(std::clamp(level, 0, static_cast<int>(levels.size()) - 1));
    Counter& counter = levels[l];
    if (counter.restartAt > 0) {
        counter.value = counter.restartAt;
        counter.restartAt = 0;
    } else if (!counter.started) {
        counter.value = startAt;
    } else {
        counter.value++;
    }
    counter.started = true;
    // A deeper level begins again under this item.
    for (size_t deeper = l + 1; deeper < levels.size(); deeper++) {
        levels[deeper].started = false;
        levels[deeper].restartAt = 0;
    }
    return counter.value;
}

void RichListNumbering::Restart(const std::string& listKey, int level, int number) {
    std::vector<Counter>& levels = LevelsOf(listKey);
    const size_t l = static_cast<size_t>(std::clamp(level, 0, static_cast<int>(levels.size()) - 1));
    levels[l].restartAt = number;
}

void RichListNumbering::Apply(std::vector<RichDocBlock>& blocks, size_t index, int number) {
    if (index >= blocks.size() || number <= 0) return;
    RichDocBlock& block = blocks[index];
    if (block.type != RichBlockType::ListItem || !block.orderedList) return;
    block.listStartNumber = 0;
    if (RichDocOrderedItemNumber(blocks, index) != number) block.listStartNumber = number;
}

} // namespace UltraCanvas
