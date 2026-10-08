// core/HTMLReader/HTMLElementBuilder.cpp
// DOM + computed styles → native UltraCanvas element tree on CSSLayout.
// Version: 1.24.0 - display: flex / grid (and inline-flex / inline-grid) lay their
//                   children out on the CSSLayout flex and grid engines: elements
//                   are items of their own size with real margins, text between
//                   them anonymous items; flex / order / align-self, grid lines,
//                   spans and named areas; repeat(auto-fill / auto-fit) counted
//                   against the grid's estimated width
// Version: 1.23.0 - BuildOptions::linkTooltips: text links and linked pictures
// Version: 1.22.0 - merged with main's 1.5.0-1.6.0 (floats, clear, shrink-to-fit
//                   tables, list markers, stacking cells, content-box px sizes):
//                   size limits are passed as the whole box's, cells size their
//                   content as in browsers
// Version: 1.21.0 - letter-spacing (Pango letter_spacing spans)
// Version: 1.20.0 - line-height on text; HTML boxes draw overflowing content
//                   (overflow: visible) unless overflow: hidden; a cell keeps its
//                   last child's bottom margin
// Version: 1.19.0 - a table is placed by its container's alignment, not its own
//                   text-align; a cell's children with a width of their own keep it
//                   (placed by the cell's align) instead of being stretched
// Version: 1.18.0 - width / height in percent on blocks: the content's share, padding
//                   and border added as pixels (Dimension::PctPlus) - no switch to
//                   content-box sizing
// Version: 1.17.0 - min / max width and height in percent; under box-sizing:
//                   border-box a percentage limit loses the padding and border
//                   (Dimension::PctPlus)
// Version: 1.16.0 - <img>: min / max width and height with the picture's ratio (CSS
//                   2.1 10.4); max-width in percent on images and blocks
// Version: 1.15.0 - min-width, min-height, max-height (content-box; border-box
//                   with box-sizing: border-box)
// Version: 1.14.0 - width / height of a block are its content's (CSS content-box);
//                   box-sizing: border-box keeps them whole; tables, cells and
//                   images keep their own sizing
// Version: 1.13.0 - border-collapse: collapse draws a shared cell edge once (the
//                   wider border wins); <table border> rules join the resolution
// Version: 1.12.0 - vertical-align on images sharing a line: top, middle, bottom
// Version: 1.11.0 - the gap between images a space apart is a space of their font,
//                   measured (SpaceWidth)
// Version: 1.10.0 - borders per side (width, colour, dashed / dotted); <hr> is its
//                   border box, as in a browser
// Version: 1.9.0 - images in a block without text share a line, side by side (a
//                  space's gap where the HTML has whitespace between them),
//                  wrapping when it is full; display:block ones keep their own.
// Version: 1.8.0 - <img> border, background, padding and rounded corners, block and
//                  inline; width / height size the picture (content box), the
//                  frame goes around it; border-radius in percent.
// Version: 1.7.0 - object-fit / object-position on <img>, block and inline (CSS's
//                  default fill: a box of another shape stretches the picture).
// Version: 1.6.0 - background-repeat: the picture tiles as the layer says (CSS's
//                  default repeats both ways).
// Version: 1.5.0 - background-position: the picture sits where the layer says
//                  (CSS's default top-left, not always centred).
// From main:
// Version: 1.6.0 - floats: float:left/right and <table align="left|right">
//                  go to their edge and the content after them flows beside
//                  them (CSSLayout floats); clear; a table without a width
//                  is shrink-to-fit; a list marker starts the item's first
//                  block; vertical-align: top / bottom on an inline box
// Version: 1.5.0 - display:block cells of a row stack in one anonymous cell
//                  (mail-template columns on a narrow screen); align="center"
//                  / "right" on a container places its narrowed blocks too;
//                  a px width / height is the content box unless
//                  box-sizing: border-box (padding and border on top)
// Version: 1.4.0 - @media answered for BuildOptions::viewportWidth; background
//                  images (first url() layer that loads, fitted by
//                  background-size); rounded borderless boxes; max-width;
//                  margin: auto centres a narrowed block or table.
// Version: 1.3.0 - tables on the CSSLayout table engine (shared columns,
//                  colspan / rowspan, min/max-content widths, cellspacing,
//                  valign); inline-block boxes and inline tables sit on the
//                  line beside their text (mail buttons); an inline around a
//                  block is looked through; nowrap; borders keep their colour.
// Version: 1.2.0 - table cells honor explicit widths; translucent (rgba) text
//                  colors are flattened to opaque so body text is not invisible.
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"

#include <functional>

#include "UltraCanvasImageElement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {
namespace HTML {

namespace {

Color ToColor(const CssColor& c) { return Color(c.r, c.g, c.b, c.a); }

// Composite a (possibly translucent) color over white and return it opaque.
// Text with alpha < 255 otherwise renders transparent (invisible); real clients
// show e.g. rgba(0,0,0,0.87) as near-black over the white page background.
CssColor FlattenOverWhite(const CssColor& c) {
    if (c.a >= 255) return CssColor{c.r, c.g, c.b, 255};
    const float a = c.a / 255.f;
    auto over = [&](uint8_t ch) {
        return static_cast<uint8_t>(ch * a + 255.f * (1.f - a) + 0.5f);
    };
    return CssColor{over(c.r), over(c.g), over(c.b), 255};
}

std::string EscapeMarkup(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string ColorHex(const CssColor& c) {
    const CssColor o = FlattenOverWhite(c);   // Pango has no alpha; flatten it
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", o.r, o.g, o.b);
    return buffer;
}

// Pango <span size="..."> takes 1/1024ths of a point; px → pt at 96 dpi.
int PangoSize(float px) {
    return static_cast<int>(px * 72.f / 96.f * 1024.f + 0.5f);
}

// Pango <span letter_spacing="...">: in Pango units (1/1024 of a pixel on the
// render contexts here, which lay text out in device pixels).
int PangoLetterSpacing(float px) {
    return static_cast<int>(std::lround(px * 1024.f));
}

// Block containers use the engine's Block flow, which measures children at
// the container width so text wraps correctly. Block does not consume child
// margins yet (engine TODO), and column Flex freezes text heights before the
// width is known — so vertical margins are emitted as explicit spacer
// elements by BuildChildrenInto (with adjacent-margin collapsing), and
// horizontal margins fold into padding in ApplyBoxStyle.
void ConfigureBlockLayout(UltraCanvasContainer& container) {
    container.layout.SetDisplay(CSSLayout::DisplayType::Block);
}

bool IsBlockDisplay(DisplayMode d) {
    return d == DisplayMode::Block || d == DisplayMode::ListItem ||
           d == DisplayMode::Table || d == DisplayMode::TableRow ||
           d == DisplayMode::TableCell;
}

// Collapse whitespace runs in `text` to single spaces, consulting `rendered`
// (the plain text emitted so far) for the boundary so a space is also dropped
// right after an already-emitted space/newline.
std::string CollapseWhitespace(const std::string& text, const std::string& rendered) {
    std::string out;
    out.reserve(text.size());
    auto lastChar = [&]() {
        if (!out.empty()) return out.back();
        return rendered.empty() ? '\n' : rendered.back();
    };
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (lastChar() != ' ' && lastChar() != '\n') out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

// Trim leading/trailing ASCII whitespace (form values often carry stray
// indentation from the source markup).
std::string TrimAscii(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n\f\v");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n\f\v");
    return s.substr(b, e - b + 1);
}

// The visible text of a display-only form control. Inputs surface their value
// (or placeholder); textareas and buttons their content; a select its chosen
// option (falling back to the first). Password values are shown literally —
// these renders are read-only, not interactive fields.
std::string FormControlText(const Node& e) {
    if (e.tag == "input") {
        std::string v = e.GetAttribute("value");
        if (v.empty()) v = e.GetAttribute("placeholder");
        return TrimAscii(v);
    }
    if (e.tag == "textarea") {
        return e.TextContent();          // keep newlines for multi-line intent
    }
    if (e.tag == "button") {
        std::string t = TrimAscii(e.TextContent());
        if (t.empty()) t = TrimAscii(e.GetAttribute("value"));
        return t;
    }
    if (e.tag == "select") {
        Node* first = nullptr;
        for (const auto& c : e.children) {
            if (!c->IsElement("option")) continue;
            if (!first) first = c.get();
            if (c->HasAttribute("selected")) return TrimAscii(c->TextContent());
        }
        return first ? TrimAscii(first->TextContent()) : std::string();
    }
    return std::string();
}

ImageFitMode ToImageFit(ObjectFitMode fit) {
    switch (fit) {
        case ObjectFitMode::Contain:   return ImageFitMode::Contain;
        case ObjectFitMode::Cover:     return ImageFitMode::Cover;
        case ObjectFitMode::NoScaling: return ImageFitMode::NoScale;
        case ObjectFitMode::ScaleDown: return ImageFitMode::ScaleDown;
        case ObjectFitMode::Fill:      break;
    }
    return ImageFitMode::Fill;
}

ImagePosition ToImagePosition(const BackgroundPosition& p) {
    auto axis = [](const BackgroundAxisPosition& a) {
        return a.pixels ? ImageAxisPosition::Pixels(a.value, a.fromEnd)
                        : ImageAxisPosition::Fraction(a.value);
    };
    return ImagePosition{ axis(p.x), axis(p.y) };
}

// Whether a run holds nothing but whitespace text (spaces, newlines, &nbsp;)
// - between two images it is the space that separates them on their line.
bool OnlyWhitespace(const std::vector<Node*>& run) {
    for (const Node* n : run) {
        if (n->type != NodeType::Text) return false;
        const std::string& t = n->text;
        for (size_t i = 0; i < t.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (std::isspace(c)) continue;
            if (c == 0xC2 && i + 1 < t.size() && static_cast<unsigned char>(t[i + 1]) == 0xA0) {
                ++i;            // U+00A0, &nbsp;
                continue;
            }
            return false;
        }
    }
    return true;
}

// The label font for a style: its family (monospace when asked), size,
// weight and slant. FontStyle sizes are points; CSS sizes are px (96 dpi).
// Inline <span size> markup converts the same way (PangoSize), so a 15px
// button caption is no longer smaller than the 12px text around it.
FontStyle FontOf(const ComputedStyle& style) {
    FontStyle font;
    font.fontFamily = style.monospace && style.fontFamily.empty() ? "monospace" : style.fontFamily;
    font.fontSize = style.fontSizePx * 72.f / 96.f;
    font.fontWeight = style.bold ? FontWeight::Bold : FontWeight::Normal;
    font.fontSlant = style.italic ? FontSlant::Italic : FontSlant::Normal;
    return font;
}

// The dash a border side is stroked with: none for solid, dashes three
// widths long for dashed, width-long dots for dotted.
UCDashPattern BorderDash(const BorderSide& side) {
    const double w = std::max(1.f, side.Width());
    switch (side.style) {
        case BorderLineStyle::Dashed: return UCDashPattern({ 3.0 * w, 3.0 * w });
        case BorderLineStyle::Dotted: return UCDashPattern({ w, w });
        default:                      return UCDashPattern();
    }
}

// The display size of an image's picture: its width / height (one of them
// keeps the picture's shape), else the picture's own size.
// The size an image's picture is shown at: its width / height (one of them
// keeps the picture's shape), else the picture's own size - then held within
// min-width / max-width / min-height / max-height (px), keeping the shape
// where CSS does (CSS 2.1 10.4: a size not given follows the ratio; with
// neither given the table of constraint violations decides). `keepsRatio`
// says whether the result still has the picture's shape.
struct ImageUsedSize {
    Size2Df size;
    bool keepsRatio = true;
};
ImageUsedSize UsedImageSize(const ComputedStyle& style, const UCImage& raster) {
    const float nw = static_cast<float>(raster.GetWidth());
    const float nh = static_cast<float>(raster.GetHeight());
    const float minW = style.minWidthPx.value_or(0.f);
    const float maxW = std::max(minW, style.maxWidthPx.value_or(INFINITY));
    const float minH = style.minHeightPx.value_or(0.f);
    const float maxH = std::max(minH, style.maxHeightPx.value_or(INFINITY));
    auto clampW = [&](float v) { return std::clamp(v, minW, maxW); };
    auto clampH = [&](float v) { return std::clamp(v, minH, maxH); };
    ImageUsedSize out;
    if (nw <= 0.f || nh <= 0.f) {
        out.size = Size2Df(clampW(style.widthPx.value_or(nw)), clampH(style.heightPx.value_or(nh)));
        out.keepsRatio = false;
        return out;
    }
    if (style.widthPx && style.heightPx) {
        out.size = Size2Df(clampW(*style.widthPx), clampH(*style.heightPx));
        out.keepsRatio = false;                       // both given: no ratio
    } else if (style.widthPx) {
        const float w = clampW(*style.widthPx), h = w * nh / nw, ch = clampH(h);
        out.size = Size2Df(w, ch);
        out.keepsRatio = std::fabs(ch - h) < 0.01f;
    } else if (style.heightPx) {
        const float h = clampH(*style.heightPx), w = h * nw / nh, cw = clampW(w);
        out.size = Size2Df(cw, h);
        out.keepsRatio = std::fabs(cw - w) < 0.01f;
    } else {
        float w = nw, h = nh;
        const bool wHigh = w > maxW, wLow = w < minW, hHigh = h > maxH, hLow = h < minH;
        if (wHigh && hHigh) {
            if (maxW / w <= maxH / h) { h = std::max(minH, maxW * h / w); w = maxW; }
            else                      { w = std::max(minW, maxH * w / h); h = maxH; }
        } else if (wLow && hLow) {
            if (minW / w <= minH / h) { w = std::min(maxW, minH * w / h); h = minH; }
            else                      { h = std::min(maxH, minW * h / w); w = minW; }
        } else if (wLow && hHigh) { w = minW; h = maxH; out.keepsRatio = false; }
        else if (wHigh && hLow)   { w = maxW; h = minH; out.keepsRatio = false; }
        else if (wHigh) { h = std::max(maxW * h / w, minH); w = maxW; }
        else if (wLow)  { h = std::min(minW * h / w, maxH); w = minW; }
        else if (hHigh) { w = std::max(maxH * w / h, minW); h = maxH; }
        else if (hLow)  { w = std::min(minH * w / h, maxW); h = minH; }
        out.size = Size2Df(w, h);
        if (out.keepsRatio) out.keepsRatio = std::fabs(w * nh - h * nw) < 0.01f * nw * nh;
    }
    return out;
}

Size2Df ImageContentSize(const ComputedStyle& style, const UCImage& raster) {
    return UsedImageSize(style, raster).size;
}

// border-radius in px for a border box of `boxW` x `boxH`: a percentage of
// the box (the shorter side, so 50% of a square is a circle), capped at half
// the shorter side as CSS caps overlapping corners.
float BorderRadiusPx(const ComputedStyle& style, float boxW, float boxH) {
    const float shorter = std::max(0.f, std::min(boxW, boxH));
    const float r = style.borderRadiusPercent > 0.f ? shorter * style.borderRadiusPercent / 100.f
                                                     : style.borderRadius;
    return std::min(r, shorter / 2.f);
}

// The href of the nearest <a href> around `node` (not `node` itself): text
// and images inside a link's blocks - <a href><div>..</div></a> - are that
// link too.
std::string AncestorLink(const Node& node) {
    for (const Node* p = node.parent; p; p = p->parent) {
        if (p->IsElement("a") && p->HasAttribute("href")) return p->GetAttribute("href");
    }
    return std::string();
}

// ---- flex and grid: the resolver's values in the layout engine's terms ----

CSSLayout::JustifyContent ToJustifyContent(BoxAlignMode a) {
    using J = CSSLayout::JustifyContent;
    switch (a) {
        case BoxAlignMode::End:          return J::FlexEnd;
        case BoxAlignMode::Center:       return J::Center;
        case BoxAlignMode::SpaceBetween: return J::SpaceBetween;
        case BoxAlignMode::SpaceAround:  return J::SpaceAround;
        case BoxAlignMode::SpaceEvenly:  return J::SpaceEvenly;
        default:                         return J::FlexStart;   // normal, start, stretch
    }
}

// align-items: CSS's normal stretches; baseline sits at the start (the
// engine aligns no baselines).
CSSLayout::AlignItems ToAlignItems(BoxAlignMode a) {
    using A = CSSLayout::AlignItems;
    switch (a) {
        case BoxAlignMode::Start:    return A::Start;
        case BoxAlignMode::Baseline: return A::Start;
        case BoxAlignMode::End:      return A::End;
        case BoxAlignMode::Center:   return A::Center;
        default:                     return A::Stretch;
    }
}

CSSLayout::AlignContent ToAlignContent(BoxAlignMode a) {
    using A = CSSLayout::AlignContent;
    switch (a) {
        case BoxAlignMode::Start:        return A::Start;
        case BoxAlignMode::Baseline:     return A::Start;
        case BoxAlignMode::End:          return A::End;
        case BoxAlignMode::Center:       return A::Center;
        case BoxAlignMode::SpaceBetween: return A::SpaceBetween;
        case BoxAlignMode::SpaceAround:  return A::SpaceAround;
        case BoxAlignMode::SpaceEvenly:  return A::SpaceEvenly;
        default:                         return A::Stretch;
    }
}

CSSLayout::AlignSelf ToAlignSelf(BoxAlignMode a) {
    using A = CSSLayout::AlignSelf;
    switch (a) {
        case BoxAlignMode::Start:    return A::Start;
        case BoxAlignMode::Baseline: return A::Start;
        case BoxAlignMode::End:      return A::End;
        case BoxAlignMode::Center:   return A::Center;
        case BoxAlignMode::Stretch:  return A::Stretch;
        default:                     return A::Auto;
    }
}

CSSLayout::JustifyItems ToJustifyItems(BoxAlignMode a) {
    using J = CSSLayout::JustifyItems;
    switch (a) {
        case BoxAlignMode::Start:    return J::Start;
        case BoxAlignMode::Baseline: return J::Start;
        case BoxAlignMode::End:      return J::End;
        case BoxAlignMode::Center:   return J::Center;
        default:                     return J::Stretch;   // normal stretches a box
    }
}

CSSLayout::JustifySelf ToJustifySelf(BoxAlignMode a) {
    using J = CSSLayout::JustifySelf;
    switch (a) {
        case BoxAlignMode::Start:    return J::Start;
        case BoxAlignMode::Baseline: return J::Start;
        case BoxAlignMode::End:      return J::End;
        case BoxAlignMode::Center:   return J::Center;
        case BoxAlignMode::Stretch:  return J::Stretch;
        default:                     return J::Auto;
    }
}

CSSLayout::Dimension TrackBoundDimension(const GridTrackBound& b) {
    switch (b.kind) {
        case GridTrackBound::Kind::Px:      return CSSLayout::Dimension::Px(b.value);
        case GridTrackBound::Kind::Percent: return CSSLayout::Dimension::Pct(b.value);
        default:                            return CSSLayout::Dimension::Auto();
    }
}

// One track. minmax(<min>, <n>fr) is n fr: the engine resolves no fr inside
// minmax(), and its fr tracks have no content floor anyway - the minimum
// still decides how many auto-fill repetitions fit (ExpandTracks).
CSSLayout::GridTrackSize ToGridTrack(const GridTrackSpec& t) {
    using K = CSSLayout::GridTrackSizeKind;
    CSSLayout::GridTrackSize out;
    const GridTrackBound& b = t.max;
    if (!t.IsMinMax() || b.kind == GridTrackBound::Kind::Fr) {
        switch (b.kind) {
            case GridTrackBound::Kind::Px:
                out.kind = K::Fixed; out.value = CSSLayout::Dimension::Px(b.value); break;
            case GridTrackBound::Kind::Percent:
                out.kind = K::Percent; out.value = CSSLayout::Dimension::Pct(b.value); break;
            case GridTrackBound::Kind::Fr:
                out.kind = K::Fr; out.value = CSSLayout::Dimension::Fr(b.value); break;
            case GridTrackBound::Kind::MinContent: out.kind = K::MinContent; break;
            case GridTrackBound::Kind::MaxContent: out.kind = K::MaxContent; break;
            case GridTrackBound::Kind::FitContent:
                out.kind = K::FitContent; out.value = CSSLayout::Dimension::Px(b.value); break;
            case GridTrackBound::Kind::Auto: out.kind = K::Auto; break;
        }
        return out;
    }
    out.kind = K::MinMax;
    out.minValue = TrackBoundDimension(t.min);
    out.maxValue = TrackBoundDimension(t.max);
    return out;
}

// A track's size where it is definite before layout (px, or a percentage of
// `extent`): the max bound if it is, else the min; nullopt for neither.
std::optional<float> DefiniteTrackSize(const GridTrackSpec& t, float extent) {
    auto definite = [&](const GridTrackBound& b) -> std::optional<float> {
        if (b.kind == GridTrackBound::Kind::Px) return b.value;
        if (b.kind == GridTrackBound::Kind::Percent && extent > 0.f) return b.value * extent / 100.f;
        return std::nullopt;
    };
    if (auto v = definite(t.max)) return v;
    return definite(t.min);
}

// grid-template-columns / -rows as engine tracks, a repeat(auto-fill |
// auto-fit, ...) written out as many times as fit `extent` (0: unknown - one
// repetition). auto-fit keeps no more repetitions than there are items, so
// the items share the width as auto-fit's collapsed tracks let them.
std::vector<CSSLayout::GridTrackSize> ExpandTracks(const GridTemplate& tpl, float gap,
                                                    float extent, int itemCount) {
    std::vector<CSSLayout::GridTrackSize> out;
    int repeats = 0;
    if (!tpl.autoRepeat.empty()) {
        repeats = 1;
        float fixed = 0.f;
        for (const auto& t : tpl.tracks) fixed += DefiniteTrackSize(t, extent).value_or(0.f);
        float one = 0.f;
        bool definite = extent > 0.f;
        for (const auto& t : tpl.autoRepeat) {
            auto v = DefiniteTrackSize(t, extent);
            if (!v || *v <= 0.f) { definite = false; break; }
            one += *v;
        }
        if (definite) {
            const int perRepeat = static_cast<int>(tpl.autoRepeat.size());
            const int fixedCount = static_cast<int>(tpl.tracks.size());
            const int limit = std::max(1, (kMaxGridLines - fixedCount) / perRepeat);
            // The most repetitions whose tracks and gaps fit the extent.
            while (repeats < limit) {
                const int n = repeats + 1;
                const int tracks = fixedCount + n * perRepeat;
                if (fixed + n * one + gap * static_cast<float>(tracks - 1) > extent) break;
                repeats = n;
            }
            if (tpl.autoFit) repeats = std::min(repeats, std::max(1, itemCount));
        }
    }
    for (size_t i = 0; i <= tpl.tracks.size(); ++i) {
        if (i == tpl.autoRepeatAt) {
            for (int r = 0; r < repeats; ++r)
                for (const auto& t : tpl.autoRepeat) out.push_back(ToGridTrack(t));
        }
        if (i < tpl.tracks.size()) out.push_back(ToGridTrack(tpl.tracks[i]));
    }
    return out;
}

// A named area of grid-template-areas: its lines, 1-based (start inclusive,
// end exclusive: an area in column 2 alone runs from line 2 to line 3).
struct GridAreaLines { int rowStart, rowEnd, columnStart, columnEnd; };

std::unordered_map<std::string, GridAreaLines> GridAreaMap(
    const std::vector<std::vector<std::string>>& rows) {
    std::unordered_map<std::string, GridAreaLines> areas;
    for (size_t r = 0; r < rows.size(); ++r) {
        for (size_t c = 0; c < rows[r].size(); ++c) {
            const std::string& name = rows[r][c];
            if (name.empty()) continue;
            const int row = static_cast<int>(r) + 1, column = static_cast<int>(c) + 1;
            auto [it, fresh] = areas.try_emplace(name, GridAreaLines{ row, row + 1, column, column + 1 });
            if (fresh) continue;
            GridAreaLines& a = it->second;   // grow to the bounding box
            a.rowStart = std::min(a.rowStart, row);
            a.rowEnd = std::max(a.rowEnd, row + 1);
            a.columnStart = std::min(a.columnStart, column);
            a.columnEnd = std::max(a.columnEnd, column + 1);
        }
    }
    return areas;
}

// One end of grid-column / grid-row in the engine's terms. A negative line
// counts back from the end of the explicit grid (-1 is its last line); a
// name is that area's start or end line, or auto for an unknown name.
CSSLayout::GridLine ToGridLine(const GridLineSpec& spec, int explicitTracks,
                               const std::unordered_map<std::string, GridAreaLines>& areas,
                               bool isStart, bool isRow) {
    CSSLayout::GridLine line;
    switch (spec.kind) {
        case GridLineSpec::Kind::Auto:
            break;
        case GridLineSpec::Kind::Line: {
            int n = spec.value > 0 ? spec.value : explicitTracks + 2 + spec.value;
            line.type = CSSLayout::GridLineKind::Line;
            line.index = std::clamp(n, 1, kMaxGridLines + 1);
            break;
        }
        case GridLineSpec::Kind::Span:
            line.type = CSSLayout::GridLineKind::Span;
            line.index = std::clamp(spec.value, 1, kMaxGridLines);
            break;
        case GridLineSpec::Kind::Name: {
            auto it = areas.find(spec.name);
            if (it == areas.end()) break;
            const GridAreaLines& a = it->second;
            line.type = CSSLayout::GridLineKind::Line;
            line.index = isRow ? (isStart ? a.rowStart : a.rowEnd)
                               : (isStart ? a.columnStart : a.columnEnd);
            break;
        }
    }
    return line;
}

// The engine places "span N / <line>" from the start of the grid; CSS
// counts the span back from the end line - so make it two lines.
void FixBackwardSpan(CSSLayout::GridLine& start, CSSLayout::GridLine& end) {
    if (start.type == CSSLayout::GridLineKind::Span && end.type == CSSLayout::GridLineKind::Line) {
        const int first = std::max(1, end.index - start.index);
        start.type = CSSLayout::GridLineKind::Line;
        start.index = first;
        if (end.index <= first) end.index = first + 1;
    }
}

bool MarkupHasVisibleText(const std::string& markup) {
    bool inTag = false;
    for (char c : markup) {
        if (inTag) {
            if (c == '>') inTag = false;
            continue;
        }
        if (c == '<') { inTag = true; continue; }
        if (!std::isspace(static_cast<unsigned char>(c))) return true;
    }
    return false;
}

} // namespace

// ============================================================================
// ENTRY POINTS
// ============================================================================

BuildResult ElementBuilder::Build(const std::string& html, const BuildOptions& options) {
    Parser parser;
    Document doc = parser.Parse(html);
    return BuildDocument(doc, options);
}

BuildResult ElementBuilder::BuildDocument(Document& document, const BuildOptions& options) {
    opts = options;
    warnings.clear();
    anchors.clear();
    elementCount = 0;
    nextId = 0;

    resolver.ClearStyleSheets();
    resolver.SetMediaWidth(opts.viewportWidth > 0.f ? opts.viewportWidth : 800.f);
    for (const auto& href : document.styleSheetLinks) {
        if (opts.resourceLoader) {
            std::vector<uint8_t> bytes = opts.resourceLoader(href);
            if (!bytes.empty()) {
                resolver.AddStyleSheet(std::string(bytes.begin(), bytes.end()));
            } else {
                warnings.push_back("stylesheet not found: " + href);
            }
        }
    }
    for (const auto& css : document.styleSheets) {
        resolver.AddStyleSheet(css);
    }
    if (!opts.userCss.empty()) {
        resolver.AddStyleSheet(opts.userCss);
    }
    resolver.Resolve(document, opts.style);

    BuildResult result;
    result.title = document.title;

    Node* body = document.Body();
    if (!body) {
        warnings.push_back("document has no body");
        result.warnings = warnings;
        return result;
    }

    result.root = BuildBlock(*body);
    ++elementCount;   // the root itself
    result.warnings = warnings;
    result.elementCount = elementCount;
    result.anchors = std::move(anchors);
    return result;
}

std::string ElementBuilder::MakeId(const std::string& hint) {
    return "html_" + hint + "_" + std::to_string(nextId++);
}

void ElementBuilder::RegisterAnchors(const Node& node,
                                     const std::shared_ptr<UltraCanvasUIElement>& element,
                                     bool deep) {
    if (!element) return;
    if (node.type == NodeType::Element) {
        std::string id = node.GetAttribute("id");
        if (!id.empty()) anchors.emplace(id, element);
        if (node.tag == "a") {
            std::string name = node.GetAttribute("name");   // legacy anchors
            if (!name.empty()) anchors.emplace(name, element);
        }
    }
    if (!deep) return;
    for (const auto& child : node.children) {
        RegisterAnchors(*child, element, true);
    }
}

std::shared_ptr<UltraCanvasContainer> ElementBuilder::MakeContainer(const std::string& hint) {
    auto container = std::make_shared<UltraCanvasContainer>(MakeId(hint));
    // Scrolling belongs to the host view (e.g. the eBook viewer's content
    // pane); overflowing chapter content must not sprout scrollbars on every
    // nested block.
    ContainerStyle style = container->GetContainerStyle();
    style.autoShowScrollbars = false;
    // CSS's overflow: visible - content wider than its box (a 280px paragraph
    // in a 250px box) is drawn past it; overflow: hidden turns the clip on
    // (ApplyBoxStyle).
    style.clipChildren = false;
    container->SetContainerStyle(style);
    return container;
}

// ============================================================================
// BLOCK CONSTRUCTION
// ============================================================================

std::shared_ptr<UltraCanvasContainer> ElementBuilder::BuildBlock(Node& element) {
    auto container = MakeContainer(element.tag);
    RegisterAnchors(element, container);

    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*container, style);
    ConfigureBlockLayout(*container);
    ApplyBackgroundImage(*container, style);

    BuildContentInto(*container, element);
    return container;
}

void ElementBuilder::BuildContentInto(UltraCanvasContainer& parent, Node& element) {
    if (resolver.StyleOf(&element).layoutMode == BoxLayoutMode::Flow) {
        BuildChildrenInto(parent, element);
    } else {
        BuildItemsInto(parent, element);
    }
}

// ============================================================================
// FLEX AND GRID CONTAINERS
// ============================================================================

void ElementBuilder::BuildItemsInto(UltraCanvasContainer& parent, Node& element) {
    const ComputedStyle& cs = resolver.StyleOf(&element);
    const bool grid = cs.layoutMode == BoxLayoutMode::Grid;

    // The items: elements, and the runs of text between them. A run of
    // whitespace alone is no item.
    struct Item { Node* element = nullptr; std::vector<Node*> text; };
    std::vector<Item> items;
    std::vector<Node*> run;
    auto endRun = [&]() {
        if (!run.empty() && !OnlyWhitespace(run)) items.push_back({ nullptr, run });
        run.clear();
    };
    for (const auto& childPtr : element.children) {
        Node& child = *childPtr;
        if (child.type == NodeType::Text) { run.push_back(&child); continue; }
        if (!child.IsElement() || child.tag == "br") continue;
        if (resolver.StyleOf(&child).display == DisplayMode::Hidden) continue;
        endRun();
        items.push_back({ &child, {} });
    }
    endRun();

    // The container.
    std::unordered_map<std::string, GridAreaLines> areas;
    int explicitColumns = 0, explicitRows = 0;
    if (grid) {
        const int itemCount = static_cast<int>(items.size());
        std::vector<CSSLayout::GridTrackSize> columns =
            ExpandTracks(cs.gridColumns, cs.columnGapPx, EstimateContentWidth(element), itemCount);
        std::vector<CSSLayout::GridTrackSize> rows = ExpandTracks(cs.gridRows, cs.rowGapPx, 0.f, itemCount);
        // grid-template-areas sizes the explicit grid where the templates
        // leave it short: auto tracks, as in CSS.
        if (!cs.gridAreas.empty()) {
            areas = GridAreaMap(cs.gridAreas);
            while (columns.size() < cs.gridAreas.front().size()) columns.push_back({});
            while (rows.size() < cs.gridAreas.size()) rows.push_back({});
        }
        // No columns at all: one column the width of the grid. (CSS's
        // implicit auto column stretches to it; the engine's would be as
        // wide as its widest content.)
        if (columns.empty() && !cs.gridAutoFlowColumn) {
            CSSLayout::GridTrackSize whole;
            whole.kind = CSSLayout::GridTrackSizeKind::Fr;
            whole.value = CSSLayout::Dimension::Fr(1.f);
            columns.push_back(whole);
        }
        explicitColumns = static_cast<int>(columns.size());
        explicitRows = static_cast<int>(rows.size());
        parent.layout.SetGrid();
        parent.layout.SetGridColumns(std::move(columns));
        parent.layout.SetGridRows(std::move(rows));
        parent.layout.SetGridGap(cs.rowGapPx, cs.columnGapPx);
        parent.layout.SetGridAutoFlow(cs.gridAutoFlowColumn ? CSSLayout::GridAutoFlow::Column
                                                            : CSSLayout::GridAutoFlow::Row);
        parent.layout.SetGridJustifyItems(ToJustifyItems(cs.justifyItems));
        parent.layout.SetGridAlignItems(ToAlignItems(cs.alignItems));
    } else {
        CSSLayout::FlexDirection direction = CSSLayout::FlexDirection::Row;
        switch (cs.flexDirection) {
            case FlexDirectionMode::RowReverse:    direction = CSSLayout::FlexDirection::RowReverse; break;
            case FlexDirectionMode::Column:        direction = CSSLayout::FlexDirection::Column; break;
            case FlexDirectionMode::ColumnReverse: direction = CSSLayout::FlexDirection::ColumnReverse; break;
            case FlexDirectionMode::Row:           break;
        }
        CSSLayout::FlexWrap wrap = CSSLayout::FlexWrap::NoWrap;
        if (cs.flexWrap == FlexWrapMode::Wrap) wrap = CSSLayout::FlexWrap::Wrap;
        else if (cs.flexWrap == FlexWrapMode::WrapReverse) wrap = CSSLayout::FlexWrap::WrapReverse;
        parent.layout.SetFlex(direction, wrap)
                     .SetFlexJustifyContent(ToJustifyContent(cs.justifyContent))
                     .SetFlexAlignItems(ToAlignItems(cs.alignItems))
                     .SetFlexAlignContent(ToAlignContent(cs.alignContent))
                     .SetFlexGap(cs.rowGapPx, cs.columnGapPx);
    }
    const bool row = cs.flexDirection == FlexDirectionMode::Row ||
                     cs.flexDirection == FlexDirectionMode::RowReverse;

    for (Item& entry : items) {
        std::shared_ptr<UltraCanvasUIElement> item;
        if (entry.element) {
            item = BuildItem(*entry.element);
        } else {
            // An anonymous item: the run's text, formatted as the container's.
            auto label = BuildInlineRun(entry.text, cs, std::string(), &element);
            for (const Node* node : entry.text) {
                RegisterAnchors(*node, label ? std::static_pointer_cast<UltraCanvasUIElement>(label)
                                             : std::static_pointer_cast<UltraCanvasUIElement>(
                                                   parent.shared_from_this()), /*deep=*/true);
            }
            if (label) label->size.width = CSSLayout::Dimension::Auto();
            item = label;
        }
        if (!item) continue;

        static const ComputedStyle kAnonymous;
        const ComputedStyle& st = entry.element ? resolver.StyleOf(entry.element) : kAnonymous;
        if (grid) {
            CSSLayout::GridLine cStart = ToGridLine(st.gridColumnStart, explicitColumns, areas, true, false);
            CSSLayout::GridLine cEnd   = ToGridLine(st.gridColumnEnd,   explicitColumns, areas, false, false);
            CSSLayout::GridLine rStart = ToGridLine(st.gridRowStart,    explicitRows,    areas, true, true);
            CSSLayout::GridLine rEnd   = ToGridLine(st.gridRowEnd,      explicitRows,    areas, false, true);
            FixBackwardSpan(cStart, cEnd);
            FixBackwardSpan(rStart, rEnd);
            item->layoutItem.SetGridColumn(cStart, cEnd);
            item->layoutItem.SetGridRow(rStart, rEnd);
            item->layoutItem.SetJustifySelf(ToJustifySelf(st.justifySelf));
            item->layoutItem.SetGridAlignSelf(ToAlignSelf(st.alignSelf));
        } else {
            // flex-basis: a px basis is the content box's, like width, unless
            // box-sizing: border-box; a percentage is of the container.
            CSSLayout::Dimension basis = CSSLayout::Dimension::Auto();
            const float around = (!entry.element || st.borderBox) ? 0.f
                : row ? st.paddingLeft + st.paddingRight + st.BorderHorizontal()
                      : st.paddingTop + st.paddingBottom + st.BorderVertical();
            if (st.flexBasisPx) basis = CSSLayout::Dimension::Px(*st.flexBasisPx + around);
            else if (st.flexBasisPercent) basis = CSSLayout::Dimension::PctPlus(*st.flexBasisPercent, around);
            item->layoutItem.SetFlex(st.flexGrow, st.flexShrink, basis);
            item->layoutItem.SetFlexOrder(st.order);
            item->layoutItem.SetAlignSelf(ToAlignSelf(st.alignSelf));
        }
        parent.AddChild(item);
        ++elementCount;
    }
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildItem(Node& element) {
    const ComputedStyle& style = resolver.StyleOf(&element);
    // A picture is the image element itself, not the full-width line
    // BuildImage puts it on.
    auto unwrapImage = [&](Node& source) -> std::shared_ptr<UltraCanvasUIElement> {
        if (!opts.enableImages) return nullptr;
        auto row = std::dynamic_pointer_cast<UltraCanvasContainer>(BuildImage(source, AncestorLink(element)));
        if (!row || row->GetChildren().empty()) return nullptr;
        std::shared_ptr<UltraCanvasUIElement> image = row->GetChildren().front();
        row->RemoveChild(image);
        // In a flex or grid line its margins are its own (BuildImage left the
        // vertical ones to the flow's spacers).
        image->box.margin.top = CSSLayout::Dimension::Px(style.marginTop);
        image->box.margin.bottom = CSSLayout::Dimension::Px(style.marginBottom);
        RegisterAnchors(element, image, /*deep=*/true);
        return image;
    };
    if (element.tag == "img" || element.tag == "image") return unwrapImage(element);
    if (element.tag == "svg") {
        Node* raster = element.FindFirst("image");
        if (!raster) raster = element.FindFirst("img");
        if (!raster) {
            warnings.push_back("svg without raster <image> skipped");
            return nullptr;
        }
        return unwrapImage(*raster);
    }
    if (element.tag == "table" || style.display == DisplayMode::Table) {
        return BuildTable(element, /*inlineBox=*/true);
    }
    if (element.tag == "hr") {
        auto rule = BuildRule(element);
        if (rule) RegisterAnchors(element, rule);
        return rule;
    }
    if (element.tag == "input" || element.tag == "textarea" ||
        element.tag == "button" || element.tag == "select") {
        return BuildFormControl(element);
    }
    // Any other element is a box (a <span> or <a> item is blockified), as
    // wide as its content unless it has a width - the flex or grid line
    // sizes and stretches it - with real margins: they do not collapse here.
    auto box = MakeContainer(element.tag);
    RegisterAnchors(element, box);
    ApplyBoxStyle(*box, style, /*fillWidth=*/false, /*realMargins=*/true);
    ConfigureBlockLayout(*box);
    ApplyBackgroundImage(*box, style);
    BuildContentInto(*box, element);
    return box;
}

float ElementBuilder::EstimateContentWidth(const Node& element) const {
    std::vector<const Node*> chain;
    for (const Node* n = &element; n && n->IsElement(); n = n->parent) chain.push_back(n);
    float width = opts.viewportWidth > 0.f ? opts.viewportWidth : 800.f;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const ComputedStyle& st = resolver.StyleOf(*it);
        const float around = st.paddingLeft + st.paddingRight + st.BorderHorizontal();
        // The border box: its width (a content-box width gains the padding
        // and border), else what its margins leave of the line.
        float box = width - st.marginLeft - st.marginRight;
        if (st.widthPx) box = *st.widthPx + (st.borderBox ? 0.f : around);
        else if (st.widthPercent) box = width * *st.widthPercent / 100.f + (st.borderBox ? 0.f : around);
        if (st.maxWidthPx) box = std::min(box, *st.maxWidthPx + (st.borderBox ? 0.f : around));
        else if (st.maxWidthPercent) box = std::min(box, width * *st.maxWidthPercent / 100.f);
        width = std::max(0.f, box - around);
    }
    return width;
}

void ElementBuilder::BuildChildrenInto(UltraCanvasContainer& parent, Node& element,
                                       int listItemIndex) {
    const ComputedStyle& blockStyle = resolver.StyleOf(&element);

    // The current line's inline content. Text and inline elements gather in
    // `inlineRun` (one label); an inline box (an inline-block button, an
    // inline table) ends the run, and the runs and boxes of the line collect
    // in `lineParts` - a line with a box in it is laid out as a wrapping row.
    struct LinePart {
        std::vector<Node*> run;
        Node* box = nullptr;
    };
    std::vector<Node*> inlineRun;
    std::vector<LinePart> lineParts;
    // The list marker still to place: this item's own, or one its list item
    // handed down (see carriedMarker).
    std::string pendingMarker;
    if (element.tag == "li") pendingMarker = MarkerText(blockStyle.listMarker, listItemIndex);
    if (!carriedMarker.empty()) {
        pendingMarker = std::move(carriedMarker);
        carriedMarker.clear();
    }
    int listCounter = 0;

    // Vertical rhythm: the engine's Block flow stacks children edge-to-edge,
    // so CSS margins become explicit spacer elements. Adjacent top/bottom
    // margins collapse to the larger one, like CSS margin collapsing.
    float pendingMargin = 0.f;
    bool anyFlowChild = false;
    // The line the last image went on, while nothing but whitespace has come
    // after it: the next (inline) image joins it, side by side.
    std::shared_ptr<UltraCanvasContainer> imageLine;
    std::shared_ptr<UltraCanvasUIElement> lastImage;   // the line's last image
    float lastImageMarginRight = 0.f;
    // Images in a block that also has text flow in that text; in a block of
    // images alone each gets a line of its own (BuildImage).
    const bool flowImages = opts.enableImages && HasInlineText(element);
    // Where the flow goes: `parent`, or - from the first float on, until a
    // clear - one block holding the floats and what flows around them.
    UltraCanvasContainer* flow = &parent;
    auto addFlowChild = [&](std::shared_ptr<UltraCanvasUIElement> child,
                            float topMargin, float bottomMargin) {
        imageLine.reset();          // anything else in the flow ends the image line
        float spacing = anyFlowChild ? std::max(pendingMargin, topMargin)
                                     : topMargin;
        if (spacing > 0.5f) {
            auto spacer = MakeContainer("gap");
            spacer->size.height = CSSLayout::Dimension::Px(spacing);
            spacer->layoutItem.SetFlexShrink(0.f);
            flow->AddChild(spacer);
        }
        flow->AddChild(std::move(child));
        ++elementCount;
        pendingMargin = bottomMargin;
        anyFlowChild = true;
    };

    // Anchors inside a run map to its label; runs that produce no label
    // (e.g. an empty <a id="..."/> target) fall back to the enclosing
    // block so #fragment navigation still lands nearby.
    auto registerRun = [&](const std::vector<Node*>& run,
                           const std::shared_ptr<UltraCanvasUIElement>& label) {
        std::shared_ptr<UltraCanvasUIElement> anchorTarget = label;
        if (!anchorTarget) {
            anchorTarget = std::static_pointer_cast<UltraCanvasUIElement>(
                parent.shared_from_this());
        }
        for (const Node* node : run) {
            RegisterAnchors(*node, anchorTarget, /*deep=*/true);
        }
    };

    auto flushRun = [&]() {
        if (lineParts.empty()) {
            if (inlineRun.empty() && pendingMarker.empty()) return;
            std::string marker = std::move(pendingMarker);
            pendingMarker.clear();
            auto label = BuildInlineRun(inlineRun, blockStyle, marker, &element);
            registerRun(inlineRun, label);
            if (label) addFlowChild(label, 0.f, 0.f);
            inlineRun.clear();
            return;
        }

        // A line with boxes in it: a wrapping row, placed by text-align, its
        // items centred on each other (a button beside its caption).
        if (!inlineRun.empty()) {
            lineParts.push_back({ inlineRun, nullptr });
            inlineRun.clear();
        }
        auto line = MakeContainer("line");
        line->size.width = CSSLayout::Dimension::Pct(100.f);
        CSSLayout::JustifyContent justify = CSSLayout::JustifyContent::FlexStart;
        if (blockStyle.textAlign == TextAlignMode::Center)     justify = CSSLayout::JustifyContent::Center;
        else if (blockStyle.textAlign == TextAlignMode::Right) justify = CSSLayout::JustifyContent::FlexEnd;
        line->layout.SetFlex(CSSLayout::FlexDirection::Row, CSSLayout::FlexWrap::Wrap)
                    .SetFlexJustifyContent(justify)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        std::string marker = std::move(pendingMarker);
        pendingMarker.clear();
        auto addLabel = [&](const std::vector<Node*>& run) {
            auto label = BuildInlineRun(run, blockStyle, marker, &element);
            marker.clear();
            registerRun(run, label);
            if (!label) return;
            // Its own width in the row, wrapping only when the line is full.
            label->size.width = CSSLayout::Dimension::Auto();
            label->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
            line->AddChild(label);
            ++elementCount;
        };
        if (!marker.empty() && lineParts.front().box) addLabel({});
        for (auto& part : lineParts) {
            if (!part.box) {
                addLabel(part.run);
                continue;
            }
            if (auto box = BuildInlineBox(*part.box)) {
                // vertical-align: top / bottom on the box (side-by-side mail
                // columns are top); otherwise centred on its neighbours.
                switch (resolver.StyleOf(part.box).verticalAlign) {
                    case VerticalAlignMode::Top:
                        box->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start); break;
                    case VerticalAlignMode::Bottom:
                        box->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::End); break;
                    default: break;
                }
                line->AddChild(box);
                ++elementCount;
            }
        }
        lineParts.clear();
        if (!line->GetChildren().empty()) addFlowChild(line, 0.f, 0.f);
    };

    // An image in a block without text. Images that are inline (as <img> is
    // by default) share one wrapping line, side by side, until text or a block
    // comes between them; whitespace between two of them is a space's gap.
    // display:block puts an image on a line of its own.
    auto addImage = [&](Node& node, const std::string& href) {
        const ComputedStyle& st = resolver.StyleOf(&node);
        bool spaced = false;
        if (!inlineRun.empty()) {
            if (lineParts.empty() && OnlyWhitespace(inlineRun)) {
                spaced = true;
                inlineRun.clear();
            } else {
                imageLine.reset();
            }
        }
        if (!lineParts.empty()) imageLine.reset();
        flushRun();
        auto row = std::dynamic_pointer_cast<UltraCanvasContainer>(BuildImage(node, href));
        if (!row || row->GetChildren().empty()) return;
        const bool inlineImage = !IsBlockDisplay(st.display);
        std::shared_ptr<UltraCanvasUIElement> image = row->GetChildren().front();
        if (inlineImage) {
            // An inline box's vertical margins grow its line; they do not
            // collapse with the blocks around it.
            image->box.margin.top = CSSLayout::Dimension::Px(st.marginTop);
            image->box.margin.bottom = CSSLayout::Dimension::Px(st.marginBottom);
            // vertical-align against the line's other images: top, middle,
            // or standing on its bottom (baseline, bottom - the line has no
            // text, so its baseline is its bottom).
            switch (st.verticalAlign) {
                case VerticalAlignMode::Top:
                    image->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start); break;
                case VerticalAlignMode::Middle:
                    image->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center); break;
                case VerticalAlignMode::Bottom:
                case VerticalAlignMode::Baseline:
                    break;
            }
        }
        if (inlineImage && imageLine) {
            row->RemoveChild(image);
            // The space goes after the image before it: at the end of a full
            // line it is lost as a browser loses it, rather than indenting
            // the next line.
            if (spaced && lastImage) {
                const float space = SpaceWidth(st);
                lastImage->box.margin.right = CSSLayout::Dimension::Px(lastImageMarginRight + space);
            }
            RegisterAnchors(node, image);
            imageLine->AddChild(image);
            lastImage = image;
            lastImageMarginRight = st.marginRight;
            return;
        }
        RegisterAnchors(node, row);
        if (inlineImage) {
            addFlowChild(row, 0.f, 0.f);
            imageLine = row;
            lastImage = image;
            lastImageMarginRight = st.marginRight;
        } else {
            addFlowChild(row, st.marginTop, st.marginBottom);
        }
    };

    auto addBox = [&](Node& child) {
        if (!inlineRun.empty()) {
            lineParts.push_back({ inlineRun, nullptr });
            inlineRun.clear();
        }
        lineParts.push_back({ {}, &child });
    };

    // float: left / right, and <table align="left|right">: the float goes
    // to its edge of a block that holds it and everything after it up to a
    // clear, and that block's layout narrows the content beside the floats
    // (CSSLayout floats) - two 300px <table align="left"> columns of a mail
    // template sit side by side, a picture's caption runs beside it.
    auto isFloat = [&](Node& n) {
        if (!n.IsElement()) return false;
        const ComputedStyle& st = resolver.StyleOf(&n);
        if (st.floatMode == FloatMode::NoFloat || st.display == DisplayMode::Hidden) return false;
        return n.tag == "table" || n.tag == "img" || IsBlockDisplay(st.display) ||
               st.display == DisplayMode::InlineBlock;
    };
    bool afterFloat = false;   // whitespace right after a float renders nothing
    auto addFloat = [&](Node& n) {
        const ComputedStyle& st = resolver.StyleOf(&n);
        std::shared_ptr<UltraCanvasUIElement> item;
        if (n.tag == "table") {
            item = BuildTable(n);
        } else if (n.tag == "img") {
            if (!opts.enableImages) return;
            item = BuildImage(n, AncestorLink(n));
            if (item) item->size.width = CSSLayout::Dimension::Auto();
        } else {
            item = BuildBlock(n);
        }
        if (!item) return;
        if (flow == &parent) {
            auto area = MakeContainer("floats");
            ConfigureBlockLayout(*area);
            area->size.width = CSSLayout::Dimension::Pct(100.f);
            addFlowChild(area, 0.f, 0.f);
            flow = area.get();
        }
        RegisterAnchors(n, item);
        item->layoutItem.SetFloat(st.floatMode == FloatMode::Right ? CSSLayout::FloatSide::Right
                                                                   : CSSLayout::FloatSide::Left);
        // A float's margins do not collapse: they are its own.
        item->box.margin.top = CSSLayout::Dimension::Px(st.marginTop);
        item->box.margin.bottom = CSSLayout::Dimension::Px(st.marginBottom);
        flow->AddChild(item);
        ++elementCount;
    };

    std::function<void(Node&)> processChild = [&](Node& child) {
        if (child.type == NodeType::Comment) return;

        if (isFloat(child)) {
            flushRun();
            addFloat(child);
            afterFloat = true;
            return;
        }
        if (afterFloat && child.type == NodeType::Text &&
            std::all_of(child.text.begin(), child.text.end(),
                        [](unsigned char ch) { return std::isspace(ch) != 0; }))
            return;
        afterFloat = false;
        // clear (and <br clear="all">): what follows starts below the floats -
        // after the block that holds them.
        if (flow != &parent && child.IsElement() && resolver.StyleOf(&child).clear) {
            flushRun();
            flow = &parent;
            if (child.tag == "br") return;
        }

        if (child.type == NodeType::Text) {
            inlineRun.push_back(&child);
            return;
        }
        if (!child.IsElement()) return;

        const ComputedStyle& childStyle = resolver.StyleOf(&child);
        if (childStyle.display == DisplayMode::Hidden) return;

        if (child.tag == "img" || child.tag == "image") {
            if (flowImages) {
                inlineRun.push_back(&child);
                return;
            }
            if (opts.enableImages) addImage(child, AncestorLink(child));
            else flushRun();
            return;
        }
        if (child.tag == "svg") {
            // EPUB cover pages wrap the image in an <svg> viewport
            // (<svg><image xlink:href="..."/></svg>). Vector content is not
            // supported; render the first referenced raster image instead.
            flushRun();
            if (opts.enableImages) {
                Node* svgImage = child.FindFirst("image");
                if (!svgImage) svgImage = child.FindFirst("img");
                if (svgImage) {
                    if (auto image = BuildImage(*svgImage, AncestorLink(child))) {
                        RegisterAnchors(child, image, /*deep=*/true);
                        addFlowChild(image, childStyle.marginTop, childStyle.marginBottom);
                    }
                } else {
                    warnings.push_back("svg without raster <image> skipped");
                }
            }
            return;
        }
        if (child.tag == "hr") {
            flushRun();
            if (auto rule = BuildRule(child)) {
                RegisterAnchors(child, rule);
                addFlowChild(rule, childStyle.marginTop, childStyle.marginBottom);
            }
            return;
        }
        // A table is a table whatever its display says: display:inline (the
        // mail-button idiom) makes it an inline table, a box on the line.
        if (child.tag == "table" || childStyle.display == DisplayMode::Table) {
            if (childStyle.display == DisplayMode::Inline ||
                childStyle.display == DisplayMode::InlineBlock) {
                addBox(child);
                return;
            }
            flushRun();
            if (auto table = BuildTable(child)) {
                addFlowChild(table, childStyle.marginTop, childStyle.marginBottom);
            }
            return;
        }
        // Form controls route to BuildFormControl by tag (not display): a void
        // <input> would otherwise fall through to BuildBlock, which recurses
        // into children and emits nothing since the value lives in an attribute.
        if (child.tag == "input" || child.tag == "textarea" ||
            child.tag == "button" || child.tag == "select") {
            flushRun();
            if (auto control = BuildFormControl(child)) {
                addFlowChild(control, childStyle.marginTop, childStyle.marginBottom);
            }
            return;
        }

        if (childStyle.display == DisplayMode::ListItem) {
            flushRun();
            auto item = MakeContainer("li");
            RegisterAnchors(child, item);
            ApplyBoxStyle(*item, childStyle);
            ConfigureBlockLayout(*item);
            ApplyBackgroundImage(*item, childStyle);
            BuildChildrenInto(*item, child, ++listCounter);
            addFlowChild(item, childStyle.marginTop, childStyle.marginBottom);
            return;
        }

        if (IsBlockDisplay(childStyle.display)) {
            // Whitespace before a block renders nothing; dropping it keeps a
            // pending list marker for the block instead of a line of its own.
            if (!pendingMarker.empty() && lineParts.empty() &&
                std::all_of(inlineRun.begin(), inlineRun.end(), [](const Node* n) {
                    return n->type == NodeType::Text &&
                           std::all_of(n->text.begin(), n->text.end(),
                                       [](unsigned char ch) { return std::isspace(ch) != 0; });
                }))
                inlineRun.clear();
            if (!inlineRun.empty() || !lineParts.empty()) flushRun();
            // align="center" / "right" on the container (<td align>, <div
            // align>, <center>) places a narrowed block too, as browsers do -
            // the mail template's <td align="center"><div style="max-width:
            // 280px">: placed like margin: auto, unless the block has its own.
            ComputedStyle placement = childStyle;
            if (!placement.marginLeftAuto && !placement.marginRightAuto) {
                std::string align = element.tag == "center" ? std::string("center")
                                                            : element.GetAttribute("align");
                for (char& ch : align) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (align == "center" || align == "middle") {
                    placement.marginLeftAuto = placement.marginRightAuto = true;
                } else if (align == "right") {
                    placement.marginLeftAuto = true;
                }
            }
            // A list item whose text is in a block (<li><div>text</div>):
            // the marker starts that block's first line, not one of its own.
            carriedMarker = std::move(pendingMarker);
            pendingMarker.clear();
            auto block = BuildBlock(child);
            if (!carriedMarker.empty()) {   // nothing in the block took it
                pendingMarker = std::move(carriedMarker);
                carriedMarker.clear();
            }
            addFlowChild(PlaceByAutoMargins(block, placement),
                         childStyle.marginTop, childStyle.marginBottom);
            return;
        }

        // An inline-block with a box of its own - the mail "button" - is a
        // box on the line.
        if (NeedsInlineBox(child)) {
            addBox(child);
            return;
        }
        // An inline element around a block or a table (<a href><table>,
        // <font><div>): as in a browser, the block breaks the line and the
        // inline's formatting carries on inside it - so look through it.
        if (HasBlockDescendant(child)) {
            RegisterAnchors(child, std::static_pointer_cast<UltraCanvasUIElement>(
                                       parent.shared_from_this()));
            for (const auto& grand : child.children) processChild(*grand);
            return;
        }

        // An inline element holding an image (<a href><img></a>, the banner
        // and button of nearly every newsletter): a text run can only name
        // the image by its alt text, so lift the images out onto lines of
        // their own - clickable when inside a link - and keep the text
        // around them in runs.
        if (opts.enableImages && !flowImages &&
            (child.FindFirst("img") || child.FindFirst("image"))) {
            std::function<void(Node&, const std::string&)> lift =
                [&](Node& node, const std::string& href) {
                    for (const auto& grandPtr : node.children) {
                        Node& g = *grandPtr;
                        if (g.type == NodeType::Text) { inlineRun.push_back(&g); continue; }
                        if (!g.IsElement()) continue;
                        const ComputedStyle& gs = resolver.StyleOf(&g);
                        if (gs.display == DisplayMode::Hidden) continue;
                        std::string gHref = href;
                        if (g.tag == "a" && g.HasAttribute("href")) gHref = g.GetAttribute("href");
                        if (g.tag == "img" || g.tag == "image") {
                            addImage(g, gHref);
                        } else if (g.FindFirst("img") || g.FindFirst("image")) {
                            lift(g, gHref);
                        } else {
                            inlineRun.push_back(&g);
                        }
                    }
                };
            RegisterAnchors(child, std::static_pointer_cast<UltraCanvasUIElement>(
                                       parent.shared_from_this()));
            std::string href = AncestorLink(child);
            if (child.tag == "a" && child.HasAttribute("href")) href = child.GetAttribute("href");
            lift(child, href);
            return;
        }

        // Inline content joins the current run.
        inlineRun.push_back(&child);
    };

    for (const auto& childPtr : element.children) processChild(*childPtr);

    flushRun();
    // The last child's bottom margin stays inside a box it cannot collapse
    // through: a table cell, or a box with bottom padding or a bottom border.
    const bool keepsMargins = element.tag == "td" || element.tag == "th" ||
                              blockStyle.display == DisplayMode::TableCell ||
                              blockStyle.paddingBottom > 0.f || blockStyle.borderBottom.Width() > 0.f;
    if (keepsMargins && anyFlowChild && pendingMargin > 0.5f) {
        auto spacer = MakeContainer("gap");
        spacer->size.height = CSSLayout::Dimension::Px(pendingMargin);
        spacer->layoutItem.SetFlexShrink(0.f);
        parent.AddChild(spacer);
    }
}

// ============================================================================
// INLINE RUNS → LABEL WITH PANGO MARKUP
// ============================================================================

std::shared_ptr<UltraCanvasLabel> ElementBuilder::BuildInlineRun(
    const std::vector<Node*>& run, const ComputedStyle& blockStyle,
    const std::string& markerPrefix, const Node* blockNode) {

    std::string markup;
    runPlain.clear();
    runLinks.clear();
    runImages.clear();
    if (!markerPrefix.empty()) {
        markup += EscapeMarkup(markerPrefix);
        runPlain += markerPrefix;
    }

    // Whether every piece of text in the run is kept on one line (nowrap on
    // the block, <nobr> or white-space: nowrap around it).
    bool anyText = false, allNoWrap = true;
    std::function<void(const Node&)> scanWrap = [&](const Node& n) {
        if (n.type == NodeType::Text) {
            bool visible = false;
            for (unsigned char c : n.text) if (!std::isspace(c)) { visible = true; break; }
            if (!visible) return;
            anyText = true;
            const Node* p = n.parent;
            if (!(p ? resolver.StyleOf(p).noWrap : blockStyle.noWrap)) allNoWrap = false;
            return;
        }
        for (const auto& c : n.children) scanWrap(*c);
    };

    for (const Node* node : run) {
        scanWrap(*node);
        const int start = static_cast<int>(runPlain.size());
        // A text node whose parent is not the block is inside an inline the
        // builder looked through (it wraps a block): format it as that inline.
        const Node* p = node->parent;
        if (node->type == NodeType::Text && blockNode && p && p != blockNode && p->IsElement()) {
            std::string prefix, suffix;
            StyleWrap(resolver.StyleOf(p), blockStyle, prefix, suffix);
            markup += prefix;
            AppendInlineMarkup(*node, resolver.StyleOf(p),
                               resolver.StyleOf(p).preserveWhitespace, markup);
            markup += suffix;
        } else {
            AppendInlineMarkup(*node, blockStyle, blockStyle.preserveWhitespace, markup);
        }
        // Inside a link that is not part of the run (the run is the content
        // of a block or box inside <a href>): the whole piece is that link.
        const int end = static_cast<int>(runPlain.size());
        if (end > start) {
            std::string href = AncestorLink(*node);
            if (!href.empty()) runLinks.push_back({ start, end, href });
        }
    }
    const bool runNoWrap = blockStyle.noWrap || (anyText && allNoWrap);

    // Trim a leading collapse-space. Literal leading spaces in the markup are
    // rendered text, so runPlain starts with the same spaces — trim both and
    // shift the link ranges accordingly.
    size_t begin = markup.find_first_not_of(' ');
    if (begin == std::string::npos) return nullptr;
    if (begin > 0) {
        markup = markup.substr(begin);
        runPlain = runPlain.size() >= begin ? runPlain.substr(begin) : std::string();
        for (auto& link : runLinks) {
            link.startByte = std::max(0, link.startByte - static_cast<int>(begin));
            link.endByte -= static_cast<int>(begin);
        }
        for (auto& image : runImages) image.byteOffset -= static_cast<int>(begin);
    }
    while (!markup.empty() && markup.back() == ' ') {
        markup.pop_back();
        if (!runPlain.empty() && runPlain.back() == ' ') runPlain.pop_back();
    }
    for (auto& link : runLinks) {
        link.endByte = std::min(link.endByte, static_cast<int>(runPlain.size()));
    }
    runLinks.erase(std::remove_if(runLinks.begin(), runLinks.end(),
                                  [](const LabelTextLink& l) {
                                      return l.endByte <= l.startByte;
                                  }),
                   runLinks.end());

    if (!MarkupHasVisibleText(markup)) return nullptr;
    // The block's own letter-spacing: around the whole run (its inline
    // elements' own spacing, where it differs, is a span inside).
    if (std::fabs(blockStyle.letterSpacingPx) > 0.01f) {
        markup = "<span letter_spacing=\"" + std::to_string(PangoLetterSpacing(blockStyle.letterSpacingPx)) +
                 "\">" + markup + "</span>";
    }

    auto label = std::make_shared<UltraCanvasLabel>(MakeId("text"));
    ConfigureLabel(*label, blockStyle, runNoWrap);
    label->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
    label->size.width = CSSLayout::Dimension::Pct(100.f);
    label->SetTextIsMarkup(true);
    label->SetText(markup);

    // Attach the link byte ranges; the label hit-tests clicks against the
    // rendered text, so only the link actually under the pointer activates.
    if (!runLinks.empty() && opts.onLinkActivated) {
        label->SetTextLinks(runLinks);
        label->onLinkActivated = opts.onLinkActivated;
        label->onLinkHovered = opts.onLinkHovered;
        label->SetShowLinkTooltips(opts.linkTooltips);
    }
    if (!runImages.empty()) label->SetInlineImages(runImages);
    return label;
}

void ElementBuilder::StyleWrap(const ComputedStyle& style, const ComputedStyle& runStyle,
                               std::string& prefix, std::string& suffix,
                               const std::string& tag) {
    auto wrap = [&](const std::string& open, const std::string& close) {
        prefix += open;
        suffix = close + suffix;
    };

    if (style.bold && !runStyle.bold) wrap("<b>", "</b>");
    if (style.italic && !runStyle.italic) wrap("<i>", "</i>");
    if (style.underline && !runStyle.underline) wrap("<u>", "</u>");
    if (style.strikethrough && !runStyle.strikethrough) wrap("<s>", "</s>");
    if (style.monospace && !runStyle.monospace) wrap("<tt>", "</tt>");
    if (tag == "sub") wrap("<sub>", "</sub>");
    else if (tag == "sup") wrap("<sup>", "</sup>");
    else if (std::fabs(style.fontSizePx - runStyle.fontSizePx) > 0.5f) {
        wrap("<span size=\"" + std::to_string(PangoSize(style.fontSizePx)) + "\">",
             "</span>");
    }
    if (std::fabs(style.letterSpacingPx - runStyle.letterSpacingPx) > 0.01f) {
        wrap("<span letter_spacing=\"" + std::to_string(PangoLetterSpacing(style.letterSpacingPx)) + "\">",
             "</span>");
    }
    bool colorDiffers = style.color.r != runStyle.color.r ||
                        style.color.g != runStyle.color.g ||
                        style.color.b != runStyle.color.b;
    if (colorDiffers) {
        wrap("<span foreground=\"" + ColorHex(style.color) + "\">", "</span>");
    }
}

void ElementBuilder::AppendInlineMarkup(const Node& node, const ComputedStyle& runStyle,
                                        bool preserveWhitespace, std::string& out) {
    // runPlain mirrors the text the layout will render (markup stripped,
    // entities decoded); link byte ranges are recorded against it.
    if (node.type == NodeType::Text) {
        std::string plain = preserveWhitespace
            ? node.text : CollapseWhitespace(node.text, runPlain);
        out += EscapeMarkup(plain);
        runPlain += plain;
        return;
    }
    if (node.type != NodeType::Element) return;

    const ComputedStyle& style = resolver.StyleOf(&node);
    if (style.display == DisplayMode::Hidden) return;

    if (node.tag == "br") {
        out += '\n';
        runPlain += '\n';
        return;
    }
    if (node.tag == "img" || node.tag == "image") {
        // An image in running text: a U+FFFC placeholder the label reserves
        // the image's box on and draws it into (UltraCanvasLabel inline images).
        if (opts.enableImages && opts.resourceLoader) {
            std::string src = node.GetAttribute("src");
            if (src.empty()) src = node.GetAttribute("href");
            if (src.empty()) src = node.GetAttribute("xlink:href");
            std::vector<uint8_t> bytes = src.empty() ? std::vector<uint8_t>{}
                                                     : opts.resourceLoader(src);
            std::shared_ptr<UCImage> raster =
                bytes.empty() ? nullptr : UCImageRaster::LoadFromMemory(bytes);
            if (raster && raster->GetWidth() > 0 && raster->GetHeight() > 0) {
                const Size2Df size = ImageContentSize(style, *raster);
                const float w = size.width, h = size.height;
                if (w >= 1.f && h >= 1.f) {
                    LabelInlineImage image;
                    image.byteOffset = static_cast<int>(runPlain.size());
                    image.width = w;
                    image.height = h;
                    image.image = raster;
                    image.fit = ToImageFit(style.objectFit);
                    image.position = ToImagePosition(style.objectPosition);
                    // Its CSS box: margins, border, padding, background.
                    LabelInlineImageFrame& f = image.frame;
                    f.marginTop = style.marginTop;       f.marginRight = style.marginRight;
                    f.marginBottom = style.marginBottom; f.marginLeft = style.marginLeft;
                    f.paddingTop = style.paddingTop;       f.paddingRight = style.paddingRight;
                    f.paddingBottom = style.paddingBottom; f.paddingLeft = style.paddingLeft;
                    auto border = [&](const BorderSide& b) {
                        return LabelInlineImageBorder{ b.Width(), ToColor(b.color), BorderDash(b) };
                    };
                    f.borderTop = border(style.borderTop);
                    f.borderRight = border(style.borderRight);
                    f.borderBottom = border(style.borderBottom);
                    f.borderLeft = border(style.borderLeft);
                    if (style.backgroundColor) f.background = ToColor(*style.backgroundColor);
                    f.borderRadius = BorderRadiusPx(style,
                        w + f.paddingLeft + f.paddingRight + style.BorderHorizontal(),
                        h + f.paddingTop + f.paddingBottom + style.BorderVertical());
                    switch (style.verticalAlign) {
                        case VerticalAlignMode::Middle: image.align = LabelInlineImageAlign::Middle; break;
                        case VerticalAlignMode::Top:    image.align = LabelInlineImageAlign::Top;    break;
                        case VerticalAlignMode::Bottom: image.align = LabelInlineImageAlign::Bottom; break;
                        case VerticalAlignMode::Baseline: break;
                    }
                    runImages.push_back(std::move(image));
                    static const char kPlaceholder[] = "\xEF\xBF\xBC";   // U+FFFC
                    out += kPlaceholder;
                    runPlain += kPlaceholder;
                    return;
                }
            }
            if (!src.empty()) warnings.push_back("inline image not shown: " + src);
        }
        // No image: note the alt text so nothing silently disappears.
        std::string alt = node.GetAttribute("alt");
        if (!alt.empty()) {
            out += EscapeMarkup("[" + alt + "]");
            runPlain += "[" + alt + "]";
        }
        return;
    }

    // Span wrappers derived from the difference to the enclosing run style.
    std::string prefix, suffix;
    StyleWrap(style, runStyle, prefix, suffix, node.tag);

    out += prefix;
    const int linkStart = static_cast<int>(runPlain.size());
    bool childPre = preserveWhitespace || style.preserveWhitespace;
    for (const auto& child : node.children) {
        AppendInlineMarkup(*child, style, childPre, out);
    }
    out += suffix;

    if (style.isLink && !style.href.empty()) {
        const int linkEnd = static_cast<int>(runPlain.size());
        if (linkEnd > linkStart) {
            runLinks.push_back({linkStart, linkEnd, style.href});
        }
    }
}

// ============================================================================
// REPLACED / SPECIAL ELEMENTS
// ============================================================================

bool ElementBuilder::HasInlineText(const Node& element) const {
    for (const auto& childPtr : element.children) {
        const Node& child = *childPtr;
        if (child.type == NodeType::Text) {
            for (unsigned char c : child.text)
                if (!std::isspace(c)) return true;
            continue;
        }
        if (!child.IsElement()) continue;
        const ComputedStyle& style = resolver.StyleOf(&child);
        if (style.display == DisplayMode::Hidden) continue;
        // Blocks, tables and list items are lines of their own, not this
        // block's inline content; images and breaks carry no text.
        if (IsBlockDisplay(style.display) || child.tag == "img" || child.tag == "image" ||
            child.tag == "br")
            continue;
        if (HasInlineText(child)) return true;
    }
    return false;
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildImage(Node& element,
                                                                 const std::string& linkHref) {
    std::string src = element.GetAttribute("src");
    if (src.empty()) src = element.GetAttribute("href");        // SVG 2 <image>
    if (src.empty()) src = element.GetAttribute("xlink:href");  // SVG 1.1 <image>
    if (src.empty() || !opts.resourceLoader) {
        if (!src.empty()) warnings.push_back("no resource loader for image: " + src);
        return nullptr;
    }

    std::vector<uint8_t> bytes = opts.resourceLoader(src);
    if (bytes.empty()) {
        warnings.push_back("image not found: " + src);
        return nullptr;
    }

    auto raster = UCImageRaster::LoadFromMemory(bytes);
    if (!raster) {
        warnings.push_back("undecodable image: " + src);
        return nullptr;
    }

    auto image = std::make_shared<UltraCanvasImageElement>(MakeId("img"));
    image->LoadFromImage(raster);
    image->SetHeightFollowsWidth(true);   // <img width="800">: the height in proportion

    const ComputedStyle& style = resolver.StyleOf(&element);
    // object-fit / object-position: how the picture fills the box its width
    // and height give it (CSS's default stretches it), and where it sits.
    image->SetFitMode(ToImageFit(style.objectFit));
    image->SetImagePosition(ToImagePosition(style.objectPosition));
    // Border, background, padding and rounded corners go around the picture:
    // width / height size the picture itself (CSS's content-box), and the
    // horizontal margins stay margins - the background must not fill them.
    ComputedStyle boxStyle = style;
    const Size2Df content = ImageContentSize(style, *raster);
    boxStyle.borderRadius = BorderRadiusPx(style,
        content.width + style.paddingLeft + style.paddingRight + style.BorderHorizontal(),
        content.height + style.paddingTop + style.paddingBottom + style.BorderVertical());
    ApplyBoxStyle(*image, boxStyle, /*fillWidth=*/false, /*realMargins=*/false,
                  /*borderBoxSizes=*/true);
    image->box.boxSizing = CSSLayout::BoxSizing::ContentBox;
    image->box.padding.left = CSSLayout::Dimension::Px(style.paddingLeft);
    image->box.padding.right = CSSLayout::Dimension::Px(style.paddingRight);
    image->box.margin.left = CSSLayout::Dimension::Px(style.marginLeft);
    image->box.margin.right = CSSLayout::Dimension::Px(style.marginRight);
    if (style.heightPercent && !style.heightPx) image->size.height = CSSLayout::Dimension::Auto();
    // Without explicit dimensions the element reports the image's natural
    // size through MeasureOwnContent. Cap at the column width so oversized
    // images (covers, photos) shrink to fit instead of overflowing; a zero
    // minimum lets the row below shrink it (its min-content is its natural
    // width, which would otherwise pin it).
    CSSLayout::BoxConstraints constraints;
    constraints.maxWidth = CSSLayout::Dimension::Pct(
        std::min(100.f, style.maxWidthPercent.value_or(100.f)));   // max-width: 50%
    constraints.minWidth = style.minWidthPercent ? CSSLayout::Dimension::Pct(*style.minWidthPercent)
                                                 : CSSLayout::Dimension::Px(0.f);
    // Height percentages limit only where the container's height is set.
    if (style.maxHeightPercent) constraints.maxHeight = CSSLayout::Dimension::Pct(*style.maxHeightPercent);
    if (style.minHeightPercent) constraints.minHeight = CSSLayout::Dimension::Pct(*style.minHeightPercent);
    image->boxConstraints = constraints;
    // min / max width and height in px: the size they leave the picture,
    // shaped as CSS shapes it. A size that keeps the picture's shape gives
    // the width only - the height follows it, and a line narrower than it
    // still shrinks the picture in proportion (MeasureOwnContent); a size
    // that breaks the shape gives both.
    if (style.minWidthPx || style.maxWidthPx || style.minHeightPx || style.maxHeightPx) {
        const ImageUsedSize used = UsedImageSize(style, *raster);
        image->size.width = CSSLayout::Dimension::Px(used.size.width);
        if (used.keepsRatio)
            image->size.height = CSSLayout::Dimension::Auto();
        else
            image->size.height = CSSLayout::Dimension::Px(used.size.height);
    }
    image->layoutItem.SetFlexGrow(0).SetFlexShrink(1);
    if (!linkHref.empty() && opts.onLinkActivated) {
        image->SetClickable(true);
        image->onClick = [activate = opts.onLinkActivated, linkHref]() { activate(linkHref); };
        if (opts.onLinkHovered) {
            image->onHoverEnter = [hover = opts.onLinkHovered, linkHref]() { hover(linkHref); };
            image->onHoverLeave = [hover = opts.onLinkHovered]() { hover(std::string()); };
        }
        if (opts.linkTooltips) image->SetTooltip(linkHref);
    }

    // Block flow gives every child the full column width, and the image
    // element draws its bitmap centred in whatever box it gets - so a bare
    // image sat in the middle of the line. A full-width row around it keeps
    // the image at its own size and places it the way a browser places an
    // inline image: by the text-align it inherits (align="..." on it or its
    // container, <center>, CSS), at the start of the line by default.
    auto row = MakeContainer("imgline");
    row->size.width = CSSLayout::Dimension::Pct(100.f);
    CSSLayout::JustifyContent justify = CSSLayout::JustifyContent::FlexStart;
    if (style.textAlign == TextAlignMode::Center)     justify = CSSLayout::JustifyContent::Center;
    else if (style.textAlign == TextAlignMode::Right) justify = CSSLayout::JustifyContent::FlexEnd;
    // Wrapping, because the images after it may join this line; they stand
    // on its bottom, as images stand on a line's baseline.
    row->layout.SetFlex(CSSLayout::FlexDirection::Row, CSSLayout::FlexWrap::Wrap)
               .SetFlexJustifyContent(justify)
               .SetFlexAlignItems(CSSLayout::AlignItems::End);
    row->AddChild(image);
    return row;
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildRule(Node& element) {
    auto rule = MakeContainer("hr");
    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*rule, style);
    // A rule is its border box: an empty box (unless it has a height) inside
    // its borders - the user agent's inset 1px lines, or the author's
    // (border-top: 1px solid #eee), or a height with a background.
    const float height = style.heightPx.value_or(0.f) + style.BorderVertical();
    rule->size.height = CSSLayout::Dimension::Px(height);
    return rule;
}

std::shared_ptr<UltraCanvasContainer> ElementBuilder::BuildFormControl(Node& element) {
    // Hidden inputs render nothing.
    if (element.tag == "input" && element.GetAttribute("type") == "hidden") {
        return nullptr;
    }

    auto box = MakeContainer(element.tag);
    RegisterAnchors(element, box);

    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*box, style);          // background / padding / border / width:100%
    ConfigureBlockLayout(*box);

    // Show the control's value/label as a single wrapped line of literal text
    // inside the styled box — how a disabled field looks in a real browser.
    std::string value = FormControlText(element);

    auto label = std::make_shared<UltraCanvasLabel>(MakeId("field"));
    ConfigureLabel(*label, style);
    label->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
    label->size.width = CSSLayout::Dimension::Pct(100.f);
    label->SetTextIsMarkup(false);       // value is literal text, not Pango markup
    // A space keeps an empty control's box laid out (email spacing preserved).
    label->SetText(value.empty() ? std::string(" ") : value);
    box->AddChild(label);
    ++elementCount;

    return box;
}

bool ElementBuilder::HasBlockDescendant(const Node& element) const {
    for (const auto& childPtr : element.children) {
        const Node& child = *childPtr;
        if (!child.IsElement()) continue;
        const ComputedStyle& style = resolver.StyleOf(&child);
        if (style.display == DisplayMode::Hidden) continue;
        if (child.tag == "table" || IsBlockDisplay(style.display)) return true;
        if (style.display == DisplayMode::InlineBlock) continue;   // a box of its own
        if (HasBlockDescendant(child)) return true;
    }
    return false;
}

bool ElementBuilder::NeedsInlineBox(const Node& element) const {
    const ComputedStyle& style = resolver.StyleOf(&element);
    if (style.display != DisplayMode::InlineBlock) return false;
    if (element.tag == "img" || element.tag == "image" || element.tag == "svg") return false;
    if (style.layoutMode != BoxLayoutMode::Flow) return true;   // inline-flex / inline-grid
    if (HasBlockDescendant(element)) return true;
    return style.backgroundColor.has_value() || style.HasBorder() ||
           style.paddingTop > 0.f || style.paddingRight > 0.f ||
           style.paddingBottom > 0.f || style.paddingLeft > 0.f ||
           style.widthPx.has_value() || style.heightPx.has_value();
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildInlineBox(Node& element) {
    const ComputedStyle& style = resolver.StyleOf(&element);
    if (element.tag == "table" || style.display == DisplayMode::Table) {
        return BuildTable(element, /*inlineBox=*/true);
    }
    auto box = MakeContainer(element.tag);
    RegisterAnchors(element, box);
    ApplyBoxStyle(*box, style, /*fillWidth=*/false, /*realMargins=*/true);
    ConfigureBlockLayout(*box);
    ApplyBackgroundImage(*box, style);
    BuildContentInto(*box, element);
    // As wide as its content (shrink-to-fit), narrower only when the line is.
    box->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
    return box;
}

// border-collapse: collapse. Neighbouring cells share an edge, drawn once:
// the wider of the two borders, kept by the cell to the left of it or above
// it (the other cell drops its side); on a tie the left / upper cell's. Where
// the table draws a border of its own, the cells along that edge leave it to
// the table. Then every cell gets its settled sides.
template <typename Entry, typename Apply>
void CollapseBorders(std::vector<Entry>& cells, const ComputedStyle& table, Apply apply) {
    int rows = 0, cols = 0;
    for (const auto& e : cells) {
        rows = std::max(rows, e.row + e.rowSpan);
        cols = std::max(cols, e.col + e.colSpan);
    }
    std::vector<int> grid(static_cast<size_t>(rows) * cols, -1);
    for (size_t i = 0; i < cells.size(); ++i)
        for (int r = cells[i].row; r < cells[i].row + cells[i].rowSpan; ++r)
            for (int c = cells[i].col; c < cells[i].col + cells[i].colSpan; ++c)
                grid[static_cast<size_t>(r) * cols + c] = static_cast<int>(i);
    auto at = [&](int r, int c) {
        return (r < 0 || c < 0 || r >= rows || c >= cols) ? -1 : grid[static_cast<size_t>(r) * cols + c];
    };
    // `keep` (left / upper) and `drop` (right / lower) meet: the wider wins.
    auto settle = [](BorderSide& keep, BorderSide& drop) {
        if (drop.Width() > keep.Width()) keep = drop;
        drop = BorderSide{};
    };
    for (size_t i = 0; i < cells.size(); ++i) {
        Entry& e = cells[i];
        for (int r = e.row; r < e.row + e.rowSpan; ++r) {       // right edge
            const int j = at(r, e.col + e.colSpan);
            if (j >= 0 && j != static_cast<int>(i)) settle(e.borders.borderRight, cells[j].borders.borderLeft);
        }
        for (int c = e.col; c < e.col + e.colSpan; ++c) {       // bottom edge
            const int j = at(e.row + e.rowSpan, c);
            if (j >= 0 && j != static_cast<int>(i)) settle(e.borders.borderBottom, cells[j].borders.borderTop);
        }
    }
    for (Entry& e : cells) {
        if (table.borderTop.Width() > 0.f && e.row == 0) e.borders.borderTop = BorderSide{};
        if (table.borderLeft.Width() > 0.f && e.col == 0) e.borders.borderLeft = BorderSide{};
        if (table.borderBottom.Width() > 0.f && e.row + e.rowSpan == rows) e.borders.borderBottom = BorderSide{};
        if (table.borderRight.Width() > 0.f && e.col + e.colSpan == cols) e.borders.borderRight = BorderSide{};
        apply(e);
    }
}

std::shared_ptr<UltraCanvasContainer> ElementBuilder::BuildTable(Node& element, bool inlineBox) {
    const ComputedStyle& style = resolver.StyleOf(&element);
    auto table = MakeContainer("table");
    RegisterAnchors(element, table);
    ++elementCount;
    ApplyBoxStyle(*table, style, /*fillWidth=*/false, /*realMargins=*/inlineBox,
                  /*borderBoxSizes=*/true);
    // border-spacing: CSS, else cellspacing, else a browser's 2px.
    const float spacing = style.borderCollapse ? 0.f : style.borderSpacing.value_or(2.f);
    table->layout.SetTableSpacing(spacing, spacing);
    ApplyBackgroundImage(*table, style);
    // <table border="1"> rules every cell too.
    const bool ruledCells = element.tag == "table" && style.HasBorder() &&
                            element.HasAttribute("border");

    auto isRow = [&](const Node& n) {
        return n.tag == "tr" || resolver.StyleOf(&n).display == DisplayMode::TableRow;
    };
    auto isCell = [&](const Node& n) {
        return n.tag == "td" || n.tag == "th" ||
               resolver.StyleOf(&n).display == DisplayMode::TableCell;
    };

    // Rows in document order - through thead / tbody / tfoot. Anything else
    // at row level (a stray block) gets a row of its own, as in a browser's
    // anonymous row and cell.
    struct RowEntry { Node* row = nullptr; Node* lone = nullptr; };
    std::vector<RowEntry> rows;
    std::function<void(Node&)> collect = [&](Node& parentNode) {
        for (const auto& childPtr : parentNode.children) {
            Node& child = *childPtr;
            if (!child.IsElement()) continue;
            if (resolver.StyleOf(&child).display == DisplayMode::Hidden) continue;
            if (child.tag == "thead" || child.tag == "tbody" || child.tag == "tfoot") {
                collect(child);
            } else if (isRow(child)) {
                rows.push_back({ &child, nullptr });
            } else if (isCell(child)) {
                rows.push_back({ nullptr, &child });
            } else if (child.tag != "caption" && child.tag != "colgroup" && child.tag != "col") {
                rows.push_back({ nullptr, &child });
            }
        }
    };
    collect(element);

    // Slots taken by row-spanning cells of earlier rows.
    std::vector<std::vector<bool>> taken(rows.size());
    auto isTaken = [&](size_t r, int c) {
        return c < static_cast<int>(taken[r].size()) && taken[r][c];
    };
    auto take = [&](size_t r, int c) {
        if (static_cast<int>(taken[r].size()) <= c) taken[r].resize(c + 1, false);
        taken[r][c] = true;
    };
    auto spanAttr = [](const Node& n, const char* name) {
        std::string v = n.GetAttribute(name);
        int value = 1;
        if (!v.empty()) {
            try { value = std::stoi(v); } catch (...) { value = 1; }
        }
        return value;
    };

    // Each cell's place and the borders it is to draw. Separate borders are
    // drawn as built; collapsed ones once the whole grid is known.
    struct CellEntry {
        std::shared_ptr<UltraCanvasContainer> box;
        int row = 0, col = 0, rowSpan = 1, colSpan = 1;
        ComputedStyle borders;      // the cell's style, with its resolved sides
    };
    std::vector<CellEntry> cellEntries;

    auto addCell = [&](Node& cell, const ComputedStyle& rowStyle, size_t r, int& c) {
        while (isTaken(r, c)) ++c;
        const int colSpan = std::clamp(spanAttr(cell, "colspan"), 1, 1000);
        int rowSpan = spanAttr(cell, "rowspan");
        const int rowsLeft = static_cast<int>(rows.size() - r);
        rowSpan = rowSpan <= 0 ? rowsLeft : std::clamp(rowSpan, 1, rowsLeft);
        for (int dr = 0; dr < rowSpan; ++dr)
            for (int dc = 0; dc < colSpan; ++dc) take(r + dr, c + dc);

        const ComputedStyle& cellStyle = resolver.StyleOf(&cell);
        auto cellBox = MakeContainer(cell.tag.empty() ? std::string("td") : cell.tag);
        RegisterAnchors(cell, cellBox);
        ++elementCount;
        // The cell's borders: its own, else the 1px rule of <table border>.
        ComputedStyle boxStyle = cellStyle;
        if (ruledCells && !cellStyle.HasBorder()) {
            BorderSide rule;
            rule.width = 1.f;
            rule.style = BorderLineStyle::Solid;
            rule.color = CssColor{128, 128, 128, 255};
            rule.currentColor = false;
            boxStyle.SetAllBorders(rule);
        }
        CellEntry entry{ cellBox, static_cast<int>(r), c, rowSpan, colSpan, boxStyle };
        if (style.borderCollapse) {
            // Settled with the neighbours' once every cell is placed.
            boxStyle.SetAllBorders(BorderSide{});
        }
        // A px / % width is the column's width (the table layout reads it).
        // A cell's width / height size its content, as in browsers (a 25px cell
        // with 10px padding is 45px).
        ApplyBoxStyle(*cellBox, boxStyle, /*fillWidth=*/false);
        if (!cellStyle.backgroundColor && rowStyle.backgroundColor) {
            cellBox->SetBackgroundColor(ToColor(*rowStyle.backgroundColor));
        }
        cellEntries.push_back(std::move(entry));
        // valign / vertical-align of the cell, else of its row; a cell
        // centres its content by default, as in a browser.
        VerticalAlignMode va = cellStyle.verticalAlign;
        if (va == VerticalAlignMode::Baseline) va = rowStyle.verticalAlign;
        CSSLayout::JustifyContent justify = CSSLayout::JustifyContent::Center;
        if (va == VerticalAlignMode::Top)         justify = CSSLayout::JustifyContent::FlexStart;
        else if (va == VerticalAlignMode::Bottom) justify = CSSLayout::JustifyContent::FlexEnd;
        cellBox->layout.SetFlex(CSSLayout::FlexDirection::Column)
                       .SetFlexJustifyContent(justify)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        cellBox->layoutItem.SetGridRowColSimplified(static_cast<int>(r), c, rowSpan, colSpan);
        ApplyBackgroundImage(*cellBox, cellStyle);
        BuildChildrenInto(*cellBox, cell);
        // A cell lays its content out as a column that stretches every child
        // across it. A child with a width of its own (<div style="width:250px">,
        // <table width="420">) keeps that width, placed as the cell's align
        // attribute places blocks in a browser (-moz-center / -webkit-right).
        {
            std::string cellAlign = cell.GetAttribute("align");
            std::transform(cellAlign.begin(), cellAlign.end(), cellAlign.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            const CSSLayout::AlignSelf place =
                cellAlign == "center" || cellAlign == "middle" ? CSSLayout::AlignSelf::Center
                : cellAlign == "right" ? CSSLayout::AlignSelf::End : CSSLayout::AlignSelf::Start;
            for (const auto& child : cellBox->GetChildren()) {
                const CSSLayout::Dimension& w = child->size.width;
                const bool fullLine = w.unit == CSSLayout::DimensionUnit::Percent &&
                                      w.value >= 99.5f && w.offsetPx == 0.f;
                if (!w.isAuto() && !fullLine) child->layoutItem.SetAlignSelf(place);
            }
        }
        table->AddChild(cellBox);
        c += colSpan;
    };

    static const ComputedStyle kNoRowStyle{};
    for (size_t r = 0; r < rows.size(); ++r) {
        int c = 0;
        if (rows[r].lone) {
            addCell(*rows[r].lone, kNoRowStyle, r, c);
            continue;
        }
        Node& row = *rows[r].row;
        const ComputedStyle& rowStyle = resolver.StyleOf(&row);
        // A <td> made display:block is no longer a cell: as in a browser, the
        // blocks next to each other in a row share one anonymous cell and
        // stack in it. This is how mail templates turn their side-by-side
        // columns into one column on a narrow screen
        // (@media (max-width:620px) { .stack .column { display:block } }).
        std::vector<Node*> stacked;
        auto flushStacked = [&]() {
            if (stacked.empty()) return;
            while (isTaken(r, c)) ++c;
            take(r, c);
            auto anon = MakeContainer("td");
            ++elementCount;
            anon->layout.SetFlex(CSSLayout::FlexDirection::Column)
                        .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexStart)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            anon->layoutItem.SetGridRowColSimplified(static_cast<int>(r), c, 1, 1);
            if (rowStyle.backgroundColor) anon->SetBackgroundColor(ToColor(*rowStyle.backgroundColor));
            for (Node* block : stacked) {
                auto box = BuildBlock(*block);
                box->layoutItem.SetFlexShrink(0.f);
                anon->AddChild(box);
                ++elementCount;
            }
            table->AddChild(anon);
            ++c;
            stacked.clear();
        };
        for (const auto& cellPtr : row.children) {
            Node& cell = *cellPtr;
            if (!cell.IsElement() || !isCell(cell)) continue;
            const DisplayMode display = resolver.StyleOf(&cell).display;
            if (display == DisplayMode::Hidden) continue;
            if (display == DisplayMode::Block) {
                stacked.push_back(&cell);
                continue;
            }
            flushStacked();
            addCell(cell, rowStyle, r, c);
        }
        flushStacked();
    }

    if (style.borderCollapse && !cellEntries.empty()) {
        CollapseBorders(cellEntries, style,
                        [&](CellEntry& e) { ApplyBorders(*e.box, e.borders); });
    }

    // A floated table is placed by the float layout (BuildChildrenInto).
    if (style.floatMode != FloatMode::NoFloat) return table;
    const bool fullWidth = style.widthPercent && *style.widthPercent >= 99.5f;
    if (inlineBox || fullWidth) {
        if (inlineBox) table->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
        return table;
    }

    // A table narrower than its line: <table align>, else the alignment its
    // container asks for (<td align="right">, <center>) - as mail clients
    // render it.
    // The container's alignment, not the table's own text-align (that aligns
    // the table's text: <table style="text-align:left"> in a centring cell
    // is still centred).
    TextAlignMode align = element.parent && element.parent->IsElement()
                              ? resolver.StyleOf(element.parent).textAlign : style.textAlign;
    std::string alignAttr = element.GetAttribute("align");
    std::transform(alignAttr.begin(), alignAttr.end(), alignAttr.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (style.marginLeftAuto && style.marginRightAuto) align = TextAlignMode::Center;
    else if (style.marginLeftAuto) align = TextAlignMode::Right;
    if (alignAttr == "center" || alignAttr == "middle") align = TextAlignMode::Center;
    else if (alignAttr == "right") align = TextAlignMode::Right;
    else if (alignAttr == "left") align = TextAlignMode::Left;
    // A table without a width is shrink-to-fit (CSS 2.1 §17.5.2): as wide as
    // its content, not its line - the mail's 30px logo table, its button.
    const bool autoWidth = !style.widthPx && !style.widthPercent;
    if (align != TextAlignMode::Center && align != TextAlignMode::Right && !autoWidth) {
        // At the start of the line, at its own width: a cell (a flex column)
        // would otherwise stretch it across.
        table->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        return table;
    }

    auto line = MakeContainer("tableline");
    line->size.width = CSSLayout::Dimension::Pct(100.f);
    line->layout.SetFlexRow()
                .SetFlexJustifyContent(align == TextAlignMode::Center
                                           ? CSSLayout::JustifyContent::Center
                                           : align == TextAlignMode::Right
                                               ? CSSLayout::JustifyContent::FlexEnd
                                               : CSSLayout::JustifyContent::FlexStart)
                .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    table->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
    line->AddChild(table);
    return line;
}

// ============================================================================
// STYLE MAPPING
// ============================================================================

void ElementBuilder::ApplyBoxStyle(UltraCanvasUIElement& target,
                                   const ComputedStyle& style, bool fillWidth,
                                   bool realMargins, bool borderBoxSizes) {
    using CSSLayout::Dimension;

    // Vertical margins become sibling spacer elements (see BuildChildrenInto);
    // horizontal margins fold into padding so width:100% never overflows -
    // except on an inline box, whose background must stay inside its margin.
    target.box.boxSizing = CSSLayout::BoxSizing::BorderBox;

    const float foldRight = realMargins ? 0.f : style.marginRight;
    const float foldLeft  = realMargins ? 0.f : style.marginLeft;
    target.box.padding.top = Dimension::Px(style.paddingTop);
    target.box.padding.right = Dimension::Px(style.paddingRight + foldRight);
    target.box.padding.bottom = Dimension::Px(style.paddingBottom);
    target.box.padding.left = Dimension::Px(style.paddingLeft + foldLeft);
    if (realMargins) {
        target.box.margin.top = Dimension::Px(style.marginTop);
        target.box.margin.right = Dimension::Px(style.marginRight);
        target.box.margin.bottom = Dimension::Px(style.marginBottom);
        target.box.margin.left = Dimension::Px(style.marginLeft);
    }

    // width / height: the content's (CSS content-box), so the box is that
    // plus its padding (and the margins folded into it) and border - or the
    // whole box with box-sizing: border-box. The box stays border-box; a px
    // size grows by what goes around the content, a percentage carries it as
    // pixels on top (calc(50% + 24px), Dimension::PctPlus). A percentage
    // height resolves against the container's set height, else it is auto
    // (images size their picture themselves and drop it).
    const bool contentBox = !borderBoxSizes && !style.borderBox;
    const float aroundW = contentBox ? style.paddingLeft + style.paddingRight + foldLeft + foldRight +
                                       style.BorderHorizontal() : 0.f;
    const float aroundH = contentBox ? style.paddingTop + style.paddingBottom + style.BorderVertical()
                                     : 0.f;
    if (style.widthPx) {
        target.size.width = Dimension::Px(*style.widthPx + aroundW);
    } else if (style.widthPercent) {
        target.size.width = Dimension::PctPlus(*style.widthPercent, aroundW);
    } else if (fillWidth) {
        target.size.width = Dimension::Pct(100.f);
    }
    if (style.heightPx) {
        target.size.height = Dimension::Px(*style.heightPx + aroundH);
    } else if (style.heightPercent) {
        // Of the container's set height (a cell: the table's); with none it
        // is auto - so mail's height="100%" on tables in a body of auto
        // height changes nothing, as in a browser.
        target.size.height = Dimension::PctPlus(*style.heightPercent, aroundH);
    }

    if (style.backgroundColor) {
        target.SetBackgroundColor(ToColor(*style.backgroundColor));
    }
    // min / max width and height (px or %).
    if (style.maxWidthPx || style.maxWidthPercent || style.minWidthPx || style.minWidthPercent ||
        style.maxHeightPx || style.maxHeightPercent || style.minHeightPx || style.minHeightPercent) {
        CSSLayout::BoxConstraints limits = target.boxConstraints.value_or(CSSLayout::BoxConstraints{});
        // The engine limits a border-box element's whole box: a content-box
        // limit (CSS's default) gains the padding and border around the
        // content; under box-sizing: border-box it is the box's already.
        const bool contentLimits = !borderBoxSizes && !style.borderBox;
        const float addW = contentLimits ? style.paddingLeft + style.paddingRight + foldLeft + foldRight +
                                           style.BorderHorizontal() : 0.f;
        const float addH = contentLimits ? style.paddingTop + style.paddingBottom + style.BorderVertical()
                                         : 0.f;
        // A px limit, else a percentage of the container (calc(50% + 24px)).
        auto limit = [](const std::optional<float>& px, const std::optional<float>& pct,
                        float add, Dimension& out) {
            if (px) out = Dimension::Px(std::max(0.f, *px + add));
            else if (pct) out = Dimension::PctPlus(*pct, add);
        };
        limit(style.maxWidthPx,  style.maxWidthPercent,  addW, limits.maxWidth);
        limit(style.minWidthPx,  style.minWidthPercent,  addW, limits.minWidth);
        limit(style.maxHeightPx, style.maxHeightPercent, addH, limits.maxHeight);
        limit(style.minHeightPx, style.minHeightPercent, addH, limits.minHeight);
        target.boxConstraints = limits;
    }
    ApplyBorders(target, style);
    if (style.overflowHidden) {
        if (auto* box = dynamic_cast<UltraCanvasContainer*>(&target)) {
            ContainerStyle cs = box->GetContainerStyle();
            cs.clipChildren = true;
            box->SetContainerStyle(cs);
        }
    }
}

// The four border sides (each its own width, colour, dash) and the radius.
void ElementBuilder::ApplyBorders(UltraCanvasUIElement& target, const ComputedStyle& style) {
    if (style.HasBorder() && style.UniformBorder()) {
        // Width, colour and radius together: box.border alone reserves the
        // space but paints nothing.
        const BorderSide& b = style.borderTop;
        target.SetBorders(b.Width(), ToColor(b.color), style.borderRadius, BorderDash(b));
    } else if (style.HasBorder()) {
        // Each side its own (border-bottom: 1px solid #eee); the radius on
        // every corner, bordered or not.
        auto side = [&](const BorderSide& b, auto setter) {
            if (b.Width() > 0.f)
                (target.*setter)(b.Width(), ToColor(b.color), style.borderRadius, BorderDash(b));
        };
        side(style.borderTop, &UltraCanvasUIElement::SetBorderTop);
        side(style.borderRight, &UltraCanvasUIElement::SetBorderRight);
        side(style.borderBottom, &UltraCanvasUIElement::SetBorderBottom);
        side(style.borderLeft, &UltraCanvasUIElement::SetBorderLeft);
        if (style.borderRadius > 0) target.SetBorderRadius(style.borderRadius);
    } else if (style.borderRadius > 0) {
        target.SetBorderRadius(style.borderRadius);   // a rounded, borderless box
    }
}

void ElementBuilder::ApplyBackgroundImage(UltraCanvasContainer& box, const ComputedStyle& style) {
    if (style.backgroundImages.empty() || !opts.enableImages || !opts.resourceLoader) return;
    std::shared_ptr<UCImage> raster;
    size_t layer = 0;
    for (; layer < style.backgroundImages.size(); ++layer) {
        const std::string& url = style.backgroundImages[layer];
        std::vector<uint8_t> bytes = opts.resourceLoader(url);
        if (!bytes.empty()) raster = UCImageRaster::LoadFromMemory(bytes);
        if (raster && raster->GetWidth() > 0 && raster->GetHeight() > 0) break;
        raster.reset();
        warnings.push_back("background image not shown: " + url);
    }
    if (!raster) return;

    auto image = std::make_shared<UltraCanvasImageElement>(MakeId("bgimg"));
    image->LoadFromImage(raster);
    // The size and position of the layer that loaded.
    switch (style.BackgroundSizeAt(layer)) {
        case BackgroundSizeMode::Contain: image->SetFitMode(ImageFitMode::Contain); break;
        case BackgroundSizeMode::Cover:   image->SetFitMode(ImageFitMode::Cover);   break;
        case BackgroundSizeMode::Auto:    image->SetFitMode(ImageFitMode::NoScale); break;
    }
    image->SetImagePosition(ToImagePosition(style.BackgroundPositionAt(layer)));
    const BackgroundRepeat repeat = style.BackgroundRepeatAt(layer);
    image->SetImageRepeat(repeat.x, repeat.y);
    // Out of flow, filling the box: it neither sizes the box nor pushes its
    // content, and as the first child it is drawn underneath that content.
    CSSLayout::Position fill;
    fill.top = fill.right = fill.bottom = fill.left = CSSLayout::Dimension::Px(0.f);
    image->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    box.AddChild(image);
    ++elementCount;
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::PlaceByAutoMargins(
    std::shared_ptr<UltraCanvasUIElement> box, const ComputedStyle& style) {
    if (!box || !style.marginLeftAuto) return box;   // right-auto alone: start of line, as is
    const bool narrowed = style.widthPx || style.maxWidthPx ||
                          (style.widthPercent && *style.widthPercent < 99.5f) ||
                          (style.maxWidthPercent && *style.maxWidthPercent < 99.5f);
    if (!narrowed) return box;
    auto line = MakeContainer("autoline");
    line->size.width = CSSLayout::Dimension::Pct(100.f);
    line->layout.SetFlexRow()
                .SetFlexJustifyContent(style.marginRightAuto ? CSSLayout::JustifyContent::Center
                                                             : CSSLayout::JustifyContent::FlexEnd)
                .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    box->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
    line->AddChild(box);
    return line;
}

float ElementBuilder::SpaceWidth(const ComputedStyle& style) {
    const FontStyle font = FontOf(style);
    const std::string key = font.fontFamily + "|" + std::to_string(font.fontSize) + "|" +
                            (style.bold ? "b" : "") + (style.italic ? "i" : "");
    if (auto it = spaceWidths.find(key); it != spaceWidths.end()) return it->second;
    float width = style.fontSizePx * 0.28f;          // no context: a common face's space
    if (!measureContext) measureContext = CreateRenderContext(Size2Di(8, 8), nullptr);
    if (measureContext) {
        // "x x" less "xx": the space between two glyphs, as a line sets it.
        auto measure = [&](const std::string& text) -> double {
            auto layout = measureContext->CreateTextLayout(text, false);
            if (!layout) return -1.0;
            layout->SetFontStyle(font);
            return layout->GetLayoutWidth();
        };
        const double spaced = measure("x x"), tight = measure("xx");
        if (spaced > 0.0 && tight > 0.0 && spaced > tight)
            width = static_cast<float>(spaced - tight);
    }
    spaceWidths[key] = width;
    return width;
}

void ElementBuilder::ConfigureLabel(UltraCanvasLabel& label, const ComputedStyle& style,
                                    bool noWrap) {
    LabelStyle labelStyle;
    labelStyle.fontStyle = FontOf(style);
    // line-height as set: px, or a factor of this text's font size.
    if (style.lineHeightSet) {
        labelStyle.lineHeightPx = style.lineHeightPx ? *style.lineHeightPx
                                                     : style.lineHeight * style.fontSizePx;
        // line-height: 0 (spacer cells) is next to nothing, not "the font's".
        labelStyle.lineHeightPx = std::max(labelStyle.lineHeightPx, 0.01f);
    }
    labelStyle.textColor = ToColor(FlattenOverWhite(style.color));
    labelStyle.wrap = noWrap ? TextWrap::WrapNone
                    : style.preserveWhitespace ? TextWrap::WrapWordChar : TextWrap::WrapWord;

    switch (style.textAlign) {
        case TextAlignMode::Left: labelStyle.horizontalAlign = TextAlignment::Left; break;
        case TextAlignMode::Right: labelStyle.horizontalAlign = TextAlignment::Right; break;
        case TextAlignMode::Center: labelStyle.horizontalAlign = TextAlignment::Center; break;
        case TextAlignMode::Justify: labelStyle.horizontalAlign = TextAlignment::Justify; break;
    }

    label.SetStyle(labelStyle);
    // TODO: line-height once UltraCanvasLabel exposes it.
}

std::string ElementBuilder::MarkerText(ListMarker marker, int index) {
    switch (marker) {
        case ListMarker::NoMarker: return "";
        case ListMarker::Disc: return "\xE2\x80\xA2 ";      // •
        case ListMarker::Circle: return "\xE2\x97\xA6 ";    // ◦
        case ListMarker::Square: return "\xE2\x96\xAA ";    // ▪
        case ListMarker::Decimal: return std::to_string(index) + ". ";
        case ListMarker::LowerAlpha:
        case ListMarker::UpperAlpha: {
            std::string result;
            int n = index;
            while (n > 0) {
                --n;
                result.insert(result.begin(),
                              static_cast<char>((marker == ListMarker::LowerAlpha ? 'a' : 'A') +
                                                (n % 26)));
                n /= 26;
            }
            return result + ". ";
        }
        case ListMarker::LowerRoman:
        case ListMarker::UpperRoman: {
            static const std::pair<int, const char*> kNumerals[] = {
                {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
                {100, "c"}, {90, "xc"}, {50, "l"}, {40, "xl"},
                {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
            std::string result;
            int n = std::max(1, index);
            for (const auto& [value, numeral] : kNumerals) {
                while (n >= value) {
                    result += numeral;
                    n -= value;
                }
            }
            if (marker == ListMarker::UpperRoman) {
                std::transform(result.begin(), result.end(), result.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::toupper(c));
                               });
            }
            return result + ". ";
        }
    }
    return "";
}

} // namespace HTML
} // namespace UltraCanvas
