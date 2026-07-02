// core/HTMLReader/HTMLStyleResolver.cpp
// CSS cascade: user-agent defaults → author rules → inline styles.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLStyleResolver.h"

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
    style.color = parentStyle.color;
    style.textAlign = parentStyle.textAlign;
    style.lineHeight = parentStyle.lineHeight;
    style.listMarker = parentStyle.listMarker;

    ApplyUserAgentDefaults(element.tag, style);

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

    // Presentational attributes still common in eBook markup.
    if (element.tag == "img" || element.tag == "table" ||
        element.tag == "td" || element.tag == "th") {
        std::string w = element.GetAttribute("width");
        std::string h = element.GetAttribute("height");
        if (!w.empty() && !style.widthPx && !style.widthPercent) {
            if (auto len = CssLength::Parse(w)) {
                if (len->unit == CssUnit::Percent) style.widthPercent = len->value;
                else style.widthPx = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
            }
        }
        if (!h.empty() && !style.heightPx) {
            if (auto len = CssLength::Parse(h)) {
                if (len->unit != CssUnit::Percent) {
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
        s.paddingTop = s.paddingBottom = 0.25f * em;
        s.paddingRight = s.paddingLeft = 0.4f * em;
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
    else if (tag == "img") { s.display = DisplayMode::InlineBlock; }
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

    if (lower == "inherit" || lower == "initial" || lower == "unset") return;

    if (prop == "display") {
        if (lower == "none") s.display = DisplayMode::Hidden;
        else if (lower == "block" || lower == "flex" || lower == "grid") s.display = DisplayMode::Block;
        else if (lower == "inline") s.display = DisplayMode::Inline;
        else if (lower == "inline-block") s.display = DisplayMode::InlineBlock;
        else if (lower == "list-item") s.display = DisplayMode::ListItem;
        else if (lower == "table") s.display = DisplayMode::Table;
        else if (lower == "table-row") s.display = DisplayMode::TableRow;
        else if (lower == "table-cell") s.display = DisplayMode::TableCell;
    }
    else if (prop == "color") {
        if (opts.overrideAuthorColors) return;
        if (auto color = CssColor::Parse(lower)) s.color = *color;
    }
    else if (prop == "background-color" || prop == "background") {
        if (opts.overrideAuthorColors) return;
        // For the 'background' shorthand only a plain color layer is honored.
        if (auto color = CssColor::Parse(SplitParts(lower).empty()
                                             ? lower
                                             : SplitParts(lower)[0])) {
            s.backgroundColor = *color;
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
    }
    else if (prop == "margin-top") {
        if (auto len = CssLength::Parse(lower)) s.marginTop = len->ToPx(em, rem);
    }
    else if (prop == "margin-right") {
        if (auto len = CssLength::Parse(lower)) s.marginRight = len->ToPx(em, rem);
    }
    else if (prop == "margin-bottom") {
        if (auto len = CssLength::Parse(lower)) s.marginBottom = len->ToPx(em, rem);
    }
    else if (prop == "margin-left") {
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
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit == CssUnit::Percent) s.widthPercent = len->value;
            else if (len->unit != CssUnit::Auto) s.widthPx = len->ToPx(em, rem);
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
