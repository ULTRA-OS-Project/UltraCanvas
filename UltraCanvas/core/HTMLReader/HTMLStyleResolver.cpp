// core/HTMLReader/HTMLStyleResolver.cpp
// CSS cascade: user-agent defaults → author rules → inline styles.
// Version: 1.4.0 - structural pseudo-classes match; float, and <table
//                  align="left|right"> floats
// Version: 1.3.0 - attribute selectors match; box-sizing
// Version: 1.2.2 - a later width declaration replaces an earlier one (px vs %)
// Version: 1.2.1 - width/height="auto" on <img>/<table>/<td> is no size, not 0px
// Version: 1.2.0 - table presentational attributes (nowrap, valign,
//                  cellpadding, cellspacing, tr align); white-space: nowrap;
//                  border-collapse / border-spacing / border-radius; cells
//                  default to a browser's 1px padding; `inherit` for
//                  color, font and text properties; background images and
//                  size, margin: auto, max-width.
// Last Modified: 2026-10-03
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLStyleResolver.h"

#include <cctype>
#include "UltraCanvasUtils.h"

#include <algorithm>

namespace UltraCanvas {
namespace HTML {

// ============================================================================
// PUBLIC API
// ============================================================================

void StyleResolver::Resolve(Document& document, const ResolverOptions& options) {
    styles.clear();
    opts = options;

    fallback = ComputedStyle{};
    fallback.fontSizePx = opts.baseFontSizePx;
    fallback.fontFamily = opts.baseFontFamily;
    fallback.color = opts.textColor;

    if (!document.root) return;

    ComputedStyle rootStyle = fallback;
    rootStyle.display = DisplayMode::Block;
    ResolveElement(*document.root, rootStyle);
}

const ComputedStyle& StyleResolver::StyleOf(const Node* node) const {
    auto it = styles.find(node);
    return (it != styles.end()) ? it->second : fallback;
}

// ============================================================================
// RESOLUTION
// ============================================================================

// The presentational align="..." attribute, still everywhere in
// email HTML. Applied before the author rules, so any CSS text-align wins, as
// in a browser. On a block (and a table cell) it is the text alignment of
// its content; on an <img> left/center/right place the image on its line and
// top/middle/bottom (texttop, absmiddle, absbottom) set its vertical-align. A
// <table align> centres the table itself, which is not text alignment, and is
// left alone here.
void StyleResolver::ApplyAlignAttribute(const Node& element, ComputedStyle& style) {
    static const char* const kAlignable[] = {
        "p", "div", "td", "th", "tr", "tbody", "thead", "tfoot",
        "h1", "h2", "h3", "h4", "h5", "h6", "img", "caption"
    };
    bool alignable = false;
    for (const char* tag : kAlignable) if (element.tag == tag) { alignable = true; break; }
    if (!alignable || !element.HasAttribute("align")) return;
    std::string value = element.GetAttribute("align");
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // On an image, the vertical values place it against the text of its line.
    if (element.tag == "img") {
        if (value == "middle" || value == "absmiddle") {
            style.verticalAlign = VerticalAlignMode::Middle; return;
        }
        if (value == "top" || value == "texttop") {
            style.verticalAlign = VerticalAlignMode::Top; return;
        }
        if (value == "bottom" || value == "absbottom") {
            style.verticalAlign = VerticalAlignMode::Bottom; return;
        }
        if (value == "baseline") { style.verticalAlign = VerticalAlignMode::Baseline; return; }
    }
    if (value == "left")         style.textAlign = TextAlignMode::Left;
    else if (value == "right")   style.textAlign = TextAlignMode::Right;
    else if (value == "center" || (value == "middle" && element.tag != "img"))
                                 style.textAlign = TextAlignMode::Center;
    else if (value == "justify") style.textAlign = TextAlignMode::Justify;
}

// The presentational attributes mail HTML is still written with - by Outlook,
// by older clients, by newsletter templates: <font color face size>, bgcolor
// on the page and on tables, <body text>. Applied before the author rules,
// so any CSS for the same property wins, as in a browser.
void StyleResolver::ApplyLegacyAttributes(const Node& element, ComputedStyle& style) {
    const bool colorsAllowed = !opts.overrideAuthorColors;
    // <table align="left|right"> floats, as in browsers - two 300px tables
    // in a 600px cell sit side by side (Mailchimp's two-column blocks).
    if (element.tag == "table") {
        const std::string align = TrimLower(element.GetAttribute("align"));
        if (align == "left") style.floatMode = FloatMode::Left;
        else if (align == "right") style.floatMode = FloatMode::Right;
    }
    if (element.tag == "font") {
        if (colorsAllowed) {
            if (auto color = CssColor::Parse(TrimLower(element.GetAttribute("color")))) style.color = *color;
        }
        std::string face = element.GetAttribute("face");
        if (size_t comma = face.find(','); comma != std::string::npos) face = face.substr(0, comma);
        face = Trim(face);
        if (TrimLower(face) == "monospace") style.monospace = true;
        else if (!face.empty()) style.fontFamily = face;
        // size="1".."7" (3 is the normal size), or relative: "+1", "-2".
        std::string size = Trim(element.GetAttribute("size"));
        if (!size.empty()) {
            static const float kSizes[] = {10.f, 13.f, 16.f, 18.f, 24.f, 32.f, 48.f};   // at a 16px base
            int step = 3;
            try {
                const int number = std::stoi(size);
                step = (size[0] == '+' || size[0] == '-') ? 3 + number : number;
                step = std::clamp(step, 1, 7);
                style.fontSizePx = kSizes[step - 1] * opts.baseFontSizePx / 16.f;
            } catch (...) {
                // Not a number: the size stays inherited.
            }
        }
    }
    if (colorsAllowed && element.HasAttribute("bgcolor")
        && (element.tag == "body" || element.tag == "table" || element.tag == "tr"
            || element.tag == "td" || element.tag == "th")) {
        if (auto color = CssColor::Parse(TrimLower(element.GetAttribute("bgcolor"))))
            style.backgroundColor = *color;
    }
    // Tables: <td nowrap>, valign on a cell or its row, the table's
    // cellpadding on each of its cells and its cellspacing on itself.
    const bool cell = element.tag == "td" || element.tag == "th";
    if (cell && element.HasAttribute("nowrap")) style.noWrap = true;
    if ((cell || element.tag == "tr") && element.HasAttribute("valign")) {
        const std::string v = TrimLower(element.GetAttribute("valign"));
        if (v == "top") style.verticalAlign = VerticalAlignMode::Top;
        else if (v == "bottom") style.verticalAlign = VerticalAlignMode::Bottom;
        else if (v == "middle" || v == "center") style.verticalAlign = VerticalAlignMode::Middle;
    }
    if (cell) {
        const Node* table = element.parent;
        while (table && !table->IsElement("table")) table = table->parent;
        if (table && table->HasAttribute("cellpadding")) {
            if (auto len = CssLength::Parse(TrimLower(table->GetAttribute("cellpadding")))) {
                const float px = len->unit == CssUnit::Percent ? 0.f
                               : len->ToPx(style.fontSizePx, opts.baseFontSizePx);
                style.paddingTop = style.paddingRight = style.paddingBottom = style.paddingLeft = px;
            }
        }
    }
    if (element.tag == "table" && element.HasAttribute("cellspacing")) {
        if (auto len = CssLength::Parse(TrimLower(element.GetAttribute("cellspacing")))) {
            if (len->unit != CssUnit::Percent)
                style.borderSpacing = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
        }
    }
    if (element.tag == "table" && element.HasAttribute("border")) {
        if (auto len = CssLength::Parse(TrimLower(element.GetAttribute("border")))) {
            if (len->unit != CssUnit::Percent)
                style.borderWidth = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
        }
    }
    if (colorsAllowed && element.tag == "body" && element.HasAttribute("text")) {
        if (auto color = CssColor::Parse(TrimLower(element.GetAttribute("text")))) style.color = *color;
    }
}

void StyleResolver::ResolveElement(Node& element, const ComputedStyle& parentStyle) {
    ComputedStyle style;

    // Inherited properties come from the parent.
    style.fontFamily = parentStyle.fontFamily;
    style.fontSizePx = parentStyle.fontSizePx;
    style.bold = parentStyle.bold;
    style.italic = parentStyle.italic;
    style.underline = parentStyle.underline;
    style.strikethrough = parentStyle.strikethrough;
    style.monospace = parentStyle.monospace;
    style.preserveWhitespace = parentStyle.preserveWhitespace;
    style.noWrap = parentStyle.noWrap;
    style.borderCollapse = parentStyle.borderCollapse;
    style.color = parentStyle.color;
    style.textAlign = parentStyle.textAlign;
    style.lineHeight = parentStyle.lineHeight;
    style.listMarker = parentStyle.listMarker;

    ApplyUserAgentDefaults(element.tag, style);
    ApplyAlignAttribute(element, style);
    ApplyLegacyAttributes(element, style);

    // Author rules, lowest specificity first so later Apply wins. !important
    // declarations are collected and re-applied last.
    struct Match {
        int specificity;
        int order;
        const Rule* rule;
    };
    std::vector<Match> matches;
    for (const auto& rule : sheet.rules) {
        int best = -1;
        for (const auto& selector : rule.selectors) {
            if (SelectorMatches(selector, element)) {
                best = std::max(best, selector.Specificity());
            }
        }
        if (best >= 0) {
            matches.push_back({best, rule.sourceOrder, &rule});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        if (a.specificity != b.specificity) return a.specificity < b.specificity;
        return a.order < b.order;
    });

    std::vector<const Declaration*> importantDecls;
    for (const auto& match : matches) {
        for (const auto& decl : match.rule->declarations) {
            if (decl.important) {
                importantDecls.push_back(&decl);
            } else {
                ApplyDeclaration(decl, style, parentStyle);
            }
        }
    }

    // Inline style beats normal author rules...
    std::string inlineStyle = element.GetAttribute("style");
    if (!inlineStyle.empty()) {
        for (const auto& decl : StyleSheet::ParseDeclarationList(inlineStyle)) {
            ApplyDeclaration(decl, style, parentStyle);
        }
    }

    // ...but !important beats inline.
    for (const Declaration* decl : importantDecls) {
        ApplyDeclaration(*decl, style, parentStyle);
    }

    // Presentational attributes still common in eBook markup. "auto" (mail
    // templates write height="auto" on every <img>) is no size at all, as in
    // CSS - read as 0px it drew the picture zero pixels tall.
    if (element.tag == "img" || element.tag == "table" ||
        element.tag == "td" || element.tag == "th") {
        std::string w = element.GetAttribute("width");
        std::string h = element.GetAttribute("height");
        if (!w.empty() && !style.widthPx && !style.widthPercent) {
            if (auto len = CssLength::Parse(w); len && len->unit != CssUnit::Auto) {
                if (len->unit == CssUnit::Percent) style.widthPercent = len->value;
                else style.widthPx = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
            }
        }
        if (!h.empty() && !style.heightPx) {
            if (auto len = CssLength::Parse(h)) {
                if (len->unit != CssUnit::Percent && len->unit != CssUnit::Auto) {
                    style.heightPx = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
                }
            }
        }
    }
    if (element.tag == "a" && element.HasAttribute("href")) {
        style.isLink = true;
        style.href = element.GetAttribute("href");
        if (opts.overrideAuthorColors) style.color = opts.linkColor;
    }

    styles[&element] = style;

    for (const auto& child : element.children) {
        if (child->IsElement()) {
            ResolveElement(*child, style);
        }
    }
}

// ============================================================================
// USER-AGENT DEFAULTS
// ============================================================================

void StyleResolver::ApplyUserAgentDefaults(const std::string& tag, ComputedStyle& s) {
    const float em = s.fontSizePx;

    if (tag == "table" || tag == "input" || tag == "select" || tag == "button" ||
        tag == "textarea")
        s.borderBox = true;

    auto block = [&]() { s.display = DisplayMode::Block; };
    auto marginsV = [&](float m) { s.marginTop = m; s.marginBottom = m; };
    auto heading = [&](float scale, float marginEm) {
        block();
        s.fontSizePx = opts.baseFontSizePx * scale;
        s.bold = true;
        marginsV(marginEm * s.fontSizePx);
    };

    if (tag == "html" || tag == "body" || tag == "div" || tag == "section" ||
        tag == "article" || tag == "aside" || tag == "header" || tag == "footer" ||
        tag == "nav" || tag == "main" || tag == "figure" || tag == "figcaption" ||
        tag == "address" || tag == "fieldset" || tag == "form" || tag == "details" ||
        tag == "summary" || tag == "dl") {
        block();
        if (tag == "figure") { marginsV(em); s.marginLeft = 2 * em; s.marginRight = 2 * em; }
    }
    else if (tag == "p") { block(); marginsV(em); }
    else if (tag == "center") { block(); s.textAlign = TextAlignMode::Center; }
    else if (tag == "h1") heading(2.0f, 0.67f);
    else if (tag == "h2") heading(1.5f, 0.83f);
    else if (tag == "h3") heading(1.17f, 1.0f);
    else if (tag == "h4") heading(1.0f, 1.33f);
    else if (tag == "h5") heading(0.83f, 1.67f);
    else if (tag == "h6") heading(0.67f, 2.33f);
    else if (tag == "ul" || tag == "ol") {
        block();
        marginsV(em);
        s.paddingLeft = 2.f * em;
        s.listMarker = (tag == "ol") ? ListMarker::Decimal : ListMarker::Disc;
    }
    else if (tag == "li") { s.display = DisplayMode::ListItem; }
    else if (tag == "dt") { block(); s.bold = true; }
    else if (tag == "dd") { block(); s.marginLeft = 2.5f * em; }
    else if (tag == "blockquote") {
        block();
        marginsV(em);
        s.marginLeft = 2.5f * em;
        s.marginRight = 2.5f * em;
    }
    else if (tag == "pre") {
        block();
        marginsV(em);
        s.monospace = true;
        s.preserveWhitespace = true;
    }
    else if (tag == "hr") { block(); marginsV(0.5f * em); }
    else if (tag == "table") { s.display = DisplayMode::Table; }
    else if (tag == "tr") { s.display = DisplayMode::TableRow; }
    else if (tag == "td" || tag == "th") {
        s.display = DisplayMode::TableCell;
        // A browser's cell padding (cellpadding="1").
        s.paddingTop = s.paddingBottom = s.paddingRight = s.paddingLeft = 1.f;
        if (tag == "th") { s.bold = true; s.textAlign = TextAlignMode::Center; }
    }
    else if (tag == "b" || tag == "strong") { s.bold = true; }
    else if (tag == "i" || tag == "em" || tag == "cite" || tag == "dfn" ||
             tag == "var") { s.italic = true; }
    else if (tag == "u" || tag == "ins") { s.underline = true; }
    else if (tag == "s" || tag == "strike" || tag == "del") { s.strikethrough = true; }
    else if (tag == "code" || tag == "tt" || tag == "kbd" || tag == "samp") {
        s.monospace = true;
    }
    else if (tag == "small") { s.fontSizePx = em * 0.833f; }
    else if (tag == "big") { s.fontSizePx = em * 1.2f; }
    else if (tag == "sub" || tag == "sup") { s.fontSizePx = em * 0.75f; }
    else if (tag == "a") {
        if (!opts.overrideAuthorColors) {
            s.color = opts.linkColor;
        }
        s.underline = true;
    }
    else if (tag == "input" || tag == "textarea" || tag == "select" ||
             tag == "button") {
        // Display-only form controls render as their own block box (emails
        // wrap them in a <div>); the builder shows the value/label inside.
        s.display = DisplayMode::Block;
    }
    else if (tag == "option" || tag == "optgroup" || tag == "datalist") {
        // A <select> reads its option text directly; keep raw option text from
        // leaking into surrounding content.
        s.display = DisplayMode::Hidden;
    }
    else if (tag == "img") { s.display = DisplayMode::InlineBlock; }
    else if (tag == "nobr") { s.display = DisplayMode::Inline; s.noWrap = true; }
    else if (tag == "br" || tag == "span" || tag == "q" || tag == "abbr" ||
             tag == "mark" || tag == "font" || tag == "wbr") {
        s.display = DisplayMode::Inline;
    }
    else if (tag == "head" || tag == "script" || tag == "style" || tag == "meta" ||
             tag == "link" || tag == "title" || tag == "base") {
        s.display = DisplayMode::Hidden;
    }
    // Unknown tags stay Inline, matching browser behavior.
}

// ============================================================================
// SELECTOR MATCHING
// ============================================================================

bool StyleResolver::CompoundMatches(const SimpleSelector& part, const Node& element) {
    if (!part.tag.empty() && part.tag != "*" && element.tag != part.tag) return false;
    if (!part.id.empty() && element.GetId() != part.id) return false;
    for (const auto& cls : part.classes) {
        if (!element.HasClass(cls)) return false;
    }
    if (part.link && !(element.tag == "a" && element.HasAttribute("href"))) return false;
    for (const auto& pc : part.pseudos) {
        if (pc.kind == PseudoClass::Kind::Root) {
            if (element.tag != "html") return false;
            continue;
        }
        if (pc.kind == PseudoClass::Kind::Empty) {
            for (const auto& child : element.children)
                if (child->IsElement() ||
                    (child->type == NodeType::Text && !child->text.empty()))
                    return false;
            continue;
        }
        // The element's 1-based position among its element siblings (of its
        // tag, for -of-type), from the first or from the last.
        const Node* parent = element.parent;
        if (!parent) return false;
        int index = 0, count = 0;
        for (const auto& sib : parent->children) {
            if (!sib->IsElement()) continue;
            if (pc.ofType && sib->tag != element.tag) continue;
            ++count;
            if (sib.get() == &element) index = count;
        }
        if (index == 0) return false;
        const int pos = pc.fromEnd ? count - index + 1 : index;
        // pos == a*n + b for some n >= 0.
        if (pc.a == 0) {
            if (pos != pc.b) return false;
        } else {
            const int diff = pos - pc.b;
            if (diff % pc.a != 0 || diff / pc.a < 0) return false;
        }
    }
    for (const auto& attr : part.attributes) {
        if (!element.HasAttribute(attr.name)) return false;
        if (attr.op == 0) continue;
        std::string have = element.GetAttribute(attr.name);
        std::string want = attr.value;
        if (attr.ignoreCase) {
            for (char& ch : have) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            for (char& ch : want) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        bool ok = false;
        switch (attr.op) {
            case '=': ok = have == want; break;
            case '^': ok = !want.empty() && have.compare(0, want.size(), want) == 0; break;
            case '$': ok = !want.empty() && have.size() >= want.size() &&
                           have.compare(have.size() - want.size(), want.size(), want) == 0; break;
            case '*': ok = !want.empty() && have.find(want) != std::string::npos; break;
            case '|': ok = have == want || have.compare(0, want.size() + 1, want + "-") == 0; break;
            case '~': {
                size_t pos = 0;
                while (!ok && pos < have.size()) {
                    while (pos < have.size() && std::isspace(static_cast<unsigned char>(have[pos]))) ++pos;
                    size_t end = pos;
                    while (end < have.size() && !std::isspace(static_cast<unsigned char>(have[end]))) ++end;
                    ok = end > pos && have.compare(pos, end - pos, want) == 0 && end - pos == want.size();
                    pos = end;
                }
                break;
            }
        }
        if (!ok) return false;
    }
    return true;
}

bool StyleResolver::SelectorMatches(const Selector& selector, const Node& element) {
    if (selector.path.empty()) return false;
    if (!CompoundMatches(selector.path.back(), element)) return false;

    // Remaining compounds must match ancestors, nearest-last, in order.
    int index = static_cast<int>(selector.path.size()) - 2;
    const Node* ancestor = element.parent;
    while (index >= 0 && ancestor) {
        if (ancestor->IsElement() &&
            CompoundMatches(selector.path[static_cast<size_t>(index)], *ancestor)) {
            --index;
        }
        ancestor = ancestor->parent;
    }
    return index < 0;
}

// ============================================================================
// DECLARATION APPLICATION
// ============================================================================

namespace {

// Split a shorthand value on whitespace.
std::vector<std::string> SplitParts(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    int parens = 0;
    for (char c : value) {
        if (c == '(') ++parens;
        if (c == ')') --parens;
        if (parens == 0 && std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// Apply a 1-4 value box shorthand (margin/padding) into the four floats.
void ApplyBoxShorthand(const std::string& value, float emPx, float remPx,
                       float& top, float& right, float& bottom, float& left) {
    std::vector<float> px;
    for (const auto& part : SplitParts(value)) {
        auto len = CssLength::Parse(part);
        // Percentages against unknown container width resolve to 0.
        px.push_back(len ? len->ToPx(emPx, remPx) : 0.f);
    }
    switch (px.size()) {
        case 1: top = right = bottom = left = px[0]; break;
        case 2: top = bottom = px[0]; right = left = px[1]; break;
        case 3: top = px[0]; right = left = px[1]; bottom = px[2]; break;
        case 4: top = px[0]; right = px[1]; bottom = px[2]; left = px[3]; break;
        default: break;
    }
}

bool ContainsWord(const std::string& value, const char* word) {
    for (const auto& part : SplitParts(value)) {
        if (TrimLower(part) == word) return true;
    }
    return false;
}

} // namespace

void StyleResolver::ApplyDeclaration(const Declaration& decl, ComputedStyle& s,
                                     const ComputedStyle& parentStyle) {
    const std::string& prop = decl.property;
    const std::string value = Trim(decl.value);
    const std::string lower = TrimLower(decl.value);
    const float em = s.fontSizePx;
    const float rem = opts.baseFontSizePx;

    // `inherit` takes the parent's value - mail writes <a style="color:
    // inherit"> to keep a link in its paragraph's colour. The other keywords
    // (and inherit on a property not listed here) leave the value as it is.
    if (lower == "inherit") {
        if (prop == "color") { if (!opts.overrideAuthorColors) s.color = parentStyle.color; }
        else if (prop == "font-size") s.fontSizePx = parentStyle.fontSizePx;
        else if (prop == "font-family") { s.fontFamily = parentStyle.fontFamily; s.monospace = parentStyle.monospace; }
        else if (prop == "font-weight") s.bold = parentStyle.bold;
        else if (prop == "font-style") s.italic = parentStyle.italic;
        else if (prop == "text-decoration" || prop == "text-decoration-line") {
            s.underline = parentStyle.underline;
            s.strikethrough = parentStyle.strikethrough;
        }
        else if (prop == "text-align") s.textAlign = parentStyle.textAlign;
        return;
    }
    if (lower == "initial" || lower == "unset") return;

    if (prop == "display") {
        if (lower == "none") s.display = DisplayMode::Hidden;
        else if (lower == "block" || lower == "flex" || lower == "grid") s.display = DisplayMode::Block;
        else if (lower == "inline") s.display = DisplayMode::Inline;
        else if (lower == "inline-block") s.display = DisplayMode::InlineBlock;
        else if (lower == "list-item") s.display = DisplayMode::ListItem;
        else if (lower == "table") s.display = DisplayMode::Table;
        else if (lower == "table-row") s.display = DisplayMode::TableRow;
        else if (lower == "table-cell") s.display = DisplayMode::TableCell;
        else if (lower == "inline-table" || lower == "inline-flex" || lower == "inline-grid")
            s.display = DisplayMode::InlineBlock;
    }
    else if (prop == "color") {
        if (opts.overrideAuthorColors) return;
        if (auto color = CssColor::Parse(lower)) s.color = *color;
    }
    else if (prop == "background-color") {
        if (opts.overrideAuthorColors) return;
        if (auto color = CssColor::Parse(lower)) s.backgroundColor = *color;
    }
    else if (prop == "background" || prop == "background-image") {
        // Layers split on top-level commas; each may hold url(...), a size
        // after '/', and (in the shorthand's last layer) a colour.
        std::vector<std::string> layers;
        {
            std::string current;
            int parens = 0;
            for (char c : value) {
                if (c == '(') ++parens;
                if (c == ')') --parens;
                if (c == ',' && parens == 0) { layers.push_back(current); current.clear(); }
                else current += c;
            }
            layers.push_back(current);
        }
        std::vector<std::string> urls;
        for (const auto& rawLayer : layers) {
            const std::string layer = Trim(rawLayer);
            std::string low = TrimLower(layer);
            size_t u = low.find("url(");
            if (u != std::string::npos) {
                size_t close = layer.find(')', u);
                std::string url = Trim(layer.substr(u + 4, close == std::string::npos
                                                               ? std::string::npos : close - u - 4));
                if (url.size() >= 2 && (url.front() == '\'' || url.front() == '"')) {
                    url = url.substr(1, url.size() - 2);
                }
                if (!url.empty()) urls.push_back(url);
            }
            if (prop == "background" && urls.size() == 1 && u != std::string::npos) {
                if (low.find("contain") != std::string::npos) s.backgroundSize = BackgroundSizeMode::Contain;
                else if (low.find("cover") != std::string::npos) s.backgroundSize = BackgroundSizeMode::Cover;
            }
        }
        s.backgroundImages = urls;   // the shorthand (and 'none') resets them
        if (prop == "background" && !opts.overrideAuthorColors) {
            // The colour sits in the last layer, anywhere among its words.
            for (const auto& part : SplitParts(TrimLower(layers.back()))) {
                if (part.rfind("url(", 0) == 0) continue;
                if (auto color = CssColor::Parse(part)) { s.backgroundColor = *color; break; }
            }
        }
    }
    else if (prop == "background-size") {
        if (lower.rfind("contain", 0) == 0) s.backgroundSize = BackgroundSizeMode::Contain;
        else if (lower.rfind("cover", 0) == 0) s.backgroundSize = BackgroundSizeMode::Cover;
        else s.backgroundSize = BackgroundSizeMode::Auto;
    }
    else if (prop == "max-width") {
        s.maxWidthPx.reset();
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit != CssUnit::Percent && len->unit != CssUnit::Auto)
                s.maxWidthPx = len->ToPx(em, rem);
        }
    }
    else if (prop == "font-size") {
        if (lower == "smaller") { s.fontSizePx = parentStyle.fontSizePx * 0.833f; return; }
        if (lower == "larger")  { s.fontSizePx = parentStyle.fontSizePx * 1.2f;  return; }
        static const std::pair<const char*, float> kKeywords[] = {
            {"xx-small", 0.5787f}, {"x-small", 0.6944f}, {"small", 0.8333f},
            {"medium", 1.f}, {"large", 1.2f}, {"x-large", 1.44f}, {"xx-large", 1.728f}};
        for (const auto& kw : kKeywords) {
            if (lower == kw.first) {
                s.fontSizePx = opts.baseFontSizePx * kw.second;
                return;
            }
        }
        if (auto len = CssLength::Parse(lower)) {
            // font-size em/% resolve against the PARENT font size.
            s.fontSizePx = len->ToPx(parentStyle.fontSizePx, rem, parentStyle.fontSizePx);
            if (s.fontSizePx <= 0) s.fontSizePx = parentStyle.fontSizePx;
        }
    }
    else if (prop == "font-family") {
        std::string family = value;
        size_t comma = family.find(',');
        if (comma != std::string::npos) family = family.substr(0, comma);
        family = Trim(family);
        if (family.size() >= 2 && (family.front() == '"' || family.front() == '\'')) {
            family = family.substr(1, family.size() - 2);
        }
        std::string genericCheck = TrimLower(family);
        if (genericCheck == "monospace") {
            s.monospace = true;
        } else if (!family.empty()) {
            s.fontFamily = family;
            s.monospace = genericCheck.find("mono") != std::string::npos ||
                          genericCheck.find("courier") != std::string::npos;
        }
    }
    else if (prop == "font-weight") {
        if (lower == "bold" || lower == "bolder") s.bold = true;
        else if (lower == "normal" || lower == "lighter") s.bold = false;
        else {
            char* end = nullptr;
            long weight = std::strtol(lower.c_str(), &end, 10);
            if (end != lower.c_str()) s.bold = weight >= 600;
        }
    }
    else if (prop == "font-style") {
        s.italic = (lower == "italic" || lower == "oblique");
    }
    else if (prop == "font") {
        // Shorthand: honor the recognizable words; sizes/families need the
        // full grammar and are skipped.
        if (ContainsWord(lower, "italic") || ContainsWord(lower, "oblique")) s.italic = true;
        if (ContainsWord(lower, "bold")) s.bold = true;
    }
    else if (prop == "text-decoration" || prop == "text-decoration-line") {
        if (lower == "none") { s.underline = false; s.strikethrough = false; }
        else {
            if (ContainsWord(lower, "underline")) s.underline = true;
            if (ContainsWord(lower, "line-through")) s.strikethrough = true;
        }
    }
    else if (prop == "text-align") {
        if (lower == "left" || lower == "start") s.textAlign = TextAlignMode::Left;
        else if (lower == "right" || lower == "end") s.textAlign = TextAlignMode::Right;
        else if (lower == "center") s.textAlign = TextAlignMode::Center;
        else if (lower == "justify") s.textAlign = TextAlignMode::Justify;
    }
    else if (prop == "vertical-align") {
        if (lower == "middle") s.verticalAlign = VerticalAlignMode::Middle;
        else if (lower == "top" || lower == "text-top") s.verticalAlign = VerticalAlignMode::Top;
        else if (lower == "bottom" || lower == "text-bottom")
            s.verticalAlign = VerticalAlignMode::Bottom;
        else if (lower == "baseline") s.verticalAlign = VerticalAlignMode::Baseline;
    }
    else if (prop == "line-height") {
        if (lower == "normal") { s.lineHeight = 1.4f; return; }
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit == CssUnit::Number) s.lineHeight = len->value;
            else if (len->unit == CssUnit::Percent) s.lineHeight = len->value / 100.f;
            else if (em > 0) s.lineHeight = len->ToPx(em, rem) / em;
        }
    }
    else if (prop == "white-space") {
        s.preserveWhitespace = (lower == "pre" || lower == "pre-wrap" ||
                                lower == "pre-line");
        s.noWrap = (lower == "nowrap");
    }
    else if (prop == "float") {
        if (lower == "left") s.floatMode = FloatMode::Left;
        else if (lower == "right") s.floatMode = FloatMode::Right;
        else if (lower == "none") s.floatMode = FloatMode::NoFloat;
    }
    else if (prop == "box-sizing") {
        if (lower == "border-box") s.borderBox = true;
        else if (lower == "content-box") s.borderBox = false;
    }
    else if (prop == "border-collapse") {
        s.borderCollapse = (lower == "collapse");
    }
    else if (prop == "border-spacing") {
        auto parts = SplitParts(lower);
        if (!parts.empty()) {
            if (auto len = CssLength::Parse(parts[0])) s.borderSpacing = len->ToPx(em, rem);
        }
    }
    else if (prop == "border-radius") {
        auto parts = SplitParts(lower);
        if (!parts.empty()) {
            if (auto len = CssLength::Parse(parts[0])) {
                if (len->unit != CssUnit::Percent) s.borderRadius = len->ToPx(em, rem);
            }
        }
    }
    else if (prop == "list-style-type" || prop == "list-style") {
        if (lower == "none") s.listMarker = ListMarker::NoMarker;
        else if (lower == "disc") s.listMarker = ListMarker::Disc;
        else if (lower == "circle") s.listMarker = ListMarker::Circle;
        else if (lower == "square") s.listMarker = ListMarker::Square;
        else if (lower == "decimal") s.listMarker = ListMarker::Decimal;
        else if (lower == "lower-alpha" || lower == "lower-latin") s.listMarker = ListMarker::LowerAlpha;
        else if (lower == "upper-alpha" || lower == "upper-latin") s.listMarker = ListMarker::UpperAlpha;
        else if (lower == "lower-roman") s.listMarker = ListMarker::LowerRoman;
        else if (lower == "upper-roman") s.listMarker = ListMarker::UpperRoman;
    }
    else if (prop == "margin") {
        ApplyBoxShorthand(lower, em, rem, s.marginTop, s.marginRight,
                          s.marginBottom, s.marginLeft);
        // Which sides are 'auto' (1-4 values, as for the lengths).
        auto parts = SplitParts(lower);
        auto isAuto = [&](size_t index) { return index < parts.size() && parts[index] == "auto"; };
        switch (parts.size()) {
            case 1: s.marginLeftAuto = s.marginRightAuto = isAuto(0); break;
            case 2: case 3: s.marginLeftAuto = s.marginRightAuto = isAuto(1); break;
            case 4: s.marginRightAuto = isAuto(1); s.marginLeftAuto = isAuto(3); break;
            default: break;
        }
    }
    else if (prop == "margin-top") {
        if (auto len = CssLength::Parse(lower)) s.marginTop = len->ToPx(em, rem);
    }
    else if (prop == "margin-right") {
        s.marginRightAuto = (lower == "auto");
        if (auto len = CssLength::Parse(lower)) s.marginRight = len->ToPx(em, rem);
    }
    else if (prop == "margin-bottom") {
        if (auto len = CssLength::Parse(lower)) s.marginBottom = len->ToPx(em, rem);
    }
    else if (prop == "margin-left") {
        s.marginLeftAuto = (lower == "auto");
        if (auto len = CssLength::Parse(lower)) s.marginLeft = len->ToPx(em, rem);
    }
    else if (prop == "padding") {
        ApplyBoxShorthand(lower, em, rem, s.paddingTop, s.paddingRight,
                          s.paddingBottom, s.paddingLeft);
    }
    else if (prop == "padding-top") {
        if (auto len = CssLength::Parse(lower)) s.paddingTop = len->ToPx(em, rem);
    }
    else if (prop == "padding-right") {
        if (auto len = CssLength::Parse(lower)) s.paddingRight = len->ToPx(em, rem);
    }
    else if (prop == "padding-bottom") {
        if (auto len = CssLength::Parse(lower)) s.paddingBottom = len->ToPx(em, rem);
    }
    else if (prop == "padding-left") {
        if (auto len = CssLength::Parse(lower)) s.paddingLeft = len->ToPx(em, rem);
    }
    else if (prop == "text-indent") {
        // Mapped to padding-left on the paragraph for v1 (no first-line
        // indent support in Label yet).
        if (auto len = CssLength::Parse(lower)) {
            float px = len->ToPx(em, rem);
            if (px > 0) s.paddingLeft += px;
        }
    }
    else if (prop == "width") {
        // The later declaration replaces the earlier one: width:100%!important
        // over an inline width:600px (a newsletter's narrow-screen rule) is
        // 100%, not 600px with a percentage beside it.
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit == CssUnit::Percent) {
                s.widthPercent = len->value;
                s.widthPx.reset();
            } else if (len->unit == CssUnit::Auto) {
                s.widthPercent.reset();
                s.widthPx.reset();
            } else {
                s.widthPx = len->ToPx(em, rem);
                s.widthPercent.reset();
            }
        }
    }
    else if (prop == "height") {
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit != CssUnit::Percent && len->unit != CssUnit::Auto) {
                s.heightPx = len->ToPx(em, rem);
            }
        }
    }
    else if (prop == "border" || prop == "border-top" || prop == "border-bottom" ||
             prop == "border-left" || prop == "border-right") {
        // Uniform border approximation: width + color from the shorthand.
        for (const auto& part : SplitParts(lower)) {
            if (auto len = CssLength::Parse(part)) {
                if (len->unit != CssUnit::Number || len->value == 0) {
                    s.borderWidth = len->ToPx(em, rem);
                    continue;
                }
            }
            if (part == "thin") s.borderWidth = 1;
            else if (part == "medium") s.borderWidth = 3;
            else if (part == "thick") s.borderWidth = 5;
            else if (part == "none" || part == "hidden") s.borderWidth = 0;
            else if (auto color = CssColor::Parse(part)) s.borderColor = *color;
        }
    }
    else if (prop == "border-width") {
        if (auto len = CssLength::Parse(lower)) s.borderWidth = len->ToPx(em, rem);
    }
    else if (prop == "border-color") {
        if (auto color = CssColor::Parse(lower)) s.borderColor = *color;
    }
    // Unrecognized properties are ignored (vertical-align, float, etc.).
}

} // namespace HTML
} // namespace UltraCanvas
