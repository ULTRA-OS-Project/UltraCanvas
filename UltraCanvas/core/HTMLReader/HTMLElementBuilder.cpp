// core/HTMLReader/HTMLElementBuilder.cpp
// DOM + computed styles → native UltraCanvas element tree on CSSLayout.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"

#include "UltraCanvasImageElement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {
namespace HTML {

namespace {

Color ToColor(const CssColor& c) { return Color(c.r, c.g, c.b, c.a); }

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
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", c.r, c.g, c.b);
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

    BuildChildrenInto(*container, element);
    return container;
}

void ElementBuilder::BuildChildrenInto(UltraCanvasContainer& parent, Node& element,
                                       int listItemIndex) {
    const ComputedStyle& blockStyle = resolver.StyleOf(&element);

    std::vector<Node*> inlineRun;
    bool markerPending = (element.tag == "li");
    int listCounter = 0;

    // Vertical rhythm: the engine's Block flow stacks children edge-to-edge,
    // so CSS margins become explicit spacer elements. Adjacent top/bottom
    // margins collapse to the larger one, like CSS margin collapsing.
    float pendingMargin = 0.f;
    bool anyFlowChild = false;
    auto addFlowChild = [&](std::shared_ptr<UltraCanvasUIElement> child,
                            float topMargin, float bottomMargin) {
        float spacing = anyFlowChild ? std::max(pendingMargin, topMargin)
                                     : topMargin;
        if (spacing > 0.5f) {
            auto spacer = MakeContainer("gap");
            spacer->size.height = CSSLayout::Dimension::Px(spacing);
            parent.AddChild(spacer);
        }
        parent.AddChild(std::move(child));
        ++elementCount;
        pendingMargin = bottomMargin;
        anyFlowChild = true;
    };

    auto flushRun = [&]() {
        if (inlineRun.empty() && !markerPending) return;
        std::string marker;
        if (markerPending) {
            marker = MarkerText(blockStyle.listMarker, listItemIndex);
            markerPending = false;
        }
        auto label = BuildInlineRun(inlineRun, blockStyle, marker);
        // Anchors inside the run map to its label; runs that produce no label
        // (e.g. an empty <a id="..."/> target) fall back to the enclosing
        // block so #fragment navigation still lands nearby.
        std::shared_ptr<UltraCanvasUIElement> anchorTarget = label;
        if (!anchorTarget) {
            anchorTarget = std::static_pointer_cast<UltraCanvasUIElement>(
                parent.shared_from_this());
        }
        for (const Node* node : inlineRun) {
            RegisterAnchors(*node, anchorTarget, /*deep=*/true);
        }
        if (label) {
            addFlowChild(label, 0.f, 0.f);
        }
        inlineRun.clear();
    };

    for (const auto& childPtr : element.children) {
        Node& child = *childPtr;

        if (child.type == NodeType::Comment) continue;

        if (child.type == NodeType::Text) {
            inlineRun.push_back(&child);
            continue;
        }
        if (!child.IsElement()) continue;

        const ComputedStyle& childStyle = resolver.StyleOf(&child);
        if (childStyle.display == DisplayMode::Hidden) continue;

        if (child.tag == "img" || child.tag == "image") {
            flushRun();
            if (opts.enableImages) {
                if (auto image = BuildImage(child)) {
                    RegisterAnchors(child, image);
                    addFlowChild(image, childStyle.marginTop, childStyle.marginBottom);
                }
            }
            continue;
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
                    if (auto image = BuildImage(*svgImage)) {
                        RegisterAnchors(child, image, /*deep=*/true);
                        addFlowChild(image, childStyle.marginTop, childStyle.marginBottom);
                    }
                } else {
                    warnings.push_back("svg without raster <image> skipped");
                }
            }
            continue;
        }
        if (child.tag == "hr") {
            flushRun();
            if (auto rule = BuildRule(child)) {
                RegisterAnchors(child, rule);
                addFlowChild(rule, childStyle.marginTop, childStyle.marginBottom);
            }
            continue;
        }
        if (childStyle.display == DisplayMode::Table) {
            flushRun();
            if (auto table = BuildTable(child)) {
                addFlowChild(table, childStyle.marginTop, childStyle.marginBottom);
            }
            continue;
        }

        if (childStyle.display == DisplayMode::ListItem) {
            flushRun();
            auto item = MakeContainer("li");
            RegisterAnchors(child, item);
            ApplyBoxStyle(*item, childStyle);
            ConfigureBlockLayout(*item);
            BuildChildrenInto(*item, child, ++listCounter);
            addFlowChild(item, childStyle.marginTop, childStyle.marginBottom);
            continue;
        }

        if (IsBlockDisplay(childStyle.display)) {
            flushRun();
            addFlowChild(BuildBlock(child), childStyle.marginTop, childStyle.marginBottom);
            continue;
        }

        // Inline / inline-block content joins the current run.
        inlineRun.push_back(&child);
    }

    flushRun();
}

// ============================================================================
// INLINE RUNS → LABEL WITH PANGO MARKUP
// ============================================================================

std::shared_ptr<UltraCanvasLabel> ElementBuilder::BuildInlineRun(
    const std::vector<Node*>& run, const ComputedStyle& blockStyle,
    const std::string& markerPrefix) {

    std::string markup;
    runPlain.clear();
    runLinks.clear();
    if (!markerPrefix.empty()) {
        markup += EscapeMarkup(markerPrefix);
        runPlain += markerPrefix;
    }

    for (const Node* node : run) {
        AppendInlineMarkup(*node, blockStyle, blockStyle.preserveWhitespace, markup);
    }

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
    ConfigureLabel(*label, blockStyle);
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
    return label;
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
    if (node.tag == "img") {
        // Inline images inside a text run are not supported yet; note the alt
        // text so nothing silently disappears.
        std::string alt = node.GetAttribute("alt");
        if (!alt.empty()) {
            out += EscapeMarkup("[" + alt + "]");
            runPlain += "[" + alt + "]";
        }
        return;
    }

    // Span wrappers derived from the difference to the enclosing run style.
    std::string prefix, suffix;
    auto wrap = [&](const std::string& open, const std::string& close) {
        prefix += open;
        suffix = close + suffix;
    };

    if (style.bold && !runStyle.bold) wrap("<b>", "</b>");
    if (style.italic && !runStyle.italic) wrap("<i>", "</i>");
    if (style.underline && !runStyle.underline) wrap("<u>", "</u>");
    if (style.strikethrough && !runStyle.strikethrough) wrap("<s>", "</s>");
    if (style.monospace && !runStyle.monospace) wrap("<tt>", "</tt>");
    if (node.tag == "sub") wrap("<sub>", "</sub>");
    else if (node.tag == "sup") wrap("<sup>", "</sup>");
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

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildImage(Node& element) {
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
    ApplyBoxStyle(*image, style, /*fillWidth=*/false);
    // Without explicit dimensions the element reports the image's natural
    // size through MeasureOwnContent. Cap at the column width so oversized
    // images (covers, photos) shrink to fit instead of overflowing.
    CSSLayout::BoxConstraints constraints;
    constraints.maxWidth = CSSLayout::Dimension::Pct(100.f);
    image->boxConstraints = constraints;
    return image;
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildRule(Node& element) {
    auto rule = MakeContainer("hr");
    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*rule, style);
    rule->size.height = CSSLayout::Dimension::Px(
        style.borderWidth > 0 ? style.borderWidth : 1.f);
    Color line = style.borderWidth > 0 ? ToColor(style.borderColor)
                                       : Color(160, 160, 160, 255);
    rule->SetBackgroundColor(line);
    return rule;
}

std::shared_ptr<UltraCanvasContainer> ElementBuilder::BuildTable(Node& element) {
    // v1 table support: each row is a flex row, cells share the width
    // equally (flex-grow 1). TODO: use CSSLayout Grid for real column sizing.
    auto table = MakeContainer("table");
    RegisterAnchors(element, table);
    ++elementCount;
    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*table, style);
    ConfigureBlockLayout(*table);

    std::function<void(Node&)> addRows = [&](Node& parent) {
        for (const auto& childPtr : parent.children) {
            Node& child = *childPtr;
            if (!child.IsElement()) continue;
            if (child.tag == "thead" || child.tag == "tbody" || child.tag == "tfoot") {
                addRows(child);
                continue;
            }
            if (child.tag != "tr") continue;

            auto row = MakeContainer("tr");
            ++elementCount;
            row->layout.SetFlex(CSSLayout::FlexDirection::Row);

            for (const auto& cellPtr : child.children) {
                Node& cell = *cellPtr;
                if (!cell.IsElement()) continue;
                if (cell.tag != "td" && cell.tag != "th") continue;

                const ComputedStyle& cellStyle = resolver.StyleOf(&cell);
                auto cellBox = MakeContainer(cell.tag);
                RegisterAnchors(cell, cellBox);
                ++elementCount;
                ApplyBoxStyle(*cellBox, cellStyle);
                ConfigureBlockLayout(*cellBox);
                cellBox->layoutItem.SetFlexGrow(1.f).SetFlexShrink(1.f)
                       .SetFlexBasis(CSSLayout::Dimension::Px(0));
                BuildChildrenInto(*cellBox, cell);
                row->AddChild(cellBox);
            }
            table->AddChild(row);
        }
    };
    addRows(element);
    return table;
}

// ============================================================================
// STYLE MAPPING
// ============================================================================

void ElementBuilder::ApplyBoxStyle(UltraCanvasUIElement& target,
                                   const ComputedStyle& style, bool fillWidth) {
    using CSSLayout::Dimension;

    // Vertical margins become sibling spacer elements (see BuildChildrenInto);
    // horizontal margins fold into padding so width:100% never overflows.
    target.box.boxSizing = CSSLayout::BoxSizing::BorderBox;

    target.box.padding.top = Dimension::Px(style.paddingTop);
    target.box.padding.right = Dimension::Px(style.paddingRight + style.marginRight);
    target.box.padding.bottom = Dimension::Px(style.paddingBottom);
    target.box.padding.left = Dimension::Px(style.paddingLeft + style.marginLeft);

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
    if (style.borderWidth > 0) {
        target.box.border.top = Dimension::Px(style.borderWidth);
        target.box.border.right = Dimension::Px(style.borderWidth);
        target.box.border.bottom = Dimension::Px(style.borderWidth);
        target.box.border.left = Dimension::Px(style.borderWidth);
    }
}

void ElementBuilder::ConfigureLabel(UltraCanvasLabel& label, const ComputedStyle& style) {
    LabelStyle labelStyle;
    labelStyle.fontStyle.fontFamily =
        style.monospace && style.fontFamily.empty() ? "monospace" : style.fontFamily;
    labelStyle.fontStyle.fontSize = style.fontSizePx;
    labelStyle.fontStyle.fontWeight = style.bold ? FontWeight::Bold : FontWeight::Normal;
    labelStyle.fontStyle.fontSlant = style.italic ? FontSlant::Italic : FontSlant::Normal;
    labelStyle.textColor = ToColor(style.color);
    labelStyle.wrap = style.preserveWhitespace ? TextWrap::WrapWordChar : TextWrap::WrapWord;

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
