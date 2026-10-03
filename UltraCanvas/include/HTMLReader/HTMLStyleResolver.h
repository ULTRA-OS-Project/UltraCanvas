// include/HTMLReader/HTMLStyleResolver.h
// CSS cascade for the HTMLReader DOM: user-agent defaults per tag, author
// stylesheets (specificity + source order), then inline style="" attributes.
// Produces one ComputedStyle per element with inherited text properties and
// resolved-px box properties. Framework-independent.
// Version: 1.5.0 - float (floatMode)
// Version: 1.4.0 - box-sizing (borderBox)
// Version: 1.3.0 - background images, margin: auto, max-width, @media width
// Version: 1.2.0 - nowrap, border-collapse / border-spacing, border-radius
// Last Modified: 2026-10-03
// Author: UltraCanvas Framework
#pragma once

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/CSSStyleSheet.h"

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace UltraCanvas {
namespace HTML {

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
    ListMarker listMarker = ListMarker::Disc;

    // ---- non-inherited box properties (resolved to px) ----
    float marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
    float paddingTop = 0, paddingRight = 0, paddingBottom = 0, paddingLeft = 0;
    std::optional<CssColor> backgroundColor;
    // background-image / the url() layers of the background shorthand, top
    // layer first; later ones are fallbacks (an animated GIF over its poster).
    std::vector<std::string> backgroundImages;
    BackgroundSizeMode backgroundSize = BackgroundSizeMode::Auto;
    // margin-left / margin-right: auto (centring a box with a width).
    bool marginLeftAuto = false, marginRightAuto = false;
    std::optional<float> maxWidthPx;
    // box-sizing: a px width / height is the content box (CSS default) unless
    // border-box - then it includes padding and border. Tables and form
    // controls are border-box, as in browsers' own style sheets.
    bool borderBox = false;
    FloatMode floatMode = FloatMode::NoFloat;
    float borderWidth = 0;
    CssColor borderColor{0, 0, 0, 255};
    float borderRadius = 0;
    // border-spacing (CSS) or the cellspacing attribute, on a table.
    std::optional<float> borderSpacing;
    std::optional<float> widthPx;
    std::optional<float> heightPx;
    std::optional<float> widthPercent;   // width given in % (builder maps to Dimension::Pct)
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

    void ResolveElement(Node& element, const ComputedStyle& parentStyle);
    void ApplyUserAgentDefaults(const std::string& tag, ComputedStyle& style);
    void ApplyAlignAttribute(const Node& element, ComputedStyle& style);
    void ApplyLegacyAttributes(const Node& element, ComputedStyle& style);
    void ApplyDeclaration(const Declaration& declaration, ComputedStyle& style,
                          const ComputedStyle& parentStyle);
    static bool SelectorMatches(const Selector& selector, const Node& element);
    static bool CompoundMatches(const SimpleSelector& part, const Node& element);
};

} // namespace HTML
} // namespace UltraCanvas
