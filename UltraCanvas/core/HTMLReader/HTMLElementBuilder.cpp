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

bool IsBlockDisplay(DisplayMode d) {
    return d == DisplayMode::Block || d == DisplayMode::ListItem ||
           d == DisplayMode::Table || d == DisplayMode::TableRow ||
           d == DisplayMode::TableCell;
}

void CollapseWhitespaceInto(const std::string& text, std::string& out) {
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
                out += ' ';
            }
        } else {
            out += c;
        }
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
    result.warnings = warnings;
    result.elementCount = elementCount;
    return result;
}

std::string ElementBuilder::MakeId(const std::string& hint) {
    return "html_" + hint + "_" + std::to_string(nextId++);
}

// ============================================================================
// BLOCK CONSTRUCTION
// ============================================================================

std::shared_ptr<UltraCanvasContainer> ElementBuilder::BuildBlock(Node& element) {
    auto container = std::make_shared<UltraCanvasContainer>(MakeId(element.tag));
    ++elementCount;

    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*container, style);
    container->layout.SetDisplay(CSSLayout::DisplayType::Block);

    BuildChildrenInto(*container, element);
    return container;
}

void ElementBuilder::BuildChildrenInto(UltraCanvasContainer& parent, Node& element,
                                       int listItemIndex) {
    const ComputedStyle& blockStyle = resolver.StyleOf(&element);

    std::vector<Node*> inlineRun;
    bool markerPending = (element.tag == "li");
    int listCounter = 0;

    auto flushRun = [&]() {
        if (inlineRun.empty() && !markerPending) return;
        std::string marker;
        if (markerPending) {
            marker = MarkerText(blockStyle.listMarker, listItemIndex);
            markerPending = false;
        }
        auto label = BuildInlineRun(inlineRun, blockStyle, marker);
        if (label) {
            parent.AddChild(label);
            ++elementCount;
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

        if (child.tag == "img") {
            flushRun();
            if (opts.enableImages) {
                if (auto image = BuildImage(child)) {
                    parent.AddChild(std::static_pointer_cast<UltraCanvasUIElement>(image));
                    ++elementCount;
                }
            }
            continue;
        }
        if (child.tag == "hr") {
            flushRun();
            if (auto rule = BuildRule(child)) {
                parent.AddChild(std::static_pointer_cast<UltraCanvasUIElement>(rule));
                ++elementCount;
            }
            continue;
        }
        if (childStyle.display == DisplayMode::Table) {
            flushRun();
            if (auto table = BuildTable(child)) {
                parent.AddChild(table);
            }
            continue;
        }

        if (childStyle.display == DisplayMode::ListItem) {
            flushRun();
            auto item = std::make_shared<UltraCanvasContainer>(MakeId("li"));
            ++elementCount;
            ApplyBoxStyle(*item, childStyle);
            item->layout.SetDisplay(CSSLayout::DisplayType::Block);
            BuildChildrenInto(*item, child, ++listCounter);
            parent.AddChild(item);
            continue;
        }

        if (IsBlockDisplay(childStyle.display)) {
            flushRun();
            parent.AddChild(BuildBlock(child));
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
    if (!markerPrefix.empty()) {
        markup += EscapeMarkup(markerPrefix);
    }

    for (const Node* node : run) {
        AppendInlineMarkup(*node, blockStyle, blockStyle.preserveWhitespace, markup);
    }

    // Trim a leading collapse-space.
    size_t begin = markup.find_first_not_of(' ');
    if (begin == std::string::npos) return nullptr;
    if (begin > 0) markup = markup.substr(begin);
    while (!markup.empty() && markup.back() == ' ') markup.pop_back();

    if (!MarkupHasVisibleText(markup)) return nullptr;

    auto label = std::make_shared<UltraCanvasLabel>(MakeId("text"));
    ConfigureLabel(*label, blockStyle);
    label->SetTextIsMarkup(true);
    label->SetText(markup);
    return label;
}

void ElementBuilder::AppendInlineMarkup(const Node& node, const ComputedStyle& runStyle,
                                        bool preserveWhitespace, std::string& out) {
    if (node.type == NodeType::Text) {
        std::string escaped = EscapeMarkup(node.text);
        if (preserveWhitespace) {
            out += escaped;
        } else {
            CollapseWhitespaceInto(escaped, out);
        }
        return;
    }
    if (node.type != NodeType::Element) return;

    const ComputedStyle& style = resolver.StyleOf(&node);
    if (style.display == DisplayMode::Hidden) return;

    if (node.tag == "br") {
        out += '\n';
        return;
    }
    if (node.tag == "img") {
        // Inline images inside a text run are not supported yet; note the alt
        // text so nothing silently disappears.
        std::string alt = node.GetAttribute("alt");
        if (!alt.empty()) out += EscapeMarkup("[" + alt + "]");
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
    bool childPre = preserveWhitespace || style.preserveWhitespace;
    for (const auto& child : node.children) {
        AppendInlineMarkup(*child, style, childPre, out);
    }
    out += suffix;
}

// ============================================================================
// REPLACED / SPECIAL ELEMENTS
// ============================================================================

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildImage(Node& element) {
    std::string src = element.GetAttribute("src");
    if (src.empty()) src = element.GetAttribute("href");     // SVG <image>
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
    ApplyBoxStyle(*image, style);
    // Without explicit dimensions the element reports the image's natural
    // size through MeasureOwnContent.
    return image;
}

std::shared_ptr<UltraCanvasUIElement> ElementBuilder::BuildRule(Node& element) {
    auto rule = std::make_shared<UltraCanvasContainer>(MakeId("hr"));
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
    auto table = std::make_shared<UltraCanvasContainer>(MakeId("table"));
    ++elementCount;
    const ComputedStyle& style = resolver.StyleOf(&element);
    ApplyBoxStyle(*table, style);
    table->layout.SetDisplay(CSSLayout::DisplayType::Block);

    std::function<void(Node&)> addRows = [&](Node& parent) {
        for (const auto& childPtr : parent.children) {
            Node& child = *childPtr;
            if (!child.IsElement()) continue;
            if (child.tag == "thead" || child.tag == "tbody" || child.tag == "tfoot") {
                addRows(child);
                continue;
            }
            if (child.tag != "tr") continue;

            auto row = std::make_shared<UltraCanvasContainer>(MakeId("tr"));
            ++elementCount;
            row->layout.SetFlex(CSSLayout::FlexDirection::Row);

            for (const auto& cellPtr : child.children) {
                Node& cell = *cellPtr;
                if (!cell.IsElement()) continue;
                if (cell.tag != "td" && cell.tag != "th") continue;

                const ComputedStyle& cellStyle = resolver.StyleOf(&cell);
                auto cellBox = std::make_shared<UltraCanvasContainer>(MakeId(cell.tag));
                ++elementCount;
                ApplyBoxStyle(*cellBox, cellStyle);
                cellBox->layout.SetDisplay(CSSLayout::DisplayType::Block);
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
                                   const ComputedStyle& style) {
    using CSSLayout::Dimension;

    target.box.margin.top = Dimension::Px(style.marginTop);
    target.box.margin.right = Dimension::Px(style.marginRight);
    target.box.margin.bottom = Dimension::Px(style.marginBottom);
    target.box.margin.left = Dimension::Px(style.marginLeft);

    target.box.padding.top = Dimension::Px(style.paddingTop);
    target.box.padding.right = Dimension::Px(style.paddingRight);
    target.box.padding.bottom = Dimension::Px(style.paddingBottom);
    target.box.padding.left = Dimension::Px(style.paddingLeft);

    if (style.widthPx) {
        target.size.width = Dimension::Px(*style.widthPx);
    } else if (style.widthPercent) {
        target.size.width = Dimension::Pct(*style.widthPercent);
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
