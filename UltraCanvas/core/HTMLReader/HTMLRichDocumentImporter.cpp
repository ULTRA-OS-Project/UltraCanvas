// core/HTMLReader/HTMLRichDocumentImporter.cpp
// HTML → UCRichDocument. See the header for what is mapped and what is not.
// Version: 1.2.0 - Word's list paragraphs (mso-list) are list items, their typed
//                  label the marker; every newline of a <pre> counts (the parser
//                  drops the one right after the start tag)
// Version: 1.1.0 - dir="rtl" paragraphs; preAsCodeBlock; skipWordListLabels
// Version: 1.0.1 - a cell border is the widest of its four sides
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLRichDocumentImporter.h"
#include "HTMLReader/CSSStyleSheet.h"
#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/HTMLStyleResolver.h"
#include "UltraCanvasTextUtils.h"   // Base64Decode

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace UltraCanvas {

namespace {

using HTML::ComputedStyle;
using HTML::DisplayMode;
using HTML::Node;

// CSS pixels to the points the model keeps lengths in.
constexpr float kPxToPt = 0.75f;
// An indent beyond this is a layout trick (a centred column built from a
// margin), not an indent the text should keep.
constexpr float kMaxIndentPx = 160.0f;

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string LowerAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string TrimAscii(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && IsSpace(text[begin])) ++begin;
    while (end > begin && IsSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

bool StartsWithNoCase(const std::string& text, const char* prefix) {
    return LowerAscii(text.substr(0, std::char_traits<char>::length(prefix))) == prefix;
}

std::string HexColor(const HTML::CssColor& color) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X", color.r, color.g, color.b);
    return buffer;
}

bool IsBlack(const HTML::CssColor& color) {
    return color.r == 0 && color.g == 0 && color.b == 0;
}

RichTextAlign AlignOf(HTML::TextAlignMode mode) {
    switch (mode) {
        case HTML::TextAlignMode::Center: return RichTextAlign::Center;
        case HTML::TextAlignMode::Right: return RichTextAlign::Right;
        case HTML::TextAlignMode::Justify: return RichTextAlign::Justify;
        default: return RichTextAlign::Default;
    }
}

int IntAttribute(const Node& node, const char* name, int fallback) {
    const std::string value = TrimAscii(node.GetAttribute(name));
    if (value.empty()) return fallback;
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    return end == value.c_str() ? fallback : static_cast<int>(number);
}

// "%41" → "A" (a data: URI without ;base64).
std::string PercentDecode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()
            && std::isxdigit(static_cast<unsigned char>(text[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out.push_back(static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

// data:[<media type>][;base64],<data>
bool DecodeDataUri(const std::string& src, HTMLRichImportImage& out) {
    if (!StartsWithNoCase(src, "data:")) return false;
    const size_t comma = src.find(',');
    if (comma == std::string::npos) return false;
    const std::string meta = LowerAscii(src.substr(5, comma - 5));
    const std::string payload = src.substr(comma + 1);
    out.mimeType = meta.substr(0, meta.find(';'));
    if (meta.find(";base64") != std::string::npos) {
        std::string compact;
        for (char c : payload) if (!IsSpace(c)) compact.push_back(c);
        out.data = Base64Decode(compact);
    } else {
        const std::string bytes = PercentDecode(payload);
        out.data.assign(bytes.begin(), bytes.end());
    }
    return !out.data.empty();
}

// The media type of a picture from its first bytes ("" when unknown).
std::string SniffImageType(const std::vector<uint8_t>& data) {
    auto starts = [&](const char* magic, size_t offset = 0) {
        const size_t length = std::char_traits<char>::length(magic);
        if (data.size() < offset + length) return false;
        return std::equal(magic, magic + length, data.begin() + static_cast<std::ptrdiff_t>(offset),
                          [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; });
    };
    if (starts("\x89PNG")) return "image/png";
    if (starts("\xFF\xD8\xFF")) return "image/jpeg";
    if (starts("GIF8")) return "image/gif";
    if (starts("RIFF") && starts("WEBP", 8)) return "image/webp";
    if (starts("BM")) return "image/bmp";
    if (starts("<svg") || starts("<?xml")) return "image/svg+xml";
    return "";
}

// The display modes that start a line of their own.
bool IsBlockDisplay(DisplayMode display) {
    return display == DisplayMode::Block || display == DisplayMode::ListItem
        || display == DisplayMode::Table || display == DisplayMode::TableRow
        || display == DisplayMode::TableCell;
}

// A link the document may keep: never one that runs code.
std::string SafeLink(const std::string& href) {
    const std::string target = TrimAscii(href);
    if (target.empty() || StartsWithNoCase(target, "javascript:")
        || StartsWithNoCase(target, "vbscript:") || StartsWithNoCase(target, "data:")) {
        return "";
    }
    return target;
}

// Whether the element's text runs right to left: the dir attribute on it or
// the nearest element around it that has one.
bool RightToLeft(const Node& node) {
    for (const Node* at = &node; at; at = at->parent) {
        if (at->IsElement() && at->HasAttribute("dir")) {
            return LowerAscii(TrimAscii(at->GetAttribute("dir"))) == "rtl";
        }
    }
    return false;
}

// A list label Word types out before an item: <span style="mso-list:Ignore">.
bool IsWordListLabel(const Node& node) {
    const std::string style = node.GetAttribute("style");
    if (style.empty()) return false;
    for (const HTML::Declaration& declaration : HTML::StyleSheet::ParseDeclarationList(style)) {
        if (declaration.property == "mso-list" && LowerAscii(declaration.value) == "ignore") return true;
    }
    return false;
}

// The level (from 1) of a paragraph Word made a list item of - its style has
// mso-list: l<list> level<n> lfo<n> - or 0 for any other element.
int WordListLevel(const Node& node) {
    const std::string style = node.GetAttribute("style");
    if (style.empty()) return 0;
    for (const HTML::Declaration& declaration : HTML::StyleSheet::ParseDeclarationList(style)) {
        if (declaration.property != "mso-list") continue;
        const std::string value = LowerAscii(declaration.value);
        const size_t at = value.find("level");
        if (at == std::string::npos) return 0;
        const int level = std::atoi(value.c_str() + at + 5);
        return level > 0 ? std::min(level, 9) : 0;
    }
    return 0;
}

// The label Word typed out in a list paragraph, or null.
const Node* FindWordListLabel(const Node& node) {
    for (const auto& child : node.children) {
        if (!child->IsElement()) continue;
        if (IsWordListLabel(*child)) return child.get();
        if (const Node* label = FindWordListLabel(*child)) return label;
    }
    return nullptr;
}

// What a Word list label says about its list.
struct WordLabel {
    bool ordered = false;
    RichNumberFormat format = RichNumberFormat::Decimal;
    int number = 0;
    std::string bullet;   // "" = the default bullet
};

// "iv" -> 4; 0 when the letters are no Roman numeral.
int RomanValue(const std::string& lower) {
    static const std::string kSymbols = "ivxlcdm";
    static const int kValues[] = {1, 5, 10, 50, 100, 500, 1000};
    int total = 0;
    for (size_t i = 0; i < lower.size(); ++i) {
        const size_t symbol = kSymbols.find(lower[i]);
        if (symbol == std::string::npos) return 0;
        const size_t next = i + 1 < lower.size() ? kSymbols.find(lower[i + 1]) : std::string::npos;
        if (next != std::string::npos && kValues[next] > kValues[symbol]) total -= kValues[symbol];
        else total += kValues[symbol];
    }
    return total > 0 && total < 4000 ? total : 0;
}

// "c" -> 3, "bb" -> 28 (Word repeats the letter past z); 0 otherwise.
int LetterValue(const std::string& lower) {
    if (lower.empty() || lower.size() > 4) return 0;
    for (char c : lower) {
        if (c != lower[0] || c < 'a' || c > 'z') return 0;
    }
    return static_cast<int>(lower.size() - 1) * 26 + (lower[0] - 'a' + 1);
}

// White space and no-break spaces off both ends.
std::string TrimSpaces(std::string text) {
    for (;;) {
        if (!text.empty() && IsSpace(text.back())) text.pop_back();
        else if (text.size() >= 2 && text.compare(text.size() - 2, 2, "\xC2\xA0") == 0) text.resize(text.size() - 2);
        else break;
    }
    size_t start = 0;
    for (;;) {
        if (start < text.size() && IsSpace(text[start])) ++start;
        else if (text.compare(start, 2, "\xC2\xA0") == 0) start += 2;
        else break;
    }
    return text.substr(start);
}

// Reads a label - "1.", "a)", "(iv)", "1.2.", "·", "o" - into a list's kind.
// `previous` is the item before at the same level, when there is one: a lone
// "i" after "h." is a letter, not a Roman one.
WordLabel ReadWordLabel(const std::string& labelText, const RichDocBlock* previous) {
    const std::string text = TrimSpaces(labelText);
    WordLabel label;
    std::string core = text;
    bool punctuated = false;
    if (!core.empty() && core.front() == '(') { core.erase(0, 1); punctuated = true; }
    while (!core.empty() && (core.back() == '.' || core.back() == ')' || core.back() == ':')) {
        core.pop_back();
        punctuated = true;
    }
    // A multilevel number ("1.2") counts by its last part.
    if (const size_t dot = core.rfind('.'); dot != std::string::npos) core.erase(0, dot + 1);

    const bool digits = !core.empty() && core.size() <= 6
        && std::all_of(core.begin(), core.end(), [](char c) { return c >= '0' && c <= '9'; });
    const bool lower = !core.empty() && std::all_of(core.begin(), core.end(), [](char c) { return c >= 'a' && c <= 'z'; });
    const bool upper = !core.empty() && std::all_of(core.begin(), core.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    if (digits) {
        label.ordered = true;
        label.number = std::atoi(core.c_str());
        return label;
    }
    if (punctuated && (lower || upper)) {
        const std::string folded = LowerAscii(core);
        const int roman = RomanValue(folded);
        const int letter = LetterValue(folded);
        const RichNumberFormat before = previous && previous->orderedList
            ? previous->numberFormat : RichNumberFormat::Decimal;
        const bool afterLetters = before == RichNumberFormat::LowerLetter || before == RichNumberFormat::UpperLetter;
        const bool afterRoman = before == RichNumberFormat::LowerRoman || before == RichNumberFormat::UpperRoman;
        // What can only be a Roman numeral ("iv") is one. What can be either
        // goes on as the list began ("i." after "h." is a letter); starting
        // a list, i, v, x and "ii", "iii" are Roman, c, d, l and m letters.
        bool asRoman = false;
        if (roman > 0 && letter == 0) asRoman = true;
        else if (roman > 0 && afterLetters) asRoman = false;
        else if (roman > 0 && afterRoman) asRoman = true;
        else if (roman > 0) asRoman = folded.size() > 1 || folded == "i" || folded == "v" || folded == "x";
        if (asRoman || letter > 0) {
            label.ordered = true;
            label.number = asRoman ? roman : letter;
            label.format = asRoman ? (lower ? RichNumberFormat::LowerRoman : RichNumberFormat::UpperRoman)
                                   : (lower ? RichNumberFormat::LowerLetter : RichNumberFormat::UpperLetter);
            return label;
        }
    }
    // A bullet. Word writes its own in symbol fonts: "o" in Courier New is a
    // circle and "§" in Wingdings a square; the Symbol font's "·" and
    // anything else unknown is the plain bullet.
    if (text == "o") {
        label.bullet = "\xE2\x97\xA6";   // ◦
    } else if (text == "\xC2\xA7") {
        label.bullet = "\xE2\x96\xAA";   // ▪
    } else if (text == "-" || text == "\xE2\x80\x93" || text == "\xE2\x80\x94") {
        label.bullet = "\xE2\x80\x93";   // –
    }
    return label;
}

// True when `text` holds anything but white space (no-break spaces count as
// white space here: an "empty" mail paragraph is often just &nbsp;).
bool HasVisibleText(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (IsSpace(static_cast<char>(c))) continue;
        if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            ++i;
            continue;
        }
        return true;
    }
    return false;
}

class Importer {
public:
    Importer(UCRichDocument& document, const HTMLRichImportOptions& options)
        : doc_(document), opts_(options), firstBlock_(document.blocks.size()) {}

    void Run(const std::string& html) {
        HTML::Parser parser;
        HTML::ParseOptions parseOptions;
        // The space between two inline elements is a text node of its own;
        // without it "<b>a</b> <i>b</i>" would read "ab".
        parseOptions.keepWhitespaceNodes = true;
        dom_ = parser.Parse(html, parseOptions);
        for (const auto& css : dom_.styleSheets) resolver_.AddStyleSheet(css);
        HTML::ResolverOptions resolverOptions;
        resolverOptions.baseFontSizePx = opts_.baseFontSizePx;
        resolver_.Resolve(dom_, resolverOptions);

        Node* body = dom_.Body();
        if (!body) return;
        Context context;
        context.quoteLevel = std::max(0, opts_.quoteLevel);
        proto_.quoteLevel = context.quoteLevel;
        proto_.rightToLeft = RightToLeft(*body);
        WalkChildren(*body, context);
        FinishParagraph();
        TrimEmptyEdges();
    }

private:
    // What the walk hands down to an element's descendants.
    struct Context {
        int quoteLevel = 0;
        float indentPx = 0.0f;
        std::string link;
        std::string highlight;
        bool subscript = false;
        bool superscript = false;
        bool pre = false;
        bool codeBlock = false;   // the runs of a code block: no formatting of their own
        bool wordListItem = false; // inside a Word list paragraph: its label is the marker
    };

    // What the next paragraph opened becomes.
    struct Proto {
        RichBlockType type = RichBlockType::Paragraph;
        int headingLevel = 0;
        bool ordered = false;
        int listLevel = 0;
        RichNumberFormat numberFormat = RichNumberFormat::Decimal;
        std::string bullet;
        int listStart = 0;
        // The number a Word list item shows (its typed label), 0 when none:
        // given to the item only where the model would count another one.
        int wordNumber = 0;
        RichTextAlign align = RichTextAlign::Default;
        bool rightToLeft = false;
        int quoteLevel = 0;
        float indentPx = 0.0f;
        std::string background;
    };

    struct ListState {
        bool ordered = false;
        RichNumberFormat format = RichNumberFormat::Decimal;
        std::string bullet;
        int start = 1;
        bool first = true;
    };

    UCRichDocument& doc_;
    HTMLRichImportOptions opts_;
    size_t firstBlock_;
    HTML::Document dom_;
    HTML::StyleResolver resolver_;

    // Where text goes: a table cell's runs while one is filled, else the
    // paragraph at openBlock_ (opened on the first text).
    std::vector<RichTextRun>* cellRuns_ = nullptr;
    int openBlock_ = -1;
    // > 0 inside a list item: the edges of blocks inside it are line breaks,
    // not new paragraphs (the model's list item is one paragraph).
    int mergeDepth_ = 0;
    bool lineHasContent_ = false;
    bool lastWasSpace_ = true;
    int pendingBreaks_ = 0;
    // Vertical space owed before the next block, collapsed as CSS collapses
    // adjacent margins: the larger one wins.
    float gapPx_ = 0.0f;
    Proto proto_;
    std::vector<ListState> lists_;

    // ===== CONTAINERS =====

    bool InLineMode() const { return cellRuns_ != nullptr || mergeDepth_ > 0; }

    std::vector<RichTextRun>& Runs() {
        if (cellRuns_) return *cellRuns_;
        if (openBlock_ < 0) OpenParagraph();
        return doc_.blocks[static_cast<size_t>(openBlock_)].runs;
    }

    bool InHeading() const {
        return !cellRuns_ && openBlock_ >= 0
            && doc_.blocks[static_cast<size_t>(openBlock_)].type == RichBlockType::Heading;
    }

    void PushBlock(RichDocBlock block) {
        if (doc_.blocks.size() > firstBlock_) {
            doc_.blocks.back().spaceAfterPt = gapPx_ * kPxToPt;
            block.spaceBeforePt = 0.0f;
        } else {
            block.spaceBeforePt = doc_.blocks.empty() ? 0.0f : gapPx_ * kPxToPt;
        }
        gapPx_ = 0.0f;
        doc_.blocks.push_back(std::move(block));
    }

    void OpenParagraph() {
        RichDocBlock block;
        block.type = proto_.type;
        block.headingLevel = proto_.headingLevel;
        if (block.type == RichBlockType::ListItem) {
            block.orderedList = proto_.ordered;
            block.listLevel = proto_.listLevel;
            block.numberFormat = proto_.numberFormat;
            block.bulletText = proto_.bullet;
            block.listStartNumber = proto_.listStart;
            proto_.listStart = 0;   // the item after it counts on
        } else {
            block.leftIndentPt = std::min(proto_.indentPx, kMaxIndentPx) * kPxToPt;
        }
        block.align = proto_.align;
        block.rightToLeft = proto_.rightToLeft;
        block.quoteLevel = proto_.quoteLevel;
        block.paragraphBackground = proto_.background;
        PushBlock(std::move(block));
        openBlock_ = static_cast<int>(doc_.blocks.size()) - 1;
        if (proto_.wordNumber > 0 && doc_.blocks.back().orderedList
            && RichDocOrderedItemNumber(doc_.blocks, doc_.blocks.size() - 1) != proto_.wordNumber) {
            doc_.blocks.back().listStartNumber = proto_.wordNumber;
        }
        proto_.wordNumber = 0;
        lineHasContent_ = false;
        lastWasSpace_ = true;
    }

    static void TrimTrailingSpace(std::vector<RichTextRun>& runs) {
        while (!runs.empty()) {
            RichTextRun& last = runs.back();
            if (last.IsInlineImage()) return;
            while (!last.text.empty() && last.text.back() == ' ') last.text.pop_back();
            if (!last.text.empty() || last.lineBreakBefore) return;
            runs.pop_back();
        }
    }

    // A block edge: a new paragraph in the flow, a line break inside a list
    // item or a table cell.
    void BreakBlock() {
        if (InLineMode()) {
            if (lineHasContent_) pendingBreaks_ = std::max(pendingBreaks_, 1);
            lastWasSpace_ = true;
        } else {
            FinishParagraph();
        }
    }

    // Closes the open paragraph. A single <br> at its end draws nothing (as in
    // a browser); each one more is an empty line. A block holding nothing but
    // <br>s is that many empty lines - "<div><br></div>" is how many editors
    // write a blank line.
    void FinishParagraph() {
        if (cellRuns_) return;
        if (openBlock_ < 0) {
            if (pendingBreaks_ <= 0) return;
            OpenParagraph();
        }
        std::vector<RichTextRun>& runs = doc_.blocks[static_cast<size_t>(openBlock_)].runs;
        TrimTrailingSpace(runs);
        for (int i = 1; i < pendingBreaks_; ++i) {
            RichTextRun empty;
            empty.lineBreakBefore = true;
            runs.push_back(empty);
        }
        pendingBreaks_ = 0;
        openBlock_ = -1;
        lineHasContent_ = false;
        lastWasSpace_ = true;
    }

    // Adds `run` to the current container, after the line breaks owed.
    void AddRun(RichTextRun run) {
        std::vector<RichTextRun>& runs = Runs();
        if (pendingBreaks_ > 0) {
            TrimTrailingSpace(runs);
            for (int i = 1; i < pendingBreaks_; ++i) {
                RichTextRun empty;
                empty.lineBreakBefore = true;
                runs.push_back(empty);
            }
            run.lineBreakBefore = true;
            pendingBreaks_ = 0;
        }
        if (InHeading()) run.fontSizePt = 0.0f;   // a heading has the view's heading size
        runs.push_back(std::move(run));
        lineHasContent_ = true;
    }

    // ===== TEXT =====

    RichTextRun MakeRun(const ComputedStyle& style, const Context& context) const {
        RichTextRun run;
        run.bold = style.bold;
        run.italic = style.italic;
        // A link is drawn underlined by the view; the UA underline is not the
        // author's.
        run.underline = style.underline && context.link.empty();
        run.strikethrough = style.strikethrough;
        run.code = style.monospace;
        run.subscript = context.subscript;
        run.superscript = context.superscript && !context.subscript;
        run.linkTarget = context.link;
        if (opts_.keepColors) {
            if (context.link.empty() && style.color.a > 0 && !IsBlack(style.color)) {
                run.color = HexColor(style.color);
            }
            run.highlightColor = context.highlight;
        }
        if (opts_.keepFonts) {
            if (!style.fontFamily.empty() && !style.monospace) run.fontFamily = style.fontFamily;
            if (!run.subscript && !run.superscript
                && std::fabs(style.fontSizePx - opts_.baseFontSizePx) > 0.5f) {
                run.fontSizePt = std::round(style.fontSizePx * kPxToPt * 2.0f) / 2.0f;
            }
        }
        if (context.codeBlock) {
            // The block is in the view's code style already.
            run.code = false;
            run.fontFamily.clear();
            run.fontSizePt = 0.0f;
        }
        return run;
    }

    void AppendText(const std::string& raw, const ComputedStyle& style, const Context& context) {
        if (context.pre || style.preserveWhitespace) {
            std::string segment;
            auto flush = [&]() {
                if (segment.empty()) return;
                RichTextRun run = MakeRun(style, context);
                run.text = segment;
                AddRun(std::move(run));
                segment.clear();
            };
            for (char c : raw) {
                if (c == '\r') continue;
                if (c == '\n') {
                    flush();
                    // Every line feed is a line: the parser has already
                    // dropped the one right after <pre>'s start tag, which
                    // is all HTML drops.
                    ++pendingBreaks_;
                    continue;
                }
                // A tab is a space in text, but indents a code block's line.
                segment.push_back(c == '\t' && !context.codeBlock ? ' ' : c);
            }
            flush();
            lastWasSpace_ = false;
            return;
        }

        std::string text;
        text.reserve(raw.size());
        for (char c : raw) {
            if (IsSpace(c)) {
                if (!text.empty() && text.back() == ' ') continue;
                text.push_back(' ');
            } else {
                text.push_back(c);
            }
        }
        if (!text.empty() && text.front() == ' '
            && (lastWasSpace_ || !lineHasContent_ || pendingBreaks_ > 0)) {
            text.erase(0, 1);
        }
        if (text.empty()) return;
        RichTextRun run = MakeRun(style, context);
        run.text = text;
        AddRun(std::move(run));
        lastWasSpace_ = text.back() == ' ';
    }

    // ===== WALK =====

    void WalkChildren(Node& node, const Context& context) {
        const ComputedStyle& style = resolver_.StyleOf(&node);
        for (const auto& child : node.children) {
            if (child->IsText()) AppendText(child->text, style, context);
            else if (child->IsElement()) Walk(*child, context);
        }
    }

    void Walk(Node& node, const Context& context) {
        const ComputedStyle& style = resolver_.StyleOf(&node);
        if (style.display == DisplayMode::Hidden) return;
        const std::string& tag = node.tag;

        if (tag == "br") {
            ++pendingBreaks_;
            lastWasSpace_ = true;
            return;
        }
        if (tag == "img") { Image(node, style, context); return; }
        if (tag == "hr") { Rule(); return; }
        if (tag == "input" || tag == "select" || tag == "textarea" || tag == "script") return;
        if ((opts_.skipWordListLabels || context.wordListItem) && IsWordListLabel(node)) return;
        if (tag == "pre" && opts_.preAsCodeBlock && !InLineMode()) { CodeBlock(node, style, context); return; }
        if (tag == "table" || style.display == DisplayMode::Table) { Table(node, style, context); return; }
        if (tag == "ul" || tag == "ol") { List(node, style, context); return; }
        if (tag == "li" && !lists_.empty() && !cellRuns_) { ListItem(node, style, context); return; }
        if (IsBlockDisplay(style.display)) { Block(node, style, context, true); return; }

        // Inline: what it adds to the text inside it.
        Context inner = context;
        if (tag == "a" && node.HasAttribute("href")) inner.link = SafeLink(node.GetAttribute("href"));
        if (opts_.keepColors && style.backgroundColor && style.backgroundColor->a > 0) {
            inner.highlight = HexColor(*style.backgroundColor);
        }
        if (tag == "sub") inner.subscript = true;
        if (tag == "sup") inner.superscript = true;
        WalkChildren(node, inner);
    }

    void Block(Node& node, const ComputedStyle& style, const Context& context, bool ownBackground) {
        Context inner = context;
        const bool quote = node.tag == "blockquote";
        if (quote) {
            inner.quoteLevel++;   // the quote bar replaces the quote's margin
        } else {
            inner.indentPx += std::max(0.0f, style.marginLeft) + std::max(0.0f, style.paddingLeft);
        }
        if (node.tag == "pre") inner.pre = true;

        if (InLineMode()) {
            BreakBlock();
            WalkChildren(node, inner);
            BreakBlock();
            return;
        }

        FinishParagraph();
        gapPx_ = std::max(gapPx_, style.marginTop);
        const Proto saved = proto_;
        proto_ = Proto{};
        if (node.tag.size() == 2 && node.tag[0] == 'h' && node.tag[1] >= '1' && node.tag[1] <= '6') {
            proto_.type = RichBlockType::Heading;
            proto_.headingLevel = node.tag[1] - '0';
        }
        proto_.align = AlignOf(style.textAlign);
        proto_.rightToLeft = RightToLeft(node);
        proto_.quoteLevel = inner.quoteLevel;
        proto_.indentPx = inner.indentPx;
        if (ownBackground && !quote && opts_.keepColors && style.backgroundColor
            && style.backgroundColor->a > 0) {
            proto_.background = HexColor(*style.backgroundColor);
        }
        // A paragraph Word made a list item of (not a numbered heading,
        // which stays a heading): its typed label becomes the item's marker.
        const int wordLevel = proto_.type == RichBlockType::Paragraph && !quote ? WordListLevel(node) : 0;
        if (wordLevel > 0) {
            const Node* labelNode = FindWordListLabel(node);
            const WordLabel label = ReadWordLabel(labelNode ? labelNode->TextContent() : std::string(),
                                                  PreviousItemAt(wordLevel - 1));
            proto_.type = RichBlockType::ListItem;
            proto_.listLevel = wordLevel - 1;
            proto_.ordered = label.ordered;
            proto_.numberFormat = label.format;
            proto_.bullet = label.bullet;
            proto_.wordNumber = label.number;
            inner.wordListItem = true;
        }
        const size_t blocksBefore = doc_.blocks.size();
        WalkChildren(node, inner);
        // An item with nothing in it still has its marker.
        if (wordLevel > 0 && doc_.blocks.size() == blocksBefore && openBlock_ < 0 && pendingBreaks_ == 0) {
            OpenParagraph();
        }
        FinishParagraph();
        gapPx_ = std::max(gapPx_, style.marginBottom);
        proto_ = saved;
    }

    // The list item before the next block at `level`, as the model groups
    // items: deeper ones are skipped, anything else ends the search.
    const RichDocBlock* PreviousItemAt(int level) const {
        for (size_t i = doc_.blocks.size(); i-- > firstBlock_;) {
            const RichDocBlock& block = doc_.blocks[i];
            if (block.type != RichBlockType::ListItem || block.listLevel < level) return nullptr;
            if (block.listLevel == level) return &block;
        }
        return nullptr;
    }

    // <pre> with preAsCodeBlock: one code block of its lines.
    void CodeBlock(Node& node, const ComputedStyle& style, const Context& context) {
        FinishParagraph();
        gapPx_ = std::max(gapPx_, style.marginTop);
        const Proto saved = proto_;
        proto_ = Proto{};
        proto_.type = RichBlockType::CodeBlock;
        proto_.quoteLevel = context.quoteLevel;
        proto_.indentPx = context.indentPx;
        Context inner = context;
        inner.pre = true;
        inner.codeBlock = true;
        WalkChildren(node, inner);
        FinishParagraph();
        gapPx_ = std::max(gapPx_, style.marginBottom);
        proto_ = saved;
    }

    void Rule() {
        if (InLineMode()) { BreakBlock(); return; }
        FinishParagraph();
        RichDocBlock rule;
        rule.type = RichBlockType::HorizontalRule;
        rule.quoteLevel = proto_.quoteLevel;
        PushBlock(std::move(rule));
    }

    // ===== LISTS =====

    static ListState ListStateFor(const Node& node, const ComputedStyle& style) {
        ListState state;
        state.ordered = node.tag == "ol";
        switch (style.listMarker) {
            case HTML::ListMarker::Decimal: state.format = RichNumberFormat::Decimal; break;
            case HTML::ListMarker::LowerAlpha: state.format = RichNumberFormat::LowerLetter; state.ordered = true; break;
            case HTML::ListMarker::UpperAlpha: state.format = RichNumberFormat::UpperLetter; state.ordered = true; break;
            case HTML::ListMarker::LowerRoman: state.format = RichNumberFormat::LowerRoman; state.ordered = true; break;
            case HTML::ListMarker::UpperRoman: state.format = RichNumberFormat::UpperRoman; state.ordered = true; break;
            case HTML::ListMarker::Circle: state.bullet = "\xE2\x97\xA6"; break;   // ◦
            case HTML::ListMarker::Square: state.bullet = "\xE2\x96\xAA"; break;   // ▪
            default: break;
        }
        // <ol type="a|A|i|I">, which the style resolver does not read.
        const std::string type = node.GetAttribute("type");
        if (state.ordered && !type.empty()) {
            if (type == "a") state.format = RichNumberFormat::LowerLetter;
            else if (type == "A") state.format = RichNumberFormat::UpperLetter;
            else if (type == "i") state.format = RichNumberFormat::LowerRoman;
            else if (type == "I") state.format = RichNumberFormat::UpperRoman;
            else if (type == "1") state.format = RichNumberFormat::Decimal;
        }
        state.start = IntAttribute(node, "start", 1);
        return state;
    }

    void List(Node& node, const ComputedStyle& style, const Context& context) {
        ListState state = ListStateFor(node, style);
        if (cellRuns_) {
            // No list inside a table cell in the model: its items become lines
            // with their marker spelled out.
            int number = state.start;
            for (const auto& child : node.children) {
                if (!child->IsElement("li")) continue;
                BreakBlock();
                RichTextRun marker = MakeRun(resolver_.StyleOf(child.get()), context);
                marker.text = state.ordered ? std::to_string(number++) + ". "
                                            : (state.bullet.empty() ? "\xE2\x80\xA2" : state.bullet) + std::string(" ");
                AddRun(std::move(marker));
                lastWasSpace_ = true;
                WalkChildren(*child, context);
            }
            BreakBlock();
            return;
        }

        FinishParagraph();
        if (lists_.empty()) gapPx_ = std::max(gapPx_, style.marginTop);
        const int savedMerge = mergeDepth_;
        mergeDepth_ = 0;
        lists_.push_back(state);
        WalkChildren(node, context);
        FinishParagraph();
        lists_.pop_back();
        mergeDepth_ = savedMerge;
        if (lists_.empty()) gapPx_ = std::max(gapPx_, style.marginBottom);
    }

    void ListItem(Node& node, const ComputedStyle& style, const Context& context) {
        FinishParagraph();
        ListState& list = lists_.back();
        const Proto saved = proto_;
        proto_ = Proto{};
        proto_.type = RichBlockType::ListItem;
        proto_.ordered = list.ordered;
        proto_.listLevel = static_cast<int>(lists_.size()) - 1;
        proto_.numberFormat = list.format;
        proto_.bullet = list.bullet;
        if (list.ordered) {
            const int value = IntAttribute(node, "value", 0);
            if (value > 0) proto_.listStart = value;
            else if (list.first && list.start != 1) proto_.listStart = list.start;
        }
        list.first = false;
        proto_.align = AlignOf(style.textAlign);
        proto_.rightToLeft = RightToLeft(node);
        proto_.quoteLevel = context.quoteLevel;

        const size_t blocksBefore = doc_.blocks.size();
        ++mergeDepth_;
        WalkChildren(node, context);
        --mergeDepth_;
        // An item with nothing in it still has its bullet.
        if (doc_.blocks.size() == blocksBefore && openBlock_ < 0 && pendingBreaks_ == 0) OpenParagraph();
        FinishParagraph();
        proto_ = saved;
    }

    // ===== PICTURES =====

    // The nearest element that is a block of its own holds no text besides
    // the picture: the picture is a paragraph of its own.
    bool StandsAlone(const Node& image) const {
        for (const Node* up = image.parent; up; up = up->parent) {
            if (!up->IsElement()) continue;
            if (IsBlockDisplay(resolver_.StyleOf(up).display)) return !HasVisibleText(up->TextContent());
        }
        return false;
    }

    void Image(Node& node, const ComputedStyle& style, const Context& context) {
        const std::string src = TrimAscii(node.GetAttribute("src"));
        const std::string alt = node.GetAttribute("alt");
        float widthPx = style.widthPx.value_or(0.0f);
        float heightPx = style.heightPx.value_or(0.0f);
        // A spacer or a tracking pixel says nothing.
        if ((widthPx > 0.0f && widthPx <= 2.0f) || (heightPx > 0.0f && heightPx <= 2.0f)) return;

        HTMLRichImportImage picture;
        const bool found = DecodeDataUri(src, picture)
            || (opts_.resolveImage && !src.empty() && opts_.resolveImage(src, picture) && !picture.data.empty());
        if (!found) {
            if (HasVisibleText(alt)) AppendText("[" + alt + "]", style, context);
            return;
        }
        int pixelWidth = 0, pixelHeight = 0;
        UCRichDocument::SniffImagePixelSize(picture.data, pixelWidth, pixelHeight);
        if (pixelWidth > 0 && pixelHeight > 0 && pixelWidth <= 2 && pixelHeight <= 2) return;
        // One side stated: the other keeps the picture's proportions.
        if (widthPx > 0.0f && heightPx <= 0.0f && pixelWidth > 0) {
            heightPx = widthPx * static_cast<float>(pixelHeight) / static_cast<float>(pixelWidth);
        } else if (heightPx > 0.0f && widthPx <= 0.0f && pixelHeight > 0) {
            widthPx = heightPx * static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
        }

        std::string mime = picture.mimeType.empty() ? SniffImageType(picture.data) : LowerAscii(picture.mimeType);
        if (mime.empty()) mime = "application/octet-stream";
        const std::string name = "image" + std::to_string(doc_.media.size() + 1) + "."
                               + UCRichDocument::FileExtensionForMimeType(mime);
        const int index = doc_.AddMedia(name, mime, std::move(picture.data));

        if (!InLineMode() && !lineHasContent_ && pendingBreaks_ == 0 && StandsAlone(node)) {
            FinishParagraph();
            RichDocBlock block;
            block.type = RichBlockType::Image;
            block.mediaIndex = index;
            block.imageAltText = alt;
            block.imageWidthPt = widthPx * kPxToPt;
            block.imageHeightPt = heightPx * kPxToPt;
            block.align = AlignOf(style.textAlign);
            block.quoteLevel = proto_.quoteLevel;
            PushBlock(std::move(block));
            return;
        }
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        run.mediaIndex = index;
        run.imageWidthPt = widthPx * kPxToPt;
        run.imageHeightPt = heightPx * kPxToPt;
        run.imageAltText = alt;
        run.linkTarget = context.link;
        AddRun(std::move(run));
        lastWasSpace_ = false;
    }

    // ===== TABLES =====

    // The rows of `table` itself, through thead / tbody / tfoot but not into
    // a table nested in one of its cells.
    static void CollectRows(Node& node, std::vector<Node*>& rows) {
        for (const auto& child : node.children) {
            if (!child->IsElement()) continue;
            if (child->tag == "tr") rows.push_back(child.get());
            else if (child->tag == "thead" || child->tag == "tbody" || child->tag == "tfoot") {
                CollectRows(*child, rows);
            }
        }
    }

    static std::vector<Node*> CellsOf(Node& row) {
        std::vector<Node*> cells;
        for (const auto& child : row.children) {
            if (child->IsElement("td") || child->IsElement("th")) cells.push_back(child.get());
        }
        return cells;
    }

    RichBorder BorderOf(const ComputedStyle& style, int tableBorder) const {
        RichBorder border;
        if (style.HasBorder()) {
            const HTML::BorderSide& side = style.WidestBorder();   // one border per cell
            border.widthPt = side.Width() * kPxToPt;
            border.color = HexColor(side.color);
        } else if (tableBorder > 0) {
            border.widthPt = std::max(0.75f, static_cast<float>(tableBorder) * kPxToPt);
            border.color = "#808080";
        }
        return border;
    }

    void Table(Node& node, const ComputedStyle& style, const Context& context) {
        std::vector<Node*> rows;
        CollectRows(node, rows);
        int columns = 0;
        for (Node* row : rows) {
            int count = 0;
            for (Node* cell : CellsOf(*row)) count += std::max(1, IntAttribute(*cell, "colspan", 1));
            columns = std::max(columns, count);
        }

        if (InLineMode()) {
            // A table inside a table cell or a list item: its rows become
            // lines, its cells separated by a space.
            for (Node* row : rows) {
                BreakBlock();
                bool firstCell = true;
                for (Node* cell : CellsOf(*row)) {
                    if (!firstCell && lineHasContent_ && !lastWasSpace_) {
                        RichTextRun gap = MakeRun(resolver_.StyleOf(cell), context);
                        gap.text = " ";
                        AddRun(std::move(gap));
                        lastWasSpace_ = true;
                    }
                    firstCell = false;
                    WalkChildren(*cell, context);
                }
            }
            BreakBlock();
            return;
        }

        if (columns <= 1) {
            // A one-column table only stacks its cells: the scaffolding of a
            // mail layout. Its cells join the text flow as blocks, without
            // the backdrop colours the layout paints behind them.
            for (Node* row : rows) {
                for (Node* cell : CellsOf(*row)) Block(*cell, resolver_.StyleOf(cell), context, false);
            }
            return;
        }

        FinishParagraph();
        gapPx_ = std::max(gapPx_, style.marginTop);
        RichDocBlock table;
        table.type = RichBlockType::Table;
        table.tableBordersFromDocument = true;
        table.quoteLevel = context.quoteLevel;
        if (style.widthPx && *style.widthPx > 0.0f) table.tableWidthPt = *style.widthPx * kPxToPt;
        else if (style.widthPercent && *style.widthPercent > 0.0f) table.tableWidthPercent = *style.widthPercent;
        const std::string align = LowerAscii(node.GetAttribute("align"));
        if (align == "center") table.tableAlign = RichTextAlign::Center;
        else if (align == "right") table.tableAlign = RichTextAlign::Right;
        const int tableBorder = IntAttribute(node, "border", 0);
        const std::string tableBackground = opts_.keepColors && style.backgroundColor && style.backgroundColor->a > 0
                                          ? HexColor(*style.backgroundColor) : "";

        std::vector<float> columnWidths;
        for (Node* row : rows) {
            const ComputedStyle& rowStyle = resolver_.StyleOf(row);
            const std::string rowBackground = opts_.keepColors && rowStyle.backgroundColor && rowStyle.backgroundColor->a > 0
                                            ? HexColor(*rowStyle.backgroundColor) : tableBackground;
            RichTableRow tableRow;
            bool allHeaders = true;
            const std::vector<Node*> cells = CellsOf(*row);
            std::vector<float> widths;
            for (Node* cellNode : cells) {
                const ComputedStyle& cellStyle = resolver_.StyleOf(cellNode);
                RichTableCell cell;
                cell.columnSpan = std::clamp(IntAttribute(*cellNode, "colspan", 1), 1, 1000);
                cell.rowSpan = std::clamp(IntAttribute(*cellNode, "rowspan", 1), 1, 1000);
                cell.align = AlignOf(cellStyle.textAlign);
                cell.backgroundColor = opts_.keepColors && cellStyle.backgroundColor && cellStyle.backgroundColor->a > 0
                                     ? HexColor(*cellStyle.backgroundColor) : rowBackground;
                const RichBorder border = BorderOf(cellStyle, tableBorder);
                cell.borderTop = cell.borderBottom = cell.borderLeft = cell.borderRight = border;
                cell.paddingTopPt = cellStyle.paddingTop * kPxToPt;
                cell.paddingBottomPt = cellStyle.paddingBottom * kPxToPt;
                cell.paddingLeftPt = cellStyle.paddingLeft * kPxToPt;
                cell.paddingRightPt = cellStyle.paddingRight * kPxToPt;
                // A table cell's content sits in its middle unless told
                // otherwise.
                const std::string valign = LowerAscii(cellNode->GetAttribute("valign"));
                cell.verticalAlign = valign == "top" ? RichVerticalAlign::Top
                                   : valign == "bottom" ? RichVerticalAlign::Bottom
                                   : RichVerticalAlign::Middle;
                widths.push_back(cell.columnSpan == 1 && cellStyle.widthPx ? *cellStyle.widthPx : 0.0f);

                FillCell(*cellNode, cell.runs, context);
                if (!cellNode->IsElement("th")) allHeaders = false;
                tableRow.cells.push_back(std::move(cell));
            }
            tableRow.header = allHeaders && !tableRow.cells.empty();
            if (columnWidths.empty() && static_cast<int>(widths.size()) == columns
                && std::all_of(widths.begin(), widths.end(), [](float w) { return w > 0.0f; })) {
                columnWidths = widths;
            }
            if (!tableRow.cells.empty()) table.tableRows.push_back(std::move(tableRow));
        }
        if (table.tableRows.empty()) return;
        table.tableColumnWidths = columnWidths;
        PushBlock(std::move(table));
        gapPx_ = std::max(gapPx_, style.marginBottom);
    }

    void FillCell(Node& cellNode, std::vector<RichTextRun>& runs, const Context& context) {
        std::vector<RichTextRun>* const savedCell = cellRuns_;
        const bool savedContent = lineHasContent_;
        const bool savedSpace = lastWasSpace_;
        const int savedBreaks = pendingBreaks_;
        cellRuns_ = &runs;
        lineHasContent_ = false;
        lastWasSpace_ = true;
        pendingBreaks_ = 0;

        Context inner = context;
        inner.indentPx = 0.0f;
        inner.highlight.clear();
        WalkChildren(cellNode, inner);
        TrimTrailingSpace(runs);

        cellRuns_ = savedCell;
        lineHasContent_ = savedContent;
        lastWasSpace_ = savedSpace;
        pendingBreaks_ = savedBreaks;
    }

    // ===== CLEAN-UP =====

    static bool IsEmptyParagraph(const RichDocBlock& block) {
        if (block.type != RichBlockType::Paragraph) return false;
        for (const RichTextRun& run : block.runs) {
            if (run.IsInlineImage() || HasVisibleText(run.text)) return false;
        }
        return true;
    }

    // Blank lines before the first and after the last thing imported (a
    // mail's trailing "<div><br></div>"s) carry nothing.
    void TrimEmptyEdges() {
        while (doc_.blocks.size() > firstBlock_ && IsEmptyParagraph(doc_.blocks.back())) {
            doc_.blocks.pop_back();
        }
        while (doc_.blocks.size() > firstBlock_ && IsEmptyParagraph(doc_.blocks[firstBlock_])) {
            doc_.blocks.erase(doc_.blocks.begin() + static_cast<std::ptrdiff_t>(firstBlock_));
        }
        if (doc_.blocks.size() > firstBlock_) doc_.blocks.back().spaceAfterPt = -1.0f;
    }
};

} // namespace

UCRichDocument ImportHTMLToRichDocument(const std::string& html, const HTMLRichImportOptions& options) {
    UCRichDocument document;
    AppendHTMLToRichDocument(document, html, options);
    return document;
}

void AppendHTMLToRichDocument(UCRichDocument& document, const std::string& html,
                              const HTMLRichImportOptions& options) {
    Importer importer(document, options);
    importer.Run(html);
}

} // namespace UltraCanvas
