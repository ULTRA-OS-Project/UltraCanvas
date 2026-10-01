// core/HTMLReader/HTMLElementBuilder.cpp
// DOM + computed styles → native UltraCanvas element tree on CSSLayout.
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
// Last Modified: 2026-10-01
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
Size2Df ImageContentSize(const ComputedStyle& style, const UCImage& raster) {
    float w = static_cast<float>(raster.GetWidth());
    float h = static_cast<float>(raster.GetHeight());
    if (style.widthPx && style.heightPx) { w = *style.widthPx; h = *style.heightPx; }
    else if (style.widthPx && w > 0.f)  { h = h * *style.widthPx / w;  w = *style.widthPx; }
    else if (style.heightPx && h > 0.f) { w = w * *style.heightPx / h; h = *style.heightPx; }
    return Size2Df(w, h);
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

    BuildChildrenInto(*container, element);
    return container;
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
    bool markerPending = (element.tag == "li");
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
    auto addFlowChild = [&](std::shared_ptr<UltraCanvasUIElement> child,
                            float topMargin, float bottomMargin) {
        imageLine.reset();          // anything else in the flow ends the image line
        float spacing = anyFlowChild ? std::max(pendingMargin, topMargin)
                                     : topMargin;
        if (spacing > 0.5f) {
            auto spacer = MakeContainer("gap");
            spacer->size.height = CSSLayout::Dimension::Px(spacing);
            spacer->layoutItem.SetFlexShrink(0.f);
            parent.AddChild(spacer);
        }
        parent.AddChild(std::move(child));
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
            if (inlineRun.empty() && !markerPending) return;
            std::string marker;
            if (markerPending) {
                marker = MarkerText(blockStyle.listMarker, listItemIndex);
                markerPending = false;
            }
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

        std::string marker;
        if (markerPending) {
            marker = MarkerText(blockStyle.listMarker, listItemIndex);
            markerPending = false;
        }
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
        }
        if (inlineImage && imageLine) {
            row->RemoveChild(image);
            // The space goes after the image before it: at the end of a full
            // line it is lost as a browser loses it, rather than indenting
            // the next line.
            if (spaced && lastImage) {
                const float space = st.fontSizePx * 0.28f;   // a space, in a common text face
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

    std::function<void(Node&)> processChild = [&](Node& child) {
        if (child.type == NodeType::Comment) return;

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
            flushRun();
            addFlowChild(PlaceByAutoMargins(BuildBlock(child), childStyle),
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
                    if (style.HasBorder()) {
                        // One border round an inline image: its widest side.
                        const BorderSide& b = style.WidestBorder();
                        f.borderWidth = b.Width();
                        f.borderColor = ToColor(b.color);
                    }
                    if (style.backgroundColor) f.background = ToColor(*style.backgroundColor);
                    f.borderRadius = BorderRadiusPx(style,
                        w + f.paddingLeft + f.paddingRight + 2.f * f.borderWidth,
                        h + f.paddingTop + f.paddingBottom + 2.f * f.borderWidth);
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
    ApplyBoxStyle(*image, boxStyle, /*fillWidth=*/false);
    image->box.boxSizing = CSSLayout::BoxSizing::ContentBox;
    image->box.padding.left = CSSLayout::Dimension::Px(style.paddingLeft);
    image->box.padding.right = CSSLayout::Dimension::Px(style.paddingRight);
    image->box.margin.left = CSSLayout::Dimension::Px(style.marginLeft);
    image->box.margin.right = CSSLayout::Dimension::Px(style.marginRight);
    // Without explicit dimensions the element reports the image's natural
    // size through MeasureOwnContent. Cap at the column width so oversized
    // images (covers, photos) shrink to fit instead of overflowing; a zero
    // minimum lets the row below shrink it (its min-content is its natural
    // width, which would otherwise pin it).
    CSSLayout::BoxConstraints constraints;
    constraints.maxWidth = CSSLayout::Dimension::Pct(100.f);
    constraints.minWidth = CSSLayout::Dimension::Px(0.f);
    image->boxConstraints = constraints;
    image->layoutItem.SetFlexGrow(0).SetFlexShrink(1);
    if (!linkHref.empty() && opts.onLinkActivated) {
        image->SetClickable(true);
        image->onClick = [activate = opts.onLinkActivated, linkHref]() { activate(linkHref); };
        image->SetTooltip(linkHref);
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
    BuildChildrenInto(*box, element);
    // As wide as its content (shrink-to-fit), narrower only when the line is.
    box->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
    return box;
}

std::shared_ptr<UltraCanvasContainer> ElementBuilder::BuildTable(Node& element, bool inlineBox) {
    const ComputedStyle& style = resolver.StyleOf(&element);
    auto table = MakeContainer("table");
    RegisterAnchors(element, table);
    ++elementCount;
    ApplyBoxStyle(*table, style, /*fillWidth=*/false, /*realMargins=*/inlineBox);
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
        // A px / % width is the column's width (the table layout reads it).
        ApplyBoxStyle(*cellBox, cellStyle, /*fillWidth=*/false);
        if (!cellStyle.backgroundColor && rowStyle.backgroundColor) {
            cellBox->SetBackgroundColor(ToColor(*rowStyle.backgroundColor));
        }
        if (ruledCells && !cellStyle.HasBorder()) {
            cellBox->SetBorders(1.f, Color(128, 128, 128, 255));
        }
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
        for (const auto& cellPtr : row.children) {
            Node& cell = *cellPtr;
            if (!cell.IsElement() || !isCell(cell)) continue;
            if (resolver.StyleOf(&cell).display == DisplayMode::Hidden) continue;
            addCell(cell, rowStyle, r, c);
        }
    }

    const bool fullWidth = style.widthPercent && *style.widthPercent >= 99.5f;
    if (inlineBox || fullWidth) {
        if (inlineBox) table->layoutItem.SetFlexGrow(0.f).SetFlexShrink(1.f);
        return table;
    }

    // A table narrower than its line: <table align>, else the alignment its
    // container asks for (<td align="right">, <center>) - as mail clients
    // render it.
    TextAlignMode align = style.textAlign;
    std::string alignAttr = element.GetAttribute("align");
    std::transform(alignAttr.begin(), alignAttr.end(), alignAttr.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (style.marginLeftAuto && style.marginRightAuto) align = TextAlignMode::Center;
    else if (style.marginLeftAuto) align = TextAlignMode::Right;
    if (alignAttr == "center" || alignAttr == "middle") align = TextAlignMode::Center;
    else if (alignAttr == "right") align = TextAlignMode::Right;
    else if (alignAttr == "left") align = TextAlignMode::Left;
    if (align != TextAlignMode::Center && align != TextAlignMode::Right) return table;

    auto line = MakeContainer("tableline");
    line->size.width = CSSLayout::Dimension::Pct(100.f);
    line->layout.SetFlexRow()
                .SetFlexJustifyContent(align == TextAlignMode::Center
                                           ? CSSLayout::JustifyContent::Center
                                           : CSSLayout::JustifyContent::FlexEnd)
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
                                   bool realMargins) {
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

    if (style.widthPx) {
        target.size.width = Dimension::Px(*style.widthPx);
    } else if (style.widthPercent) {
        target.size.width = Dimension::Pct(*style.widthPercent);
    } else if (fillWidth) {
        target.size.width = Dimension::Pct(100.f);
    }
    if (style.heightPx) {
        target.size.height = Dimension::Px(*style.heightPx);
    }

    if (style.backgroundColor) {
        target.SetBackgroundColor(ToColor(*style.backgroundColor));
    }
    if (style.maxWidthPx) {
        CSSLayout::BoxConstraints limits = target.boxConstraints.value_or(CSSLayout::BoxConstraints{});
        limits.maxWidth = Dimension::Px(*style.maxWidthPx);
        target.boxConstraints = limits;
    }
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
                          (style.widthPercent && *style.widthPercent < 99.5f);
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

void ElementBuilder::ConfigureLabel(UltraCanvasLabel& label, const ComputedStyle& style,
                                    bool noWrap) {
    LabelStyle labelStyle;
    labelStyle.fontStyle.fontFamily =
        style.monospace && style.fontFamily.empty() ? "monospace" : style.fontFamily;
    // FontStyle sizes are points; CSS sizes are px (96 dpi). Inline <span
    // size> markup converts the same way (PangoSize), so a 15px button caption
    // is no longer smaller than the 12px text around it.
    labelStyle.fontStyle.fontSize = style.fontSizePx * 72.f / 96.f;
    labelStyle.fontStyle.fontWeight = style.bold ? FontWeight::Bold : FontWeight::Normal;
    labelStyle.fontStyle.fontSlant = style.italic ? FontSlant::Italic : FontSlant::Normal;
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
