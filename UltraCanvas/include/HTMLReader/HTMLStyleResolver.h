// include/HTMLReader/HTMLStyleResolver.h
// CSS cascade for the HTMLReader DOM: user-agent defaults per tag, author
// stylesheets (specificity + source order), then inline style="" attributes.
// Produces one ComputedStyle per element with inherited text properties and
// resolved-px box properties. Framework-independent.
// Version: 1.19.0 - NodeSelectorTraits::TagIs matches a foreign element's camelCase
//                  name (inline <svg>) against the lower-cased type selector
// Version: 1.18.0 - selector matching moved to CSSStyleSheet.h; NodeSelectorTraits
//                  is the DOM's view of it, Matches() the stand-alone test
// Version: 1.17.0 - merged with main's 1.4.0-1.6.0 (float / clear, box-sizing as
//                  borderBox)
// Version: 1.16.0 - letter-spacing
// Version: 1.15.0 - line-height kept as the author gave it (lineHeightSet / lineHeightPx);
//                  overflow: hidden
// Version: 1.14.0 - quirks mode: a table does not inherit text-align
// Version: 1.13.0 - height in percent
// Version: 1.12.0 - min-width, min-height, max-height in percent
// Version: 1.11.0 - max-width in percent
// Version: 1.10.0 - min-width, min-height, max-height
// Version: 1.9.0 - box-sizing
// Version: 1.8.0 - borders per side (width, style, colour): BorderSide
// Version: 1.7.0 - border-radius in percent; <img border>
// Version: 1.6.0 - object-fit / object-position
// Version: 1.5.0 - background-repeat (per layer)
// Version: 1.4.0 - background-position; background size and position per layer
// From main:
// Version: 1.6.0 - clear; <img align="left|right"> floats
// Version: 1.5.0 - float (floatMode)
// Version: 1.4.0 - box-sizing (borderBox)
// Version: 1.3.0 - background images, margin: auto, max-width, @media width
// Version: 1.2.0 - nowrap, border-collapse / border-spacing, border-radius
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/CSSStyleSheet.h"

#include <cctype>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace UltraCanvas {
namespace HTML {

// border-style, as drawn: double, groove, ridge, inset and outset draw
// solid. (NoBorder, not None: X11 defines None.)
enum class BorderLineStyle { NoBorder, Solid, Dashed, Dotted };

// One side of a box's border.
struct BorderSide {
    float width = 3.f;                                // CSS initial: medium
    BorderLineStyle style = BorderLineStyle::NoBorder;
    CssColor color{0, 0, 0, 255};
    // No colour of its own: the element's text colour (CSS's initial
    // currentColor), filled in once the element's colour is known.
    bool currentColor = true;
    // The width it takes and draws: none without a style.
    float Width() const { return style == BorderLineStyle::NoBorder ? 0.f : width; }
    bool SameAs(const BorderSide& o) const {
        return Width() == o.Width() && (Width() == 0.f ||
               (style == o.style && color.r == o.color.r && color.g == o.color.g &&
                color.b == o.color.b && color.a == o.color.a));
    }
};

enum class DisplayMode {
    Block,
    Inline,
    InlineBlock,
    ListItem,
    Table,
    TableRow,
    TableCell,
    Hidden       // display:none
};

enum class TextAlignMode { Left, Right, Center, Justify };

// float: left / right, and <table align="left|right"> (mail columns).
enum class FloatMode { NoFloat, Left, Right };   // not None: an X11 macro

// vertical-align, as far as an inline image uses it: where the image sits
// against the text of its line.
enum class VerticalAlignMode { Baseline, Middle, Top, Bottom };

// background-size, as far as a single background picture uses it.
enum class BackgroundSizeMode { Auto, Contain, Cover };

// background-position on one axis: a fraction of the free space (0 = left /
// top, 0.5 = centre, 1 = right / bottom - what a percentage is) or a px
// offset from the left / top edge, or from the right / bottom one.
struct BackgroundAxisPosition {
    float value   = 0.f;
    bool  pixels  = false;
    bool  fromEnd = false;
};
struct BackgroundPosition {
    BackgroundAxisPosition x, y;   // CSS initial value: 0% 0% (top left)
};
// object-fit, how an <img> fills the box its width / height give it. CSS's
// initial value is fill (stretch). (No enumerator named None: X11 defines it.)
enum class ObjectFitMode { Fill, Contain, Cover, NoScaling, ScaleDown };

// background-repeat on each axis. CSS's initial value repeats both ways;
// space and round are taken as repeat.
struct BackgroundRepeat {
    bool x = true, y = true;
};

enum class ListMarker {
    Disc, Circle, Square,
    Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman,
    NoMarker
};

struct ComputedStyle {
    DisplayMode display = DisplayMode::Inline;

    // ---- inherited text properties ----
    std::string fontFamily;          // empty = resolver default
    float fontSizePx = 16.f;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool monospace = false;
    bool preserveWhitespace = false; // white-space: pre
    bool noWrap = false;             // white-space: nowrap, <nobr>, <td nowrap>
    bool borderCollapse = false;     // border-collapse: collapse
    CssColor color{0, 0, 0, 255};
    TextAlignMode textAlign = TextAlignMode::Left;
    float lineHeight = 1.4f;         // multiplier
    // line-height as the author set it (inherited): a length or percentage is
    // a fixed px height (lineHeightPx, inherited as px, as CSS computes it);
    // a plain number a factor of each element's own font size (lineHeight).
    // Not set (or normal): the font's own line height.
    bool lineHeightSet = false;
    // letter-spacing in px (inherited, as CSS computes it); 0: normal.
    float letterSpacingPx = 0.f;
    std::optional<float> lineHeightPx;
    ListMarker listMarker = ListMarker::Disc;

    // ---- non-inherited box properties (resolved to px) ----
    float marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
    float paddingTop = 0, paddingRight = 0, paddingBottom = 0, paddingLeft = 0;
    std::optional<CssColor> backgroundColor;
    // background-image / the url() layers of the background shorthand, top
    // layer first; later ones are fallbacks (an animated GIF over its poster).
    std::vector<std::string> backgroundImages;
    // Per layer, as CSS lists them: layer i takes entry i, a shorter list
    // repeats (use the *At helpers).
    std::vector<BackgroundSizeMode> backgroundSizes;
    std::vector<BackgroundPosition> backgroundPositions;
    std::vector<BackgroundRepeat> backgroundRepeats;
    // object-fit / object-position, on an <img> (not inherited). The
    // position's initial value is 50% 50%.
    ObjectFitMode objectFit = ObjectFitMode::Fill;
    BackgroundPosition objectPosition{ { 0.5f, false, false }, { 0.5f, false, false } };
    BackgroundSizeMode BackgroundSizeAt(size_t layer) const {
        return backgroundSizes.empty() ? BackgroundSizeMode::Auto
                                       : backgroundSizes[layer % backgroundSizes.size()];
    }
    BackgroundRepeat BackgroundRepeatAt(size_t layer) const {
        return backgroundRepeats.empty() ? BackgroundRepeat{}
                                         : backgroundRepeats[layer % backgroundRepeats.size()];
    }
    BackgroundPosition BackgroundPositionAt(size_t layer) const {
        return backgroundPositions.empty() ? BackgroundPosition{}
                                           : backgroundPositions[layer % backgroundPositions.size()];
    }
    // margin-left / margin-right: auto (centring a box with a width).
    bool marginLeftAuto = false, marginRightAuto = false;
    std::optional<float> maxWidthPx;
    std::optional<float> maxWidthPercent;   // max-width: 50% (of the containing line)
    // min-width / min-height / max-height: in px, or in percent (of the
    // containing width / height; a height percentage limits nothing when the
    // container's height is not set, as in CSS).
    std::optional<float> minWidthPx;
    std::optional<float> minHeightPx;
    std::optional<float> maxHeightPx;
    std::optional<float> minWidthPercent;
    std::optional<float> minHeightPercent;
    std::optional<float> maxHeightPercent;
    // The four borders, each with its own width, style and colour (CSS
    // border, border-top, border-width, border-left-color, ...). A side draws
    // only with a style: Width() is 0 for border-style none, whatever its
    // width says.
    BorderSide borderTop, borderRight, borderBottom, borderLeft;
    bool HasBorder() const {
        return borderTop.Width() > 0.f || borderRight.Width() > 0.f ||
               borderBottom.Width() > 0.f || borderLeft.Width() > 0.f;
    }
    // All four sides alike (one SetBorders call draws them).
    bool UniformBorder() const {
        return borderTop.SameAs(borderRight) && borderTop.SameAs(borderBottom) &&
               borderTop.SameAs(borderLeft);
    }
    // The widest side - for consumers with one border only (a rich-text
    // paragraph, an inline image's frame).
    const BorderSide& WidestBorder() const {
        const BorderSide* w = &borderTop;
        for (const BorderSide* b : { &borderRight, &borderBottom, &borderLeft })
            if (b->Width() > w->Width()) w = b;
        return *w;
    }
    float BorderHorizontal() const { return borderLeft.Width() + borderRight.Width(); }
    float BorderVertical() const { return borderTop.Width() + borderBottom.Width(); }
    void SetAllBorders(const BorderSide& side) {
        borderTop = borderRight = borderBottom = borderLeft = side;
    }
    // box-sizing: a px width / height is the content box (CSS default) unless
    // border-box - then it includes padding and border. Tables and form
    // controls are border-box, as in browsers' own style sheets.
    bool borderBox = false;
    FloatMode floatMode = FloatMode::NoFloat;
    // clear: left / right / both (and <br clear>): starts below the floats.
    bool clear = false;
    float borderRadius = 0;
    // border-radius given in percent (of the box; 50% rounds a square to a
    // circle): kept apart, since only the builder knows the box's size. 0 when
    // the radius is a length.
    float borderRadiusPercent = 0;
    // border-spacing (CSS) or the cellspacing attribute, on a table.
    std::optional<float> borderSpacing;
    // overflow: hidden / clip / scroll / auto - the box clips its content.
    // CSS's visible (the initial value): content wider or taller than the box
    // is drawn past it.
    bool overflowHidden = false;
    std::optional<float> widthPx;
    std::optional<float> heightPx;
    std::optional<float> widthPercent;   // width given in % (builder maps to Dimension::Pct)
    // height given in %: of the container's set height (none: auto, as in CSS).
    std::optional<float> heightPercent;
    // Not inherited. On a table cell (or row: valign) Baseline means "not
    // set", and the cell centres its content, as a browser's UA sheet does.
    VerticalAlignMode verticalAlign = VerticalAlignMode::Baseline;

    // links
    bool isLink = false;
    std::string href;
};

struct ResolverOptions {
    float baseFontSizePx = 16.f;
    std::string baseFontFamily;              // empty = system default
    CssColor textColor{0, 0, 0, 255};
    CssColor linkColor{0, 0, 238, 255};
    // eBook reading modes recolor everything; when set, author 'color' and
    // 'background-color' declarations are ignored so night/sepia stay legible.
    bool overrideAuthorColors = false;
};

// The HTML DOM as the selector matcher sees it (CSSStyleSheet.h, "Matching"):
// HTML tag names are stored lower-case by the parser and compare directly;
// a foreign element's name (linearGradient inside an inline <svg>) keeps its
// case and compares case-insensitively with the parser's lower-cased type
// selector, as a browser does for SVG in HTML. Attribute names go through
// Node's own lookup, exact then case-insensitive. Class names and ids compare
// exactly. The matcher itself is shared with every other tree that is styled
// by CSS (the SVG reader).
struct NodeSelectorTraits {
    using Element = Node;
    static bool TagIs(const Node& n, const std::string& tag) {
        if (n.tag == tag) return true;
        if (n.tag.size() != tag.size()) return false;
        for (size_t i = 0; i < tag.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(n.tag[i])) != tag[i]) return false;
        }
        return true;
    }
    static bool IdIs(const Node& n, const std::string& id) { return n.GetId() == id; }
    static bool HasClass(const Node& n, const std::string& name) { return n.HasClass(name); }
    static bool GetAttribute(const Node& n, const std::string& name, std::string& value) {
        if (!n.HasAttribute(name)) return false;
        value = n.GetAttribute(name);
        return true;
    }
    static bool IsLink(const Node& n) { return n.tag == "a" && n.HasAttribute("href"); }
    static bool IsRoot(const Node& n) { return n.tag == "html"; }
    static bool IsEmpty(const Node& n) {
        for (const auto& child : n.children) {
            if (child->IsElement() || (child->type == NodeType::Text && !child->text.empty())) return false;
        }
        return true;
    }
    static bool SiblingPosition(const Node& n, bool ofType, int& index, int& count) {
        if (!n.parent) return false;
        index = 0;
        count = 0;
        for (const auto& sib : n.parent->children) {
            if (!sib->IsElement()) continue;
            if (ofType && sib->tag != n.tag) continue;
            ++count;
            if (sib.get() == &n) index = count;
        }
        return index > 0;
    }
    // Only an element can match a compound; the document node above <html>
    // ends the walk.
    static const Node* Parent(const Node& n) {
        return n.parent && n.parent->IsElement() ? n.parent : nullptr;
    }
};

// Whether `selector` matches `element` - the resolver's own test, on its own,
// for callers that select nodes rather than style them (a querySelector).
inline bool Matches(const Selector& selector, const Node& element) {
    return SelectorMatches<NodeSelectorTraits>(selector, element);
}

class StyleResolver {
public:
    void AddStyleSheet(const std::string& css) { sheet.ParseAppend(css); }
    // The viewport width @media queries are answered for; set it before
    // adding style sheets.
    void SetMediaWidth(float px) { sheet.SetMediaWidth(px); }
    void ClearStyleSheets() { sheet.Clear(); }

    // Compute styles for every element in the document. Call again after
    // adding stylesheets or changing options; previous results are discarded.
    void Resolve(Document& document, const ResolverOptions& options = {});

    // Valid after Resolve(); falls back to a default style for unknown nodes.
    const ComputedStyle& StyleOf(const Node* node) const;

private:
    StyleSheet sheet;
    std::unordered_map<const Node*, ComputedStyle> styles;
    ComputedStyle fallback;
    ResolverOptions opts;
    bool quirks = true;          // the document's Document::quirksMode

    void ResolveElement(Node& element, const ComputedStyle& parentStyle);
    void ApplyUserAgentDefaults(const std::string& tag, ComputedStyle& style);
    void ApplyAlignAttribute(const Node& element, ComputedStyle& style);
    void ApplyLegacyAttributes(const Node& element, ComputedStyle& style);
    void ApplyDeclaration(const Declaration& declaration, ComputedStyle& style,
                          const ComputedStyle& parentStyle);
};

} // namespace HTML
} // namespace UltraCanvas
