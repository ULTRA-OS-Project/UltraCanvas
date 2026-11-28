// UltraSite/core/UltraSiteMarkdown.cpp
// Markdown as a web page: the framework reads and writes the Markdown
// (UCRichDocument), this cuts the text at the site's own directives -
// admonitions and tabs - and hooks the writer for heading ids, highlighted
// code, links and pictures.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSiteMarkdown.h"

#include "HTMLReader/HTMLDocument.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasSyntaxTokenizer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

using UltraCanvas::HTML::EscapeText;

namespace UltraSite {

namespace {

std::string Trim(const std::string& text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

// A fence's marker when the line opens or closes one ("```", "~~~~"), else
// empty.
std::string FenceMarker(const std::string& line) {
    const std::string trimmed = Trim(line);
    if (trimmed.size() < 3 || (trimmed[0] != '`' && trimmed[0] != '~')) return std::string();
    size_t n = 0;
    while (n < trimmed.size() && trimmed[n] == trimmed[0]) ++n;
    return n >= 3 ? trimmed.substr(0, n) : std::string();
}

// The parts of a page: Markdown, an admonition around parts, tabs of parts.
struct Segment {
    enum class Kind { Text, Admonition, Tabs } kind = Kind::Text;
    std::string text;                                            // Text
    std::string type, title;                                     // Admonition
    std::vector<Segment> children;                               // Admonition
    std::vector<std::pair<std::string, std::vector<Segment>>> tabs;   // Tabs: label, parts
};

std::vector<Segment> Parse(const std::vector<std::string>& lines);

// ":::name rest" -> name and rest; false for another line. ":::" alone is
// a close, not an open.
bool DirectiveOpen(const std::string& line, std::string& name, std::string& rest) {
    const std::string trimmed = Trim(line);
    if (trimmed.size() < 4 || trimmed.compare(0, 3, ":::") != 0 || trimmed[3] == ':') return false;
    size_t end = 3;
    while (end < trimmed.size() && (std::isalnum(static_cast<unsigned char>(trimmed[end])) || trimmed[end] == '-')) ++end;
    if (end == 3) return false;
    name = Lower(trimmed.substr(3, end - 3));
    rest = Trim(trimmed.substr(end));
    if (rest.size() >= 2 && rest.front() == '[' && rest.back() == ']') rest = rest.substr(1, rest.size() - 2);
    return true;
}

bool DirectiveClose(const std::string& line) { return Trim(line) == ":::"; }

// The lines up to the ":::" that closes the directive opened just before
// `i`, nested directives and fenced code counted; `i` ends past the close.
std::vector<std::string> CollectBody(const std::vector<std::string>& lines, size_t& i) {
    std::vector<std::string> body;
    int depth = 0;
    std::string fence;
    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::string marker = FenceMarker(line);
        if (!fence.empty()) {
            if (marker.size() >= fence.size() && marker[0] == fence[0] && Trim(line) == marker) fence.clear();
        } else if (!marker.empty()) {
            fence = marker;
        } else {
            std::string name, rest;
            if (DirectiveOpen(line, name, rest)) {
                ++depth;
            } else if (DirectiveClose(line)) {
                if (depth == 0) {
                    ++i;
                    return body;
                }
                --depth;
            }
        }
        body.push_back(line);
    }
    return body;
}

// "::tab[Label]" or "::tab Label" -> the label.
bool TabLine(const std::string& line, std::string& label) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("::tab", 0) != 0) return false;
    std::string rest = Trim(trimmed.substr(5));
    if (rest.size() >= 2 && rest.front() == '[' && rest.back() == ']') rest = rest.substr(1, rest.size() - 2);
    label = rest;
    return true;
}

std::vector<Segment> Parse(const std::vector<std::string>& lines) {
    std::vector<Segment> out;
    std::string text;
    std::string fence;
    auto flush = [&]() {
        if (!Trim(text).empty()) {
            Segment segment;
            segment.text = text;
            out.push_back(std::move(segment));
        }
        text.clear();
    };
    for (size_t i = 0; i < lines.size();) {
        const std::string& line = lines[i];
        const std::string marker = FenceMarker(line);
        if (!fence.empty()) {
            if (marker.size() >= fence.size() && marker[0] == fence[0] && Trim(line) == marker) fence.clear();
            text += line + "\n";
            ++i;
            continue;
        }
        if (!marker.empty()) {
            fence = marker;
            text += line + "\n";
            ++i;
            continue;
        }
        std::string name, rest;
        if (DirectiveOpen(line, name, rest)) {
            flush();
            ++i;
            const std::vector<std::string> body = CollectBody(lines, i);
            Segment segment;
            if (name == "tabs") {
                segment.kind = Segment::Kind::Tabs;
                std::vector<std::string> tabLines;
                std::string label;
                bool open = false;
                std::string innerFence;
                auto closeTab = [&]() {
                    if (open) segment.tabs.emplace_back(label, Parse(tabLines));
                    tabLines.clear();
                };
                for (const std::string& bodyLine : body) {
                    const std::string bodyMarker = FenceMarker(bodyLine);
                    if (!innerFence.empty()) {
                        if (bodyMarker.size() >= innerFence.size() && bodyMarker[0] == innerFence[0] && Trim(bodyLine) == bodyMarker) innerFence.clear();
                    } else if (!bodyMarker.empty()) {
                        innerFence = bodyMarker;
                    } else {
                        std::string tabLabel;
                        if (TabLine(bodyLine, tabLabel)) {
                            closeTab();
                            label = tabLabel;
                            open = true;
                            continue;
                        }
                    }
                    tabLines.push_back(bodyLine);
                }
                closeTab();
            } else {
                segment.kind = Segment::Kind::Admonition;
                segment.type = name;
                segment.title = rest;
                segment.children = Parse(body);
            }
            out.push_back(std::move(segment));
            continue;
        }
        // GitHub's alerts: "> [!NOTE]" and the quoted lines after it.
        const std::string trimmed = Trim(line);
        if (trimmed.size() > 5 && trimmed[0] == '>' ) {
            const std::string inner = Trim(trimmed.substr(1));
            if (inner.size() > 3 && inner.compare(0, 2, "[!") == 0 && inner.back() == ']') {
                const std::string type = Lower(inner.substr(2, inner.size() - 3));
                if (type == "note" || type == "tip" || type == "important" || type == "warning" || type == "caution") {
                    flush();
                    std::vector<std::string> body;
                    ++i;
                    while (i < lines.size() && !Trim(lines[i]).empty() && Trim(lines[i])[0] == '>') {
                        std::string quoted = Trim(lines[i]).substr(1);
                        if (!quoted.empty() && quoted[0] == ' ') quoted.erase(0, 1);
                        body.push_back(quoted);
                        ++i;
                    }
                    Segment segment;
                    segment.kind = Segment::Kind::Admonition;
                    segment.type = type == "important" ? "info" : type == "caution" ? "danger" : type;
                    segment.children = Parse(body);
                    out.push_back(std::move(segment));
                    continue;
                }
            }
        }
        text += line + "\n";
        ++i;
    }
    flush();
    return out;
}

// What kind of Markdown line this is, for joining wrapped lines.
enum class LineKind { Blank, Paragraph, ListItem, Quote, Block };

bool IsListMarker(const std::string& trimmed) {
    if (trimmed.size() >= 2 && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') return true;
    size_t digits = 0;
    while (digits < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[digits]))) ++digits;
    return digits > 0 && digits <= 9 && digits + 1 < trimmed.size() && (trimmed[digits] == '.' || trimmed[digits] == ')') &&
           trimmed[digits + 1] == ' ';
}

bool IsRule(const std::string& trimmed) {
    if (trimmed.size() < 3) return false;
    const char mark = trimmed[0];
    if (mark != '-' && mark != '*' && mark != '_' && mark != '=') return false;
    size_t count = 0;
    for (char c : trimmed) {
        if (c == mark) ++count;
        else if (c != ' ') return false;
    }
    return count >= 3;
}

LineKind KindOf(const std::string& line) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) return LineKind::Blank;
    if (trimmed[0] == '>') return LineKind::Quote;
    if (IsListMarker(trimmed)) return LineKind::ListItem;
    if (trimmed[0] == '#' || trimmed[0] == '|' || trimmed[0] == '<' || IsRule(trimmed) || trimmed.rfind(":::", 0) == 0 ||
        trimmed.rfind("::tab", 0) == 0 || !FenceMarker(line).empty()) {
        return LineKind::Block;
    }
    // An indented line after a blank one is code; after text it continues it.
    return LineKind::Paragraph;
}

// The framework's Markdown reader takes a line as a paragraph, as an editor
// does; the Markdown of a site wraps its paragraphs (CommonMark joins
// them). The wrapped lines of a paragraph, a list item and a quote are
// joined here; code, tables, headings and a hard break ("  " or "\" at the
// end) are left as they are.
std::string UnwrapParagraphs(const std::string& markdown) {
    const std::vector<std::string> lines = SplitLines(markdown);
    std::vector<std::string> out;
    std::vector<LineKind> kinds;
    std::string fence;
    for (const std::string& line : lines) {
        const std::string marker = FenceMarker(line);
        if (!fence.empty()) {
            if (marker.size() >= fence.size() && marker[0] == fence[0] && Trim(line) == marker) fence.clear();
            out.push_back(line);
            kinds.push_back(LineKind::Block);
            continue;
        }
        if (!marker.empty()) {
            fence = marker;
            out.push_back(line);
            kinds.push_back(LineKind::Block);
            continue;
        }
        const LineKind kind = KindOf(line);
        const bool hardBreak = !out.empty() && (out.back().size() >= 2 && out.back().compare(out.back().size() - 2, 2, "  ") == 0);
        const bool backslash = !out.empty() && !out.back().empty() && out.back().back() == '\\';
        if (!out.empty() && !hardBreak && !backslash) {
            const LineKind previous = kinds.back();
            if (kind == LineKind::Paragraph && (previous == LineKind::Paragraph || previous == LineKind::ListItem)) {
                out.back() += " " + Trim(line);
                continue;
            }
            if (kind == LineKind::Quote && previous == LineKind::Quote) {
                const std::string text = Trim(Trim(line).substr(1));
                const std::string before = Trim(Trim(out.back()).substr(1));
                if (!text.empty() && !before.empty() && KindOf(text) == LineKind::Paragraph && KindOf(before) != LineKind::Block) {
                    out.back() += " " + text;
                    continue;
                }
            }
        }
        out.push_back(line);
        kinds.push_back(kind);
    }
    std::string joined;
    for (const std::string& line : out) joined += line + "\n";
    return joined;
}

struct Context {
    const MarkdownOptions& options;
    HeadingIds ids;
    MarkdownResult result;
};

bool KeepsLink(const std::string& target) {
    return target.empty() || target[0] == '#' || target[0] == '/' || target.find("://") != std::string::npos ||
           target.rfind("mailto:", 0) == 0 || target.rfind("tel:", 0) == 0 || target.rfind("data:", 0) == 0;
}

void RewriteLinks(std::vector<UltraCanvas::RichTextRun>& runs, const MarkdownOptions& options) {
    for (UltraCanvas::RichTextRun& run : runs) {
        if (!run.linkTarget.empty() && !KeepsLink(run.linkTarget)) run.linkTarget = options.link(run.linkTarget);
    }
}

std::string RenderText(const std::string& markdown, Context& context) {
    const MarkdownOptions& options = context.options;
    UltraCanvas::UCRichDocument document = UltraCanvas::UCRichDocument::FromMarkdown(UnwrapParagraphs(markdown), options.baseDirectory);
    if (options.link) {
        for (UltraCanvas::RichDocBlock& block : document.blocks) {
            RewriteLinks(block.runs, options);
            for (UltraCanvas::RichTableRow& row : block.tableRows) {
                for (UltraCanvas::RichTableCell& cell : row.cells) RewriteLinks(cell.runs, options);
            }
        }
    }
    UltraCanvas::RichDocumentHTMLOptions html;
    if (options.image) {
        html.imageSource = [&](int index) {
            if (index < 0 || static_cast<size_t>(index) >= document.media.size()) return std::string();
            const UltraCanvas::RichDocMedia& media = document.media[static_cast<size_t>(index)];
            return options.image(media.name, media.data);
        };
    }
    html.headingId = [&](int level, const std::string& text) {
        const std::string id = context.ids.Next(text);
        if (level == 1 && context.result.title.empty()) context.result.title = Trim(text);
        if (level == 2 || level == 3) context.result.toc.push_back({level, Trim(text), id});
        return id;
    };
    html.codeBlock = [&](const std::string& language, const std::string& code) {
        return HighlightCode(language, code, options.copyLabel, options.copiedLabel);
    };
    return document.ToHTML(html);
}

std::string AdmonitionTitle(const std::string& type) {
    if (type.empty()) return std::string();
    std::string title = type;
    title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    return title;
}

std::string Render(const std::vector<Segment>& segments, Context& context) {
    std::string out;
    for (const Segment& segment : segments) {
        switch (segment.kind) {
            case Segment::Kind::Text:
                out += RenderText(segment.text, context);
                break;
            case Segment::Kind::Admonition: {
                std::string type = Slugify(segment.type);
                if (type.empty()) type = "note";
                out += "<div class=\"us-admonition us-admonition-" + type + "\" role=\"note\">\n<p class=\"us-admonition-title\">" +
                       EscapeText(segment.title.empty() ? AdmonitionTitle(segment.type) : segment.title) + "</p>\n";
                out += Render(segment.children, context);
                out += "</div>\n";
                break;
            }
            case Segment::Kind::Tabs:
                out += "<div class=\"us-tabs\" data-us=\"tabs\">\n";
                for (const auto& [label, parts] : segment.tabs) {
                    out += "<section class=\"us-tab\" data-label=\"" + EscapeText(label) + "\">\n<p class=\"us-tab-label\">" +
                           EscapeText(label) + "</p>\n";
                    out += Render(parts, context);
                    out += "</section>\n";
                }
                out += "</div>\n";
                break;
        }
    }
    return out;
}

const char* TokenClass(UltraCanvas::TokenType type) {
    using UltraCanvas::TokenType;
    switch (type) {
        case TokenType::Keyword: case TokenType::Assembly: return "k";
        case TokenType::Type: return "t";
        case TokenType::Function: return "f";
        case TokenType::Number: return "n";
        case TokenType::String: case TokenType::Character: return "s";
        case TokenType::Comment: return "c";
        case TokenType::Operator: return "o";
        case TokenType::Punctuation: return "p";
        case TokenType::Preprocessor: return "pp";
        case TokenType::Constant: return "cn";
        case TokenType::Builtin: case TokenType::Register: return "b";
        default: return nullptr;
    }
}

} // namespace

FrontMatter ParseFrontMatter(const std::string& text) {
    FrontMatter matter;
    size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;   // a byte-order mark
    if (text.compare(start, 3, "---") != 0) {
        matter.body = text.substr(start);
        return matter;
    }
    size_t lineEnd = text.find('\n', start);
    if (lineEnd == std::string::npos || Trim(text.substr(start, lineEnd - start)) != "---") {
        matter.body = text.substr(start);
        return matter;
    }
    size_t position = lineEnd + 1;
    while (position < text.size()) {
        size_t end = text.find('\n', position);
        if (end == std::string::npos) end = text.size();
        const std::string line = Trim(text.substr(position, end - position));
        position = end + 1;
        if (line == "---") {
            matter.present = true;
            matter.body = position < text.size() ? text.substr(position) : std::string();
            return matter;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos || line[0] == '#') continue;
        std::string value = Trim(line.substr(colon + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
        matter.values[Trim(line.substr(0, colon))] = value;
    }
    // No closing "---": not front matter after all.
    matter.values.clear();
    matter.body = text.substr(start);
    return matter;
}

namespace {

// One code point from UTF-8 at `i`, which it moves past; U+FFFD for a
// broken sequence.
uint32_t NextCodePoint(const std::string& text, size_t& i) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
    if (i + length > text.size()) length = 1;
    uint32_t cp = length == 1 ? lead : length == 2 ? (lead & 0x1Fu) : length == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
    for (size_t k = 1; k < length; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) {
            i += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (next & 0x3Fu);
    }
    if (length == 1 && lead >= 0x80) cp = 0xFFFD;
    i += length;
    return cp;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Whether GitHub keeps a code point beyond ASCII in an id: letters, marks
// and digits yes; punctuation, symbols, dashes, quotes, arrows, emoji no.
// The blocks of punctuation and symbols are listed, so every script's
// letters are kept.
bool KeptInId(uint32_t cp) {
    if (cp == 0xFFFD) return false;
    if (cp <= 0xBF) return false;                                  // Latin-1 punctuation and symbols
    if (cp == 0xD7 || cp == 0xF7) return false;                    // x and divide
    if (cp >= 0x2000 && cp <= 0x2BFF) return false;               // general punctuation .. misc symbols and arrows
    if (cp >= 0x2E00 && cp <= 0x2E7F) return false;               // supplemental punctuation
    if (cp >= 0x3000 && cp <= 0x303F) return false;               // CJK symbols and punctuation
    if (cp >= 0xFE10 && cp <= 0xFE6F) return false;               // vertical forms, small forms
    if (cp >= 0xFF00 && cp <= 0xFF0F) return false;               // fullwidth punctuation
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return false;             // emoji and pictographs
    if (cp >= 0xFE00 && cp <= 0xFE0F) return false;               // variation selectors
    return true;
}

// The lower case of the letters most headings use beyond ASCII: Latin-1,
// Latin Extended-A, Greek and Cyrillic.
uint32_t LowerCodePoint(uint32_t cp) {
    if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) return cp + 0x20;
    if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x138 && cp != 0x149 && cp != 0x17F) {
        const bool evenUpper = (cp < 0x139) || (cp >= 0x14A && cp < 0x178);
        if (evenUpper ? (cp % 2 == 0) : (cp % 2 == 1)) return cp + 1;
        if (cp == 0x178) return 0xFF;
        return cp;
    }
    if (cp >= 0x391 && cp <= 0x3AB && cp != 0x3A2) return cp + 0x20;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
    return cp;
}

} // namespace

std::string Slugify(const std::string& text) {
    const std::string trimmed = Trim(text);
    std::string out;
    for (size_t i = 0; i < trimmed.size();) {
        const unsigned char c = static_cast<unsigned char>(trimmed[i]);
        if (c < 0x80) {
            ++i;
            if (std::isalnum(c) || c == '_' || c == '-') out += static_cast<char>(std::tolower(c));
            else if (c == ' ') out += '-';
            continue;
        }
        const uint32_t cp = NextCodePoint(trimmed, i);
        if (KeptInId(cp)) AppendUtf8(out, LowerCodePoint(cp));
    }
    return out;
}

std::string HeadingIds::Next(const std::string& text) {
    const std::string base = Slugify(text);
    int& count = seen[base];
    const std::string id = count == 0 ? base : base + "-" + std::to_string(count);
    ++count;
    return id;
}

std::string HighlightCode(const std::string& language, const std::string& code,
                          const std::string& copyLabel, const std::string& copiedLabel) {
    std::string text = code;
    if (!text.empty() && text.front() == '\n') text.erase(0, 1);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const std::string tag = Lower(Trim(language));
    std::string body;
    thread_local UltraCanvas::SyntaxTokenizer tokenizer;
    if (!tag.empty() && tokenizer.SetLanguageByTag(tag)) {
        for (const UltraCanvas::SyntaxTokenizer::Token& token : tokenizer.Tokenize(text)) {
            const char* cls = TokenClass(token.type);
            if (cls) body += std::string("<span class=\"tok-") + cls + "\">" + EscapeText(token.text) + "</span>";
            else body += EscapeText(token.text);
        }
    } else {
        body = EscapeText(text);
    }
    std::string out = "<pre class=\"us-code\"";
    if (!copyLabel.empty()) {
        out += " data-us=\"copy\" data-copy-label=\"" + EscapeText(copyLabel) + "\"";
        if (!copiedLabel.empty()) out += " data-copied-label=\"" + EscapeText(copiedLabel) + "\"";
    }
    if (!tag.empty()) out += " data-language=\"" + EscapeText(tag) + "\"";
    out += "><code";
    if (!tag.empty()) out += " class=\"language-" + EscapeText(Slugify(tag)) + "\"";
    return out + ">" + body + "</code></pre>";
}

MarkdownResult MarkdownToHtml(const std::string& markdown, const MarkdownOptions& options) {
    Context context{options, {}, {}};
    context.result.html = Render(Parse(SplitLines(markdown)), context);
    return context.result;
}

} // namespace UltraSite
