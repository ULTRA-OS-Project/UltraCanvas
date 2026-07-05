// Plugins/Documents/Word/UltraCanvasRichDocument.cpp
// UCRichDocument serializers: Markdown (editable round-trip), HTML
// (read-only rich view), plain text — plus media helpers shared by the
// ODT/DOCX readers and writers.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"

#include <algorithm>
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
    for (char c : text) {
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

std::string RunToMarkdown(const RichTextRun& run, bool inTableCell) {
    std::string lead, core, trail;
    SplitEdgeWhitespace(run.text, lead, core, trail);
    if (core.empty()) return run.text;

    std::string body;
    if (run.code) {
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
    }
    if (!run.linkTarget.empty()) {
        body = "[" + body + "](" + run.linkTarget + ")";
    }
    return lead + body + trail;
}

std::string RunsToMarkdown(const std::vector<RichTextRun>& runs, bool inTableCell) {
    std::string out;
    for (const auto& run : MergeAdjacentRuns(runs)) {
        if (run.lineBreakBefore && !out.empty()) {
            // Hard break inside a paragraph: markdown two-space line break in
            // normal flow, "<br>" is avoided; inside table cells fall back to a space.
            out += inTableCell ? " " : "  \n";
        }
        out += RunToMarkdown(run, inTableCell);
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
    std::string ext = ToLowerCopy(std::filesystem::path(fileName).extension().string());
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

std::string UCRichDocument::ToMarkdown(const RichDocumentMarkdownOptions& options) const {
    // Write referenced media to disk once, remembering the path per index.
    std::vector<std::string> mediaPaths(media.size());
    if (!options.imageDirectory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.imageDirectory, ec);
        for (size_t i = 0; i < media.size(); ++i) {
            std::string name = media[i].name.empty()
                ? "image" + std::to_string(i + 1) + "." + FileExtensionForMimeType(media[i].mimeType)
                : std::filesystem::path(media[i].name).filename().string();
            std::filesystem::path target = std::filesystem::path(options.imageDirectory) / name;
            std::ofstream out(target, std::ios::binary);
            if (out.is_open()) {
                out.write(reinterpret_cast<const char*>(media[i].data.data()),
                          static_cast<std::streamsize>(media[i].data.size()));
                mediaPaths[i] = target.string();
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
                md << std::string(level, '#') << ' ' << RunsToMarkdown(block.runs, false) << "\n";
                break;
            }
            case RichBlockType::ListItem: {
                // Consecutive list items form one list without blank lines.
                bool previousIsListItem = bi > 0 && blocks[bi - 1].type == RichBlockType::ListItem;
                if (!previousIsListItem) blockSeparator();
                else first = false;
                md << std::string(static_cast<size_t>(std::max(0, block.listLevel)) * 2, ' ')
                   << (block.orderedList ? "1. " : "- ")
                   << RunsToMarkdown(block.runs, false) << "\n";
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
                md << "> " << RunsToMarkdown(block.runs, false) << "\n";
                break;
            }
            case RichBlockType::Table: {
                blockSeparator();
                for (size_t r = 0; r < block.tableRows.size(); ++r) {
                    const auto& row = block.tableRows[r];
                    md << "|";
                    for (const auto& cell : row.cells) {
                        md << ' ' << RunsToMarkdown(cell.runs, true) << " |";
                    }
                    md << "\n";
                    if (r == 0) {
                        // Markdown tables require a header separator after row one.
                        md << "|";
                        for (size_t c = 0; c < row.cells.size(); ++c) md << " --- |";
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
            case RichBlockType::Paragraph:
            default: {
                blockSeparator();
                md << RunsToMarkdown(block.runs, false) << "\n";
                break;
            }
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
                    resolved = std::filesystem::path(baseDirectory) / resolved;
                }
                std::ifstream in(resolved, std::ios::binary);
                if (in.is_open()) {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                               std::istreambuf_iterator<char>());
                    RichDocBlock block;
                    block.type = RichBlockType::Image;
                    block.imageAltText = alt;
                    block.mediaIndex = doc.AddMedia(resolved.filename().string(),
                                                    MimeTypeForImageName(resolved.string()),
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
                parseInlineToBlock(info.content, block);
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

    return doc;
}

// ===== HTML SERIALIZER =====

namespace {

std::string RunsToHtml(const std::vector<RichTextRun>& runs) {
    std::string out;
    for (const auto& run : MergeAdjacentRuns(runs)) {
        if (run.lineBreakBefore && !out.empty()) out += "<br/>";
        std::string body = EscapeHtml(run.text);
        if (run.code) body = "<code>" + body + "</code>";
        if (run.bold) body = "<b>" + body + "</b>";
        if (run.italic) body = "<i>" + body + "</i>";
        if (run.underline) body = "<u>" + body + "</u>";
        if (run.strikethrough) body = "<s>" + body + "</s>";
        if (run.subscript) body = "<sub>" + body + "</sub>";
        if (run.superscript) body = "<sup>" + body + "</sup>";
        std::string style;
        if (!run.color.empty()) style += "color:" + run.color + ";";
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
                html << "<h" << level << ">" << RunsToHtml(block.runs) << "</h" << level << ">\n";
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
                    html << (block.orderedList ? "<ol>\n" : "<ul>\n");
                    listOrderedStack.push_back(block.orderedList);
                    ++openListLevel;
                }
                html << "<li>" << RunsToHtml(block.runs) << "</li>\n";
                break;
            }
            case RichBlockType::CodeBlock:
                html << "<pre><code>" << EscapeHtml(ConcatenateRunText(block.runs))
                     << "</code></pre>\n";
                break;
            case RichBlockType::BlockQuote:
                html << "<blockquote><p>" << RunsToHtml(block.runs) << "</p></blockquote>\n";
                break;
            case RichBlockType::Table: {
                html << "<table border=\"1\">\n";
                for (const auto& row : block.tableRows) {
                    const char* tag = row.header ? "th" : "td";
                    html << "<tr>";
                    for (const auto& cell : row.cells) {
                        html << "<" << tag;
                        if (cell.columnSpan > 1) html << " colspan=\"" << cell.columnSpan << "\"";
                        if (cell.rowSpan > 1) html << " rowspan=\"" << cell.rowSpan << "\"";
                        html << ">" << RunsToHtml(cell.runs) << "</" << tag << ">";
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
            case RichBlockType::Paragraph:
            default: {
                const char* alignCss = AlignCss(block.align);
                if (alignCss) html << "<p style=\"text-align:" << alignCss << "\">";
                else html << "<p>";
                html << RunsToHtml(block.runs) << "</p>\n";
                break;
            }
        }
    }
    closeListsTo(-1);
    return html.str();
}

// ===== PLAIN TEXT SERIALIZER =====

std::string UCRichDocument::ToPlainText() const {
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
                        text << ConcatenateRunText(cell.runs);
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
                text << ConcatenateRunText(block.runs) << "\n";
                break;
        }
    }
    return text.str();
}

} // namespace UltraCanvas
