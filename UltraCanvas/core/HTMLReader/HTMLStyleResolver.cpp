// core/HTMLReader/HTMLStyleResolver.cpp
// CSS cascade: user-agent defaults → author rules → inline styles.
// Version: 1.19.0 - flex and grid: display: flex / grid / inline-flex / inline-grid set
//                  BoxLayoutMode; flex, flex-*, order, gap, justify-* / align-* /
//                  place-*, grid-template-columns / -rows / -areas (repeat(),
//                  minmax(), fit-content(), auto-fill / auto-fit), grid-template,
//                  grid-column / -row / -area, grid-auto-flow. Counts, lines and
//                  spans are capped at kMaxGridLines.
// Version: 1.18.0 - selector matching is CSSStyleSheet.h's (SelectorMatches /
//                  MatchingRules over NodeSelectorTraits); the copy that lived
//                  here is gone, so the SVG reader and this resolver match alike
// Version: 1.17.0 - an inline !important declaration beats a style sheet's
//                  !important one, as in CSS: style="color:#fff !important" on
//                  a mail's button link no longer loses to the template's
//                  a.link{color:#ff4554 !important} (red text on a red button)
// Version: 1.16.0 - merged with main's 1.2.1-1.4.0 (attribute selectors,
//                  structural pseudo-classes, float / clear, box-sizing as borderBox)
// Version: 1.15.0 - letter-spacing (px / em, inherited; normal = 0)
// Version: 1.14.0 - line-height kept for labels (px for lengths / %, a factor for
//                  numbers, normal clears it); overflow
// Version: 1.13.0 - quirks mode (no standards doctype, most mail): a table does not
//                  inherit text-align, as in browsers
// Version: 1.12.0 - height in percent (CSS and the height attribute); a later width /
//                  height replaces an earlier one of either kind
// Version: 1.11.0 - min-width, min-height, max-height in percent too
// Version: 1.10.0 - max-width in percent (maxWidthPercent)
// Version: 1.9.0 - min-width, min-height, max-height (px; none / auto reset them)
// Version: 1.8.0 - box-sizing (content-box / border-box)
// Version: 1.7.0 - borders per side: border, border-top/-right/-bottom/-left,
//                  border-width / -style / -color (1-4 values) and the per-side
//                  longhands; a border without a style draws nothing, as in CSS;
//                  dashed and dotted kept. <hr> draws a browser's inset rule.
// Version: 1.6.0 - border-radius in percent; <img border="N"> (in the image's
//                  colour); a border shorthand without a colour uses the text
//                  colour (currentColor).
// Version: 1.5.0 - object-fit and object-position (the background-position syntax)
// Version: 1.4.0 - background-repeat: repeat / repeat-x / repeat-y / no-repeat,
//                  one or two values, per layer (initial: repeat).
// Version: 1.3.0 - background-position (keywords, %, lengths, edge offsets);
//                  background size and position kept per layer.
// From main:
// Version: 1.4.0 - structural pseudo-classes match; float, and <table
//                  align="left|right"> / <img align="left|right"> floats;
//                  clear and <br clear>
// Version: 1.3.0 - attribute selectors match; box-sizing
// Version: 1.2.2 - a later width declaration replaces an earlier one (px vs %)
// Version: 1.2.1 - width/height="auto" on <img>/<table>/<td> is no size, not 0px
// Version: 1.2.0 - table presentational attributes (nowrap, valign,
//                  cellpadding, cellspacing, tr align); white-space: nowrap;
//                  border-collapse / border-spacing / border-radius; cells
//                  default to a browser's 1px padding; `inherit` for
//                  color, font and text properties; background images and
//                  size, margin: auto, max-width.
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLStyleResolver.h"

#include <cctype>
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace UltraCanvas {
namespace HTML {

// ============================================================================
// PUBLIC API
// ============================================================================

void StyleResolver::Resolve(Document& document, const ResolverOptions& options) {
    styles.clear();
    opts = options;
    quirks = document.quirksMode;

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
    // So does <img align="left|right">: the text beside it runs around it.
    if (element.tag == "table" || element.tag == "img") {
        const std::string align = TrimLower(element.GetAttribute("align"));
        if (align == "left") style.floatMode = FloatMode::Left;
        else if (align == "right") style.floatMode = FloatMode::Right;
    }
    // <br clear="all|left|right">: what follows starts below the floats.
    if (element.tag == "br" && element.HasAttribute("clear")) {
        style.clear = TrimLower(element.GetAttribute("clear")) != "none";
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
            if (len->unit != CssUnit::Percent) {
                BorderSide side;
                side.width = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
                side.style = BorderLineStyle::Solid;
                side.currentColor = false;          // black, as before
                style.SetAllBorders(side);
            }
        }
    }
    // <img border="N">: an N-pixel solid border in the image's colour - the
    // link colour for an image inside a link. border="0" (mail's usual) none.
    if (element.tag == "img" && element.HasAttribute("border")) {
        if (auto len = CssLength::Parse(TrimLower(element.GetAttribute("border")))) {
            if (len->unit != CssUnit::Percent) {
                BorderSide side;
                side.width = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
                side.style = BorderLineStyle::Solid;   // in currentColor
                style.SetAllBorders(side);
            }
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
    style.lineHeightSet = parentStyle.lineHeightSet;
    style.letterSpacingPx = parentStyle.letterSpacingPx;
    style.lineHeightPx = parentStyle.lineHeightPx;
    style.listMarker = parentStyle.listMarker;

    ApplyUserAgentDefaults(element.tag, style);
    // Quirks mode (a page without a standards doctype - most HTML mail): a
    // table starts its own text alignment instead of inheriting it, as
    // browsers' quirks style sheets say. <td align="center"> around a mail's
    // content tables centres the tables, not every line of text in them.
    if (quirks && element.tag == "table") style.textAlign = TextAlignMode::Left;
    ApplyAlignAttribute(element, style);
    ApplyLegacyAttributes(element, style);

    // Author rules, lowest specificity first so later Apply wins. !important
    // declarations are collected and re-applied last.
    std::vector<const Declaration*> importantDecls;
    for (const Rule* rule : MatchingRules<NodeSelectorTraits>(sheet, element)) {
        for (const auto& decl : rule->declarations) {
            if (decl.important) {
                importantDecls.push_back(&decl);
            } else {
                ApplyDeclaration(decl, style, parentStyle);
            }
        }
    }

    // The cascade, weakest first (CSS Cascading 4, 6.1): normal author rules,
    // normal inline declarations, !important author rules, !important inline
    // declarations - within one origin and importance the style attribute
    // wins. So an inline style beats a normal rule...
    std::vector<Declaration> inlineImportant;
    std::string inlineStyle = element.GetAttribute("style");
    if (!inlineStyle.empty()) {
        for (auto& decl : StyleSheet::ParseDeclarationList(inlineStyle)) {
            if (decl.important) inlineImportant.push_back(std::move(decl));
            else ApplyDeclaration(decl, style, parentStyle);
        }
    }

    // ...an !important rule beats an inline style...
    for (const Declaration* decl : importantDecls) {
        ApplyDeclaration(*decl, style, parentStyle);
    }

    // ...and an !important inline declaration beats both. Mail templates
    // write the button's own colour that way over a stylesheet's
    // a.link{color:...!important} - applied last, the button's white text
    // was painted in the button's red.
    for (const Declaration& decl : inlineImportant) {
        ApplyDeclaration(decl, style, parentStyle);
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
        if (!h.empty() && !style.heightPx && !style.heightPercent) {
            if (auto len = CssLength::Parse(h)) {
                if (len->unit == CssUnit::Percent) style.heightPercent = len->value;
                else if (len->unit != CssUnit::Auto)
                    style.heightPx = len->ToPx(style.fontSizePx, opts.baseFontSizePx);
            }
        }
    }
    if (element.tag == "a" && element.HasAttribute("href")) {
        style.isLink = true;
        style.href = element.GetAttribute("href");
        if (opts.overrideAuthorColors) style.color = opts.linkColor;
    }

    // Borders without a colour of their own take the text colour.
    for (BorderSide* side : { &style.borderTop, &style.borderRight,
                              &style.borderBottom, &style.borderLeft }) {
        if (side->currentColor) side->color = style.color;
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
    else if (tag == "hr") {
        block(); marginsV(0.5f * em);
        // A browser's rule: a 1px inset border round an empty box - darker
        // above and on the left, lighter below and on the right.
        BorderSide dark, light;
        dark.width = light.width = 1.f;
        dark.style = light.style = BorderLineStyle::Solid;
        dark.color = CssColor{154, 154, 154, 255};
        light.color = CssColor{238, 238, 238, 255};
        dark.currentColor = light.currentColor = false;
        s.borderTop = s.borderLeft = dark;
        s.borderBottom = s.borderRight = light;
    }
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

// A border-style keyword; nullopt when `word` is not one.
std::optional<BorderLineStyle> ParseBorderLineStyle(const std::string& word) {
    if (word == "none" || word == "hidden") return BorderLineStyle::NoBorder;
    if (word == "dashed") return BorderLineStyle::Dashed;
    if (word == "dotted") return BorderLineStyle::Dotted;
    if (word == "solid" || word == "double" || word == "groove" || word == "ridge" ||
        word == "inset" || word == "outset") return BorderLineStyle::Solid;
    return std::nullopt;
}

// A border-width value: a length or thin / medium / thick.
std::optional<float> ParseBorderWidth(const std::string& word, float emPx, float remPx) {
    if (word == "thin") return 1.f;
    if (word == "medium") return 3.f;
    if (word == "thick") return 5.f;
    if (auto len = CssLength::Parse(word)) {
        if (len->unit == CssUnit::Percent) return std::nullopt;
        // A bare number other than 0 is not a length.
        if (len->unit == CssUnit::Number && len->value != 0) return std::nullopt;
        return std::max(0.f, len->ToPx(emPx, remPx));
    }
    return std::nullopt;
}

// A border colour; nullopt for currentColor (the element's text colour,
// filled in later) and for a word that is no colour.
std::optional<CssColor> ParseBorderColor(const std::string& word, bool& isCurrent) {
    isCurrent = (word == "currentcolor");
    if (isCurrent) return std::nullopt;
    return CssColor::Parse(word);
}

// A border side shorthand (border, border-top, ...): width, style and colour
// in any order; what it leaves out goes back to its initial value (medium,
// none, the text colour).
BorderSide ParseBorderSide(const std::string& value, float emPx, float remPx) {
    BorderSide side;
    for (const auto& part : SplitParts(value)) {
        bool isCurrent = false;
        if (auto st = ParseBorderLineStyle(part)) side.style = *st;
        else if (auto w = ParseBorderWidth(part, emPx, remPx)) side.width = *w;
        else if (auto c = ParseBorderColor(part, isCurrent)) {
            side.color = *c;
            side.currentColor = false;
        }
    }
    return side;
}

// A 1-4 value list (top, right, bottom, left - as margin), each value parsed
// by `parse` and handed to `apply` with its side; values that do not parse
// leave their side as it is.
template <typename T, typename Parse, typename Apply>
void ForBoxSides(const std::string& value, Parse parse, Apply apply) {
    std::vector<std::optional<T>> v;
    for (const auto& part : SplitParts(value)) v.push_back(parse(part));
    if (v.empty() || v.size() > 4) return;
    const auto& top = v[0];
    const auto& right = v.size() > 1 ? v[1] : v[0];
    const auto& bottom = v.size() > 2 ? v[2] : v[0];
    const auto& left = v.size() > 3 ? v[3] : right;
    if (top) apply(0, *top);
    if (right) apply(1, *right);
    if (bottom) apply(2, *bottom);
    if (left) apply(3, *left);
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

// Split on `sep` outside parentheses (url(...), rgb(...)).
std::vector<std::string> SplitTopLevel(const std::string& value, char sep) {
    std::vector<std::string> out;
    std::string current;
    int parens = 0;
    for (char c : value) {
        if (c == '(') ++parens;
        if (c == ')') --parens;
        if (c == sep && parens == 0) { out.push_back(current); current.clear(); }
        else current += c;
    }
    out.push_back(current);
    return out;
}

// background-size: contain / cover; anything else (auto, lengths) keeps the
// picture at its own size.
BackgroundSizeMode ParseBackgroundSize(const std::string& text) {
    for (const auto& part : SplitParts(TrimLower(text))) {
        if (part == "contain") return BackgroundSizeMode::Contain;
        if (part == "cover") return BackgroundSizeMode::Cover;
    }
    return BackgroundSizeMode::Auto;
}

// background-repeat: repeat-x, repeat-y, or one value per axis (repeat,
// space, round, no-repeat); nullopt when the text names none of them. The
// shorthand's whole layer can be passed: other words are skipped.
std::optional<BackgroundRepeat> ParseBackgroundRepeat(const std::string& text) {
    std::vector<bool> axes;
    for (const auto& part : SplitParts(TrimLower(text))) {
        if (part == "repeat-x") return BackgroundRepeat{ true, false };
        if (part == "repeat-y") return BackgroundRepeat{ false, true };
        if (part == "repeat" || part == "space" || part == "round") axes.push_back(true);
        else if (part == "no-repeat") axes.push_back(false);
    }
    if (axes.empty()) return std::nullopt;
    return BackgroundRepeat{ axes[0], axes.size() > 1 ? axes[1] : axes[0] };
}

// background-position: keywords (left / center / right, top / center /
// bottom), percentages and lengths, in 1 to 4 values ("right 10px bottom
// 20%"). Words that are not part of a position (no-repeat, a colour, fixed)
// are skipped, so the shorthand's rest can be passed as it is. Nothing that
// looks like a position gives nullopt.
std::optional<BackgroundPosition> ParseBackgroundPosition(const std::string& text,
                                                          float emPx, float remPx) {
    struct Token { std::string keyword; std::optional<CssLength> length; };
    std::vector<Token> tokens;
    for (const auto& part : SplitParts(TrimLower(text))) {
        if (part == "left" || part == "right" || part == "top" || part == "bottom" ||
            part == "center") {
            tokens.push_back({ part, std::nullopt });
        } else if (auto len = CssLength::Parse(part)) {
            // A bare number is a length only when it is 0.
            if (len->unit == CssUnit::Auto) continue;
            if (len->unit == CssUnit::Number && len->value != 0.f) continue;
            tokens.push_back({ std::string(), len });
        }
    }
    if (tokens.empty() || tokens.size() > 4) return std::nullopt;

    auto fraction = [](float f) { return BackgroundAxisPosition{ f, false, false }; };
    auto fromLength = [&](const CssLength& len, bool fromEnd) {
        if (len.unit == CssUnit::Percent) {
            const float f = len.value / 100.f;
            return fraction(fromEnd ? 1.f - f : f);
        }
        return BackgroundAxisPosition{ len.ToPx(emPx, remPx), true, fromEnd };
    };
    auto keywordFraction = [](const std::string& k) {
        return (k == "left" || k == "top") ? 0.f : (k == "right" || k == "bottom") ? 1.f : 0.5f;
    };
    auto isHorizontal = [](const std::string& k) { return k == "left" || k == "right"; };
    auto isVertical   = [](const std::string& k) { return k == "top" || k == "bottom"; };

    BackgroundPosition p;
    p.x = fraction(0.5f);
    p.y = fraction(0.5f);

    if (tokens.size() == 1) {
        const Token& t = tokens[0];
        if (t.length) { p.x = fromLength(*t.length, false); return p; }
        if (isVertical(t.keyword)) p.y = fraction(keywordFraction(t.keyword));
        else p.x = fraction(keywordFraction(t.keyword));
        return p;
    }
    if (tokens.size() == 2) {
        Token a = tokens[0], b = tokens[1];
        // "top left", "center left": the vertical word came first.
        if ((!a.keyword.empty() && isVertical(a.keyword)) ||
            (!b.keyword.empty() && isHorizontal(b.keyword))) std::swap(a, b);
        p.x = a.length ? fromLength(*a.length, false) : fraction(keywordFraction(a.keyword));
        p.y = b.length ? fromLength(*b.length, false) : fraction(keywordFraction(b.keyword));
        return p;
    }
    // 3 or 4 values: edge keywords, each maybe followed by its offset.
    bool haveX = false, haveY = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.keyword.empty()) return std::nullopt;   // an offset without its edge
        std::optional<CssLength> offset;
        if (i + 1 < tokens.size() && tokens[i + 1].length) offset = tokens[++i].length;
        const bool end = (t.keyword == "right" || t.keyword == "bottom");
        BackgroundAxisPosition v = offset ? fromLength(*offset, end)
                                          : fraction(keywordFraction(t.keyword));
        if (isHorizontal(t.keyword) || (t.keyword == "center" && !haveX && haveY)) {
            p.x = v; haveX = true;
        } else if (isVertical(t.keyword) || t.keyword == "center") {
            if (t.keyword == "center" && !haveX && !haveY) { p.x = v; haveX = true; }
            else { p.y = v; haveY = true; }
        }
    }
    return p;
}

bool ContainsWord(const std::string& value, const char* word) {
    for (const auto& part : SplitParts(value)) {
        if (TrimLower(part) == word) return true;
    }
    return false;
}

// ---- flex and grid --------------------------------------------------------

// An integer, as a grid line or a repeat() count is written; nullopt for
// anything else (1.5, 3px, a name).
std::optional<int> ParseInteger(const std::string& word) {
    if (word == "0") return 0;          // CssLength reads a bare 0 as 0px
    auto len = CssLength::Parse(word);
    if (!len || len->unit != CssUnit::Number) return std::nullopt;
    if (len->value != std::floor(len->value)) return std::nullopt;
    const float clamped = std::clamp(len->value, -1.0e6f, 1.0e6f);
    return static_cast<int>(clamped);
}

// A plain non-negative number (flex-grow, flex-shrink).
std::optional<float> ParseNumber(const std::string& word) {
    if (word == "0") return 0.f;        // CssLength reads a bare 0 as 0px
    auto len = CssLength::Parse(word);
    if (!len || len->unit != CssUnit::Number || !(len->value >= 0.f)) return std::nullopt;
    return std::min(len->value, 1.0e6f);
}

// One alignment value of justify-* / align-* / place-*: the keyword, with
// safe / unsafe and first / last (baseline) dropped.
std::optional<BoxAlignMode> ParseBoxAlign(const std::string& text) {
    std::string word;
    for (const auto& part : SplitParts(TrimLower(text))) {
        if (part == "safe" || part == "unsafe" || part == "first" || part == "last") continue;
        if (!word.empty()) return std::nullopt;
        word = part;
    }
    if (word == "normal" || word == "auto") return BoxAlignMode::Unset;
    if (word == "start" || word == "flex-start" || word == "self-start" || word == "left")
        return BoxAlignMode::Start;
    if (word == "end" || word == "flex-end" || word == "self-end" || word == "right")
        return BoxAlignMode::End;
    if (word == "center") return BoxAlignMode::Center;
    if (word == "stretch") return BoxAlignMode::Stretch;
    if (word == "baseline") return BoxAlignMode::Baseline;
    if (word == "space-between") return BoxAlignMode::SpaceBetween;
    if (word == "space-around") return BoxAlignMode::SpaceAround;
    if (word == "space-evenly") return BoxAlignMode::SpaceEvenly;
    return std::nullopt;
}

// place-items / place-content / place-self: "<align> [<justify>]", one value
// meaning both. The parts may themselves be two words (safe center).
bool ParsePlace(const std::string& text, BoxAlignMode& align, BoxAlignMode& justify) {
    const std::vector<std::string> parts = SplitParts(TrimLower(text));
    // Split where the first value ends: a modifier joins the word after it.
    std::vector<std::string> values;
    std::string pending;
    for (const auto& part : parts) {
        if (part == "safe" || part == "unsafe" || part == "first" || part == "last") {
            pending += part + " ";
            continue;
        }
        values.push_back(pending + part);
        pending.clear();
    }
    if (values.empty() || values.size() > 2) return false;
    auto a = ParseBoxAlign(values[0]);
    auto j = ParseBoxAlign(values.size() > 1 ? values[1] : values[0]);
    if (!a || !j) return false;
    align = *a;
    justify = *j;
    return true;
}

// A gap length in px; percentages (of a size not known here) and normal are 0.
std::optional<float> ParseGap(const std::string& word, float emPx, float remPx) {
    if (word == "normal") return 0.f;
    auto len = CssLength::Parse(word);
    if (!len || len->unit == CssUnit::Auto) return std::nullopt;
    if (len->unit == CssUnit::Number && len->value != 0.f) return std::nullopt;
    if (len->unit == CssUnit::Percent) return 0.f;
    return std::max(0.f, len->ToPx(emPx, remPx));
}

// One bound of a grid track: a length, a percentage, Nfr, auto,
// min-content, max-content or fit-content(<length>).
std::optional<GridTrackBound> ParseGridTrackBound(const std::string& word, float emPx, float remPx) {
    using Kind = GridTrackBound::Kind;
    if (word == "auto") return GridTrackBound{ Kind::Auto, 0.f };
    if (word == "min-content") return GridTrackBound{ Kind::MinContent, 0.f };
    if (word == "max-content") return GridTrackBound{ Kind::MaxContent, 0.f };
    if (word.rfind("fit-content(", 0) == 0 && word.back() == ')') {
        auto len = CssLength::Parse(word.substr(12, word.size() - 13));
        if (!len || len->unit == CssUnit::Auto || len->unit == CssUnit::Percent) return std::nullopt;
        return GridTrackBound{ Kind::FitContent, std::max(0.f, len->ToPx(emPx, remPx)) };
    }
    if (word.size() > 2 && word.compare(word.size() - 2, 2, "fr") == 0) {
        auto n = ParseNumber(word.substr(0, word.size() - 2));
        if (!n) return std::nullopt;
        return GridTrackBound{ Kind::Fr, *n };
    }
    auto len = CssLength::Parse(word);
    if (!len || len->unit == CssUnit::Auto) return std::nullopt;
    if (len->unit == CssUnit::Number && len->value != 0.f) return std::nullopt;
    if (len->unit == CssUnit::Percent) return GridTrackBound{ Kind::Percent, std::max(0.f, len->value) };
    return GridTrackBound{ Kind::Px, std::clamp(len->ToPx(emPx, remPx), 0.f, 1.0e6f) };
}

// A track size: a bound, or minmax(<min>, <max>) - whose min cannot be fr.
std::optional<GridTrackSpec> ParseGridTrack(const std::string& word, float emPx, float remPx) {
    if (word.rfind("minmax(", 0) == 0 && word.back() == ')') {
        const std::vector<std::string> args = SplitTopLevel(word.substr(7, word.size() - 8), ',');
        if (args.size() != 2) return std::nullopt;
        auto lo = ParseGridTrackBound(TrimLower(args[0]), emPx, remPx);
        auto hi = ParseGridTrackBound(TrimLower(args[1]), emPx, remPx);
        if (!lo || !hi || lo->kind == GridTrackBound::Kind::Fr) return std::nullopt;
        return GridTrackSpec{ *lo, *hi };
    }
    auto bound = ParseGridTrackBound(word, emPx, remPx);
    if (!bound) return std::nullopt;
    return GridTrackSpec{ *bound, *bound };
}

// grid-template-columns / -rows: none, or a track list with repeat(N, ...)
// and at most one repeat(auto-fill | auto-fit, ...). Line names ([a b]) are
// dropped. subgrid, masonry and anything that does not parse give nullopt:
// the declaration is then ignored, as an invalid one is in CSS.
std::optional<GridTemplate> ParseGridTemplate(const std::string& lower, float emPx, float remPx) {
    GridTemplate tpl;
    if (lower == "none") return tpl;
    std::string text;
    int brackets = 0;
    for (char c : lower) {
        if (c == '[') { ++brackets; text += ' '; continue; }
        if (c == ']') { if (brackets > 0) --brackets; text += ' '; continue; }
        if (brackets == 0) text += c;
    }
    for (const auto& part : SplitParts(text)) {
        if (part.rfind("repeat(", 0) == 0 && part.back() == ')') {
            const std::string inner = part.substr(7, part.size() - 8);
            const size_t comma = inner.find(',');
            if (comma == std::string::npos) return std::nullopt;
            const std::string count = TrimLower(inner.substr(0, comma));
            std::vector<GridTrackSpec> group;
            for (const auto& t : SplitParts(inner.substr(comma + 1))) {
                auto track = ParseGridTrack(t, emPx, remPx);
                if (!track) return std::nullopt;
                group.push_back(*track);
            }
            if (group.empty()) return std::nullopt;
            if (count == "auto-fill" || count == "auto-fit") {
                if (!tpl.autoRepeat.empty()) return std::nullopt;   // only one
                tpl.autoRepeat = std::move(group);
                tpl.autoRepeatAt = tpl.tracks.size();
                tpl.autoFit = (count == "auto-fit");
                continue;
            }
            auto n = ParseInteger(count);
            if (!n || *n < 1) return std::nullopt;
            for (int i = 0; i < *n; ++i) {
                for (const auto& track : group) {
                    if (static_cast<int>(tpl.tracks.size()) >= kMaxGridLines) break;
                    tpl.tracks.push_back(track);
                }
            }
            continue;
        }
        auto track = ParseGridTrack(part, emPx, remPx);
        if (!track) return std::nullopt;
        if (static_cast<int>(tpl.tracks.size()) < kMaxGridLines) tpl.tracks.push_back(*track);
    }
    if (tpl.Empty()) return std::nullopt;
    return tpl;
}

// grid-template-areas: one quoted string per row, each a list of area names
// ("." or a run of dots for no area). Rows of unequal length are invalid.
std::optional<std::vector<std::vector<std::string>>> ParseGridAreas(const std::string& value) {
    std::vector<std::vector<std::string>> rows;
    if (TrimLower(value) == "none") return rows;
    size_t i = 0;
    while (i < value.size()) {
        const char quote = value[i];
        if (quote != '"' && quote != '\'') {
            if (!std::isspace(static_cast<unsigned char>(quote))) return std::nullopt;
            ++i;
            continue;
        }
        const size_t close = value.find(quote, i + 1);
        if (close == std::string::npos) return std::nullopt;
        std::vector<std::string> row;
        for (const auto& cell : SplitParts(value.substr(i + 1, close - i - 1))) {
            const bool dots = std::all_of(cell.begin(), cell.end(), [](char c) { return c == '.'; });
            row.push_back(dots ? std::string() : cell);
        }
        if (row.empty()) return std::nullopt;
        if (!rows.empty() && row.size() != rows.front().size()) return std::nullopt;
        if (static_cast<int>(row.size()) > kMaxGridLines || static_cast<int>(rows.size()) >= kMaxGridLines)
            return std::nullopt;
        rows.push_back(std::move(row));
        i = close + 1;
    }
    if (rows.empty()) return std::nullopt;
    return rows;
}

// One end of grid-column / grid-row: auto, an integer line, span N (or span
// <name>, taken as span 1), a name, or "<integer> <name>" (the line number).
std::optional<GridLineSpec> ParseGridLine(const std::string& text) {
    using Kind = GridLineSpec::Kind;
    const std::vector<std::string> parts = SplitParts(TrimLower(text));
    if (parts.empty() || parts.size() > 3) return std::nullopt;
    GridLineSpec line;
    if (parts.size() == 1 && parts[0] == "auto") return line;
    bool span = false;
    std::optional<int> number;
    std::string name;
    for (const auto& part : parts) {
        if (part == "span") { if (span) return std::nullopt; span = true; continue; }
        if (auto n = ParseInteger(part)) {
            if (number || *n == 0) return std::nullopt;
            number = *n;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(part[0])) && part[0] != '_' && part[0] != '-')
            return std::nullopt;
        if (!name.empty()) return std::nullopt;
        name = part;
    }
    if (span) {
        if (number && *number < 0) return std::nullopt;
        line.kind = Kind::Span;
        line.value = std::clamp(number.value_or(1), 1, kMaxGridLines);
        return line;
    }
    if (number) {
        line.kind = Kind::Line;
        line.value = std::clamp(*number, -kMaxGridLines, kMaxGridLines);
        return line;
    }
    if (name.empty()) return std::nullopt;
    line.kind = Kind::Name;
    line.name = name;
    return line;
}

// grid-column / grid-row: "<start> [/ <end>]". A name alone names both ends
// (the area's start and end lines); anything else alone leaves the end auto.
bool ParseGridLinePair(const std::string& value, GridLineSpec& start, GridLineSpec& end) {
    const std::vector<std::string> halves = SplitTopLevel(value, '/');
    if (halves.size() > 2) return false;
    auto s = ParseGridLine(halves[0]);
    if (!s) return false;
    std::optional<GridLineSpec> e = GridLineSpec{};
    if (halves.size() == 2) e = ParseGridLine(halves[1]);
    else if (s->kind == GridLineSpec::Kind::Name) e = s;
    if (!e) return false;
    start = *s;
    end = *e;
    return true;
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
        // A flex or grid container is a block (or, inline-, an inline
        // block) that lays its children out as flex / grid items.
        const DisplayMode before = s.display;
        const BoxLayoutMode layoutBefore = s.layoutMode;
        s.layoutMode = BoxLayoutMode::Flow;
        if (lower == "none") s.display = DisplayMode::Hidden;
        else if (lower == "block") s.display = DisplayMode::Block;
        else if (lower == "flex") { s.display = DisplayMode::Block; s.layoutMode = BoxLayoutMode::Flex; }
        else if (lower == "grid") { s.display = DisplayMode::Block; s.layoutMode = BoxLayoutMode::Grid; }
        else if (lower == "inline") s.display = DisplayMode::Inline;
        else if (lower == "inline-block") s.display = DisplayMode::InlineBlock;
        else if (lower == "list-item") s.display = DisplayMode::ListItem;
        else if (lower == "table") s.display = DisplayMode::Table;
        else if (lower == "table-row") s.display = DisplayMode::TableRow;
        else if (lower == "table-cell") s.display = DisplayMode::TableCell;
        else if (lower == "inline-table") s.display = DisplayMode::InlineBlock;
        else if (lower == "inline-flex") { s.display = DisplayMode::InlineBlock; s.layoutMode = BoxLayoutMode::Flex; }
        else if (lower == "inline-grid") { s.display = DisplayMode::InlineBlock; s.layoutMode = BoxLayoutMode::Grid; }
        else { s.display = before; s.layoutMode = layoutBefore; }   // not a value: ignored
    }
    else if (prop == "flex-direction") {
        if (lower == "row") s.flexDirection = FlexDirectionMode::Row;
        else if (lower == "row-reverse") s.flexDirection = FlexDirectionMode::RowReverse;
        else if (lower == "column") s.flexDirection = FlexDirectionMode::Column;
        else if (lower == "column-reverse") s.flexDirection = FlexDirectionMode::ColumnReverse;
    }
    else if (prop == "flex-wrap") {
        if (lower == "nowrap") s.flexWrap = FlexWrapMode::NoWrap;
        else if (lower == "wrap") s.flexWrap = FlexWrapMode::Wrap;
        else if (lower == "wrap-reverse") s.flexWrap = FlexWrapMode::WrapReverse;
    }
    else if (prop == "flex-flow") {
        for (const auto& part : SplitParts(lower)) {
            if (part == "row") s.flexDirection = FlexDirectionMode::Row;
            else if (part == "row-reverse") s.flexDirection = FlexDirectionMode::RowReverse;
            else if (part == "column") s.flexDirection = FlexDirectionMode::Column;
            else if (part == "column-reverse") s.flexDirection = FlexDirectionMode::ColumnReverse;
            else if (part == "nowrap") s.flexWrap = FlexWrapMode::NoWrap;
            else if (part == "wrap") s.flexWrap = FlexWrapMode::Wrap;
            else if (part == "wrap-reverse") s.flexWrap = FlexWrapMode::WrapReverse;
        }
    }
    else if (prop == "flex") {
        // none = 0 0 auto, auto = 1 1 auto; otherwise up to two numbers
        // (grow, shrink) and a basis, which defaults to 0 when left out.
        if (lower == "none") { s.flexGrow = 0.f; s.flexShrink = 0.f; s.flexBasisPx.reset(); s.flexBasisPercent.reset(); return; }
        if (lower == "auto") { s.flexGrow = 1.f; s.flexShrink = 1.f; s.flexBasisPx.reset(); s.flexBasisPercent.reset(); return; }
        std::vector<float> numbers;
        bool haveBasis = false, basisAuto = false;
        std::optional<float> basisPx, basisPercent;
        for (const auto& part : SplitParts(lower)) {
            if (auto n = ParseNumber(part); n && numbers.size() < 2 && !haveBasis) {
                numbers.push_back(*n);
                continue;
            }
            if (haveBasis) return;                     // two bases: invalid
            haveBasis = true;
            if (part == "auto" || part == "content") { basisAuto = true; continue; }
            auto len = CssLength::Parse(part);
            if (!len || len->unit == CssUnit::Number || len->unit == CssUnit::Auto) return;
            if (len->unit == CssUnit::Percent) basisPercent = std::max(0.f, len->value);
            else basisPx = std::max(0.f, len->ToPx(em, rem));
        }
        if (numbers.empty() && !haveBasis) return;
        s.flexGrow = numbers.empty() ? 1.f : numbers[0];
        s.flexShrink = numbers.size() > 1 ? numbers[1] : 1.f;
        s.flexBasisPx.reset();
        s.flexBasisPercent.reset();
        if (!haveBasis) s.flexBasisPx = 0.f;           // flex: 1 is 1 1 0%
        else if (!basisAuto) { s.flexBasisPx = basisPx; s.flexBasisPercent = basisPercent; }
    }
    else if (prop == "flex-grow") {
        if (auto n = ParseNumber(lower)) s.flexGrow = *n;
    }
    else if (prop == "flex-shrink") {
        if (auto n = ParseNumber(lower)) s.flexShrink = *n;
    }
    else if (prop == "flex-basis") {
        if (lower == "auto" || lower == "content") { s.flexBasisPx.reset(); s.flexBasisPercent.reset(); return; }
        auto len = CssLength::Parse(lower);
        if (!len || len->unit == CssUnit::Auto || (len->unit == CssUnit::Number && len->value != 0.f)) return;
        s.flexBasisPx.reset();
        s.flexBasisPercent.reset();
        if (len->unit == CssUnit::Percent) s.flexBasisPercent = std::max(0.f, len->value);
        else s.flexBasisPx = std::max(0.f, len->ToPx(em, rem));
    }
    else if (prop == "order") {
        if (auto n = ParseInteger(lower)) s.order = std::clamp(*n, -kMaxGridLines, kMaxGridLines);
    }
    else if (prop == "justify-content") { if (auto a = ParseBoxAlign(lower)) s.justifyContent = *a; }
    else if (prop == "align-items")     { if (auto a = ParseBoxAlign(lower)) s.alignItems = *a; }
    else if (prop == "align-content")   { if (auto a = ParseBoxAlign(lower)) s.alignContent = *a; }
    else if (prop == "justify-items")   { if (auto a = ParseBoxAlign(lower)) s.justifyItems = *a; }
    else if (prop == "align-self")      { if (auto a = ParseBoxAlign(lower)) s.alignSelf = *a; }
    else if (prop == "justify-self")    { if (auto a = ParseBoxAlign(lower)) s.justifySelf = *a; }
    else if (prop == "place-items")   ParsePlace(lower, s.alignItems, s.justifyItems);
    else if (prop == "place-content") ParsePlace(lower, s.alignContent, s.justifyContent);
    else if (prop == "place-self")    ParsePlace(lower, s.alignSelf, s.justifySelf);
    else if (prop == "gap" || prop == "grid-gap") {
        const std::vector<std::string> parts = SplitParts(lower);
        if (parts.empty() || parts.size() > 2) return;
        auto row = ParseGap(parts[0], em, rem);
        auto column = ParseGap(parts.size() > 1 ? parts[1] : parts[0], em, rem);
        if (!row || !column) return;
        s.rowGapPx = *row;
        s.columnGapPx = *column;
    }
    else if (prop == "row-gap" || prop == "grid-row-gap") {
        if (auto g = ParseGap(lower, em, rem)) s.rowGapPx = *g;
    }
    else if (prop == "column-gap" || prop == "grid-column-gap") {
        if (auto g = ParseGap(lower, em, rem)) s.columnGapPx = *g;
    }
    else if (prop == "grid-template-columns") {
        if (auto t = ParseGridTemplate(lower, em, rem)) s.gridColumns = std::move(*t);
    }
    else if (prop == "grid-template-rows") {
        if (auto t = ParseGridTemplate(lower, em, rem)) s.gridRows = std::move(*t);
    }
    else if (prop == "grid-template-areas") {
        if (auto areas = ParseGridAreas(value)) s.gridAreas = std::move(*areas);
    }
    else if (prop == "grid-template" || prop == "grid") {
        // "<rows> / <columns>" (and none). The forms with area strings or
        // auto-flow are not read.
        if (lower == "none") { s.gridRows = {}; s.gridColumns = {}; s.gridAreas.clear(); return; }
        if (lower.find('"') != std::string::npos || lower.find('\'') != std::string::npos ||
            lower.find("auto-flow") != std::string::npos) return;
        const std::vector<std::string> halves = SplitTopLevel(lower, '/');
        if (halves.size() != 2) return;
        auto rows = ParseGridTemplate(TrimLower(halves[0]), em, rem);
        auto columns = ParseGridTemplate(TrimLower(halves[1]), em, rem);
        if (!rows || !columns) return;
        s.gridRows = std::move(*rows);
        s.gridColumns = std::move(*columns);
    }
    else if (prop == "grid-auto-flow") {
        if (ContainsWord(lower, "column")) s.gridAutoFlowColumn = true;
        else if (ContainsWord(lower, "row") || ContainsWord(lower, "dense")) s.gridAutoFlowColumn = false;
    }
    else if (prop == "grid-column") ParseGridLinePair(lower, s.gridColumnStart, s.gridColumnEnd);
    else if (prop == "grid-row")    ParseGridLinePair(lower, s.gridRowStart, s.gridRowEnd);
    else if (prop == "grid-column-start") { if (auto l = ParseGridLine(lower)) s.gridColumnStart = *l; }
    else if (prop == "grid-column-end")   { if (auto l = ParseGridLine(lower)) s.gridColumnEnd = *l; }
    else if (prop == "grid-row-start")    { if (auto l = ParseGridLine(lower)) s.gridRowStart = *l; }
    else if (prop == "grid-row-end")      { if (auto l = ParseGridLine(lower)) s.gridRowEnd = *l; }
    else if (prop == "grid-area") {
        // <row-start> / <column-start> / <row-end> / <column-end>; a name
        // alone is the area of that name, on both axes.
        const std::vector<std::string> parts = SplitTopLevel(lower, '/');
        if (parts.size() > 4) return;
        std::vector<GridLineSpec> lines;
        for (const auto& part : parts) {
            auto l = ParseGridLine(part);
            if (!l) return;
            lines.push_back(*l);
        }
        // A missing value repeats the one it pairs with when that is a
        // name, else it is auto.
        auto orFrom = [&](size_t i, const GridLineSpec& from) {
            if (i < lines.size()) return lines[i];
            return from.kind == GridLineSpec::Kind::Name ? from : GridLineSpec{};
        };
        const GridLineSpec rowStart = lines[0];
        const GridLineSpec columnStart = orFrom(1, rowStart);
        s.gridRowStart = rowStart;
        s.gridColumnStart = columnStart;
        s.gridRowEnd = orFrom(2, rowStart);
        s.gridColumnEnd = orFrom(3, columnStart);
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
        // Layers split on top-level commas; each may hold url(...), a
        // position, a size after '/', and (in the shorthand's last layer) a
        // colour. Size and position are kept per image layer.
        const std::vector<std::string> layers = SplitTopLevel(value, ',');
        std::vector<std::string> urls;
        std::vector<BackgroundSizeMode> sizes;
        std::vector<BackgroundPosition> positions;
        std::vector<BackgroundRepeat> repeats;
        for (const auto& rawLayer : layers) {
            const std::string layer = Trim(rawLayer);
            const std::string low = TrimLower(layer);
            const size_t u = low.find("url(");
            if (u == std::string::npos) continue;
            const size_t close = layer.find(')', u);
            std::string url = Trim(layer.substr(u + 4, close == std::string::npos
                                                           ? std::string::npos : close - u - 4));
            if (url.size() >= 2 && (url.front() == '\'' || url.front() == '"')) {
                url = url.substr(1, url.size() - 2);
            }
            if (url.empty()) continue;
            urls.push_back(url);
            if (prop != "background") continue;
            // The rest of the layer: "<position> [/ <size>] <repeat> ...".
            std::string rest = low.substr(0, u) + " " +
                               (close == std::string::npos ? std::string() : low.substr(close + 1));
            std::string sizePart;
            if (const size_t slash = rest.find('/'); slash != std::string::npos) {
                sizePart = rest.substr(slash + 1);
                rest = rest.substr(0, slash);
            }
            sizes.push_back(ParseBackgroundSize(sizePart));
            repeats.push_back(ParseBackgroundRepeat(rest + " " + sizePart).value_or(BackgroundRepeat{}));
            positions.push_back(ParseBackgroundPosition(rest, em, rem).value_or(BackgroundPosition{}));
        }
        s.backgroundImages = urls;   // the shorthand (and 'none') resets them
        if (prop == "background") {
            s.backgroundSizes = sizes;
            s.backgroundPositions = positions;
            s.backgroundRepeats = repeats;
        }
        if (prop == "background" && !opts.overrideAuthorColors) {
            // The colour sits in the last layer, anywhere among its words.
            for (const auto& part : SplitParts(TrimLower(layers.back()))) {
                if (part.rfind("url(", 0) == 0) continue;
                if (auto color = CssColor::Parse(part)) { s.backgroundColor = *color; break; }
            }
        }
    }
    else if (prop == "background-size") {
        s.backgroundSizes.clear();
        for (const auto& item : SplitTopLevel(lower, ','))
            s.backgroundSizes.push_back(ParseBackgroundSize(item));
    }
    else if (prop == "background-repeat") {
        s.backgroundRepeats.clear();
        for (const auto& item : SplitTopLevel(lower, ','))
            s.backgroundRepeats.push_back(ParseBackgroundRepeat(item).value_or(BackgroundRepeat{}));
    }
    else if (prop == "object-fit") {
        if (lower == "contain") s.objectFit = ObjectFitMode::Contain;
        else if (lower == "cover") s.objectFit = ObjectFitMode::Cover;
        else if (lower == "none") s.objectFit = ObjectFitMode::NoScaling;
        else if (lower == "scale-down") s.objectFit = ObjectFitMode::ScaleDown;
        else if (lower == "fill") s.objectFit = ObjectFitMode::Fill;
    }
    else if (prop == "object-position") {
        if (auto position = ParseBackgroundPosition(lower, em, rem)) s.objectPosition = *position;
    }
    else if (prop == "background-position") {
        s.backgroundPositions.clear();
        for (const auto& item : SplitTopLevel(lower, ',')) {
            if (auto position = ParseBackgroundPosition(item, em, rem))
                s.backgroundPositions.push_back(*position);
        }
    }
    else if (prop == "max-width" || prop == "min-width" ||
             prop == "max-height" || prop == "min-height") {
        std::optional<float>& limit = prop == "max-width"  ? s.maxWidthPx
                                    : prop == "min-width"  ? s.minWidthPx
                                    : prop == "max-height" ? s.maxHeightPx
                                                           : s.minHeightPx;
        std::optional<float>& percent = prop == "max-width"  ? s.maxWidthPercent
                                      : prop == "min-width"  ? s.minWidthPercent
                                      : prop == "max-height" ? s.maxHeightPercent
                                                             : s.minHeightPercent;
        // none (max) / auto (min): no limit. A percentage is resolved against
        // the container at layout.
        limit.reset();
        percent.reset();
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit == CssUnit::Percent) percent = std::max(0.f, len->value);
            else if (len->unit != CssUnit::Auto) limit = std::max(0.f, len->ToPx(em, rem));
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
        if (lower == "normal") {
            s.lineHeight = 1.4f;
            s.lineHeightSet = false;
            s.lineHeightPx.reset();
            return;
        }
        if (auto len = CssLength::Parse(lower)) {
            if (len->unit == CssUnit::Number) {
                s.lineHeight = len->value;
                s.lineHeightSet = true;
                s.lineHeightPx.reset();
            } else if (len->unit == CssUnit::Percent) {
                s.lineHeight = len->value / 100.f;
                s.lineHeightSet = true;
                s.lineHeightPx = em * len->value / 100.f;
            } else if (em > 0) {
                s.lineHeight = len->ToPx(em, rem) / em;
                s.lineHeightSet = true;
                s.lineHeightPx = len->ToPx(em, rem);
            }
        }
    }
    else if (prop == "letter-spacing") {
        if (lower == "normal") s.letterSpacingPx = 0.f;
        else if (auto len = CssLength::Parse(lower)) {
            if (len->unit != CssUnit::Percent && len->unit != CssUnit::Auto)
                s.letterSpacingPx = len->ToPx(em, rem);
        }
    }
    else if (prop == "white-space") {
        s.preserveWhitespace = (lower == "pre" || lower == "pre-wrap" ||
                                lower == "pre-line");
        s.noWrap = (lower == "nowrap");
    }
    else if (prop == "overflow" || prop == "overflow-x" || prop == "overflow-y") {
        s.overflowHidden = lower == "hidden" || lower == "clip" || lower == "scroll" ||
                           lower == "auto";
    }
    else if (prop == "clear") {
        if (lower == "left" || lower == "right" || lower == "both" ||
            lower == "inline-start" || lower == "inline-end") s.clear = true;
        else if (lower == "none") s.clear = false;
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
                if (len->unit != CssUnit::Percent) {
                    s.borderRadius = len->ToPx(em, rem);
                    s.borderRadiusPercent = 0;
                } else {
                    s.borderRadius = 0;
                    s.borderRadiusPercent = static_cast<float>(len->value);
                }
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
    else if (prop == "width" || prop == "height") {
        // A width / height replaces an earlier one, px or %; auto clears it.
        std::optional<float>& px  = prop == "width" ? s.widthPx : s.heightPx;
        std::optional<float>& pct = prop == "width" ? s.widthPercent : s.heightPercent;
        if (auto len = CssLength::Parse(lower)) {
            px.reset();
            pct.reset();
            if (len->unit == CssUnit::Percent) pct = len->value;
            else if (len->unit != CssUnit::Auto) px = len->ToPx(em, rem);
        }
    }
    else if (prop == "border" || prop == "border-top" || prop == "border-right" ||
             prop == "border-bottom" || prop == "border-left") {
        const BorderSide side = ParseBorderSide(lower, em, rem);
        if (prop == "border") s.SetAllBorders(side);
        else if (prop == "border-top") s.borderTop = side;
        else if (prop == "border-right") s.borderRight = side;
        else if (prop == "border-bottom") s.borderBottom = side;
        else s.borderLeft = side;
    }
    else if (prop == "border-width" || prop == "border-style" || prop == "border-color") {
        BorderSide* sides[4] = { &s.borderTop, &s.borderRight, &s.borderBottom, &s.borderLeft };
        if (prop == "border-width") {
            ForBoxSides<float>(lower, [&](const std::string& w) { return ParseBorderWidth(w, em, rem); },
                               [&](int i, float w) { sides[i]->width = w; });
        } else if (prop == "border-style") {
            ForBoxSides<BorderLineStyle>(lower, ParseBorderLineStyle,
                               [&](int i, BorderLineStyle st) { sides[i]->style = st; });
        } else {
            // currentColor parses as "black, current" so it still takes its side.
            ForBoxSides<std::pair<CssColor, bool>>(lower,
                [&](const std::string& c) -> std::optional<std::pair<CssColor, bool>> {
                    bool isCurrent = false;
                    if (auto color = ParseBorderColor(c, isCurrent)) return std::make_pair(*color, false);
                    if (isCurrent) return std::make_pair(CssColor{}, true);
                    return std::nullopt;
                },
                [&](int i, const std::pair<CssColor, bool>& c) {
                    sides[i]->color = c.first;
                    sides[i]->currentColor = c.second;
                });
        }
    }
    else if (prop.rfind("border-", 0) == 0 &&
             (prop.size() > 6 && (prop.find("-width") != std::string::npos ||
                                  prop.find("-style") != std::string::npos ||
                                  prop.find("-color") != std::string::npos))) {
        // border-top-width, border-left-color, ...
        BorderSide* side = nullptr;
        if (prop.rfind("border-top-", 0) == 0) side = &s.borderTop;
        else if (prop.rfind("border-right-", 0) == 0) side = &s.borderRight;
        else if (prop.rfind("border-bottom-", 0) == 0) side = &s.borderBottom;
        else if (prop.rfind("border-left-", 0) == 0) side = &s.borderLeft;
        if (side) {
            const std::string what = prop.substr(prop.rfind('-') + 1);
            if (what == "width") {
                if (auto w = ParseBorderWidth(lower, em, rem)) side->width = *w;
            } else if (what == "style") {
                if (auto st = ParseBorderLineStyle(lower)) side->style = *st;
            } else if (what == "color") {
                bool isCurrent = false;
                if (auto c = ParseBorderColor(lower, isCurrent)) {
                    side->color = *c;
                    side->currentColor = false;
                } else if (isCurrent) {
                    side->currentColor = true;
                }
            }
        }
    }
    // Unrecognized properties are ignored (vertical-align, float, etc.).
}

} // namespace HTML
} // namespace UltraCanvas
