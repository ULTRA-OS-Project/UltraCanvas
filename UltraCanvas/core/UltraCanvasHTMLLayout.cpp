// core/UltraCanvasHTMLLayout.cpp
// HTML Layout Engine and Style Resolver - Completes the HTML Converter
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "UltraCanvasHTMLConverter.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

// ============================================================================
// HTML STYLE RESOLVER IMPLEMENTATION
// ============================================================================

HTMLStyleResolver::HTMLStyleResolver() {
}

void HTMLStyleResolver::AddStylesheet(const std::string& css) {
    UltraWeb::CSSParser parser;
    auto stylesheet = parser.Parse(css);
    if (!stylesheet.HasErrors()) {
        stylesheets.push_back(std::move(stylesheet));
    }
}

void HTMLStyleResolver::AddInlineStyle(HTMLElement* element, const std::string& style) {
    if (!element || style.empty()) return;
    
    // Parse inline style as a rule body
    UltraWeb::CSSParser parser;
    std::string wrappedCSS = "* { " + style + " }";
    auto stylesheet = parser.Parse(wrappedCSS);
    
    if (!stylesheet.HasErrors() && !stylesheet.rules.empty()) {
        for (const auto& decl : stylesheet.rules[0].declarations) {
            ApplyDeclaration(decl, element->computedStyle);
        }
    }
}

void HTMLStyleResolver::ClearStyles() {
    stylesheets.clear();
}

void HTMLStyleResolver::ResolveStyles(HTMLDocument* document) {
    if (!document || !document->documentElement) return;
    
    std::function<void(HTMLElement*, const HTMLComputedStyle*)> resolveRecursive;
    resolveRecursive = [this, &resolveRecursive](HTMLElement* element, const HTMLComputedStyle* parentStyle) {
        ResolveElementStyle(element, parentStyle);
        
        for (const auto& child : element->GetChildren()) {
            if (child->GetNodeType() == HTMLNodeType::Element) {
                resolveRecursive(static_cast<HTMLElement*>(child.get()), &element->computedStyle);
            }
        }
    };
    
    resolveRecursive(document->documentElement.get(), nullptr);
}

void HTMLStyleResolver::ResolveElementStyle(HTMLElement* element, const HTMLComputedStyle* parentStyle) {
    if (!element) return;
    
    // Start with default style
    element->computedStyle = GetDefaultStyle(element->GetTagType());
    
    // Inherit from parent
    if (parentStyle) {
        InheritStyle(element->computedStyle, parentStyle);
    }
    
    // Apply stylesheet rules
    ApplyMatchingRules(element, element->computedStyle);
    
    // Apply inline styles
    std::string inlineStyle = element->GetStyle();
    if (!inlineStyle.empty()) {
        AddInlineStyle(element, inlineStyle);
    }
}

HTMLComputedStyle HTMLStyleResolver::GetDefaultStyle(HTMLTagType type) {
    HTMLComputedStyle style;
    
    // Set defaults based on tag type
    switch (type) {
        // Headings
        case HTMLTagType::H1:
            style.fontSize = 32.0f;
            style.fontWeight = 700;
            style.marginTop = 21.44f;
            style.marginBottom = 21.44f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::H2:
            style.fontSize = 24.0f;
            style.fontWeight = 700;
            style.marginTop = 19.92f;
            style.marginBottom = 19.92f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::H3:
            style.fontSize = 18.72f;
            style.fontWeight = 700;
            style.marginTop = 18.72f;
            style.marginBottom = 18.72f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::H4:
            style.fontSize = 16.0f;
            style.fontWeight = 700;
            style.marginTop = 21.28f;
            style.marginBottom = 21.28f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::H5:
            style.fontSize = 13.28f;
            style.fontWeight = 700;
            style.marginTop = 22.18f;
            style.marginBottom = 22.18f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::H6:
            style.fontSize = 10.72f;
            style.fontWeight = 700;
            style.marginTop = 24.97f;
            style.marginBottom = 24.97f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        // Paragraph
        case HTMLTagType::P:
            style.marginTop = 16.0f;
            style.marginBottom = 16.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        // Text formatting (inline)
        case HTMLTagType::Strong:
        case HTMLTagType::B:
            style.fontWeight = 700;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::Em:
        case HTMLTagType::I:
            style.fontItalic = true;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::U:
            style.textDecoration = HTMLComputedStyle::TextDecoration::Underline;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::S:
        case HTMLTagType::Del:
            style.textDecoration = HTMLComputedStyle::TextDecoration::LineThrough;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::Small:
            style.fontSize = 13.0f;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::Mark:
            style.backgroundColor = Color(255, 255, 0, 255);
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::Sub:
            style.fontSize = 12.0f;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::Sup:
            style.fontSize = 12.0f;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
            
        // Code
        case HTMLTagType::Code:
        case HTMLTagType::Samp:
        case HTMLTagType::Kbd:
        case HTMLTagType::Var:
            style.fontFamily = "monospace";
            style.display = HTMLComputedStyle::Display::Inline;
            break;
        case HTMLTagType::Pre:
            style.fontFamily = "monospace";
            style.marginTop = 16.0f;
            style.marginBottom = 16.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        // Links
        case HTMLTagType::A:
            style.color = Color(0, 0, 238, 255);
            style.textDecoration = HTMLComputedStyle::TextDecoration::Underline;
            style.cursor = HTMLComputedStyle::Cursor::Pointer;
            style.display = HTMLComputedStyle::Display::Inline;
            break;
            
        // Lists
        case HTMLTagType::Ul:
        case HTMLTagType::Ol:
            style.paddingLeft = 40.0f;
            style.marginTop = 16.0f;
            style.marginBottom = 16.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::Li:
            style.display = HTMLComputedStyle::Display::ListItem;
            break;
        case HTMLTagType::Dl:
            style.marginTop = 16.0f;
            style.marginBottom = 16.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::Dt:
            style.fontWeight = 700;
            style.display = HTMLComputedStyle::Display::Block;
            break;
        case HTMLTagType::Dd:
            style.marginLeft = 40.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        // Blockquote
        case HTMLTagType::Blockquote:
            style.marginTop = 16.0f;
            style.marginBottom = 16.0f;
            style.marginLeft = 40.0f;
            style.marginRight = 40.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        // Horizontal rule
        case HTMLTagType::Hr:
            style.marginTop = 8.0f;
            style.marginBottom = 8.0f;
            style.borderTopWidth = 1.0f;
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        // Images
        case HTMLTagType::Img:
            style.display = HTMLComputedStyle::Display::Inline;
            break;
            
        // Tables
        case HTMLTagType::Table:
            style.borderTopWidth = 1.0f;
            style.borderRightWidth = 1.0f;
            style.borderBottomWidth = 1.0f;
            style.borderLeftWidth = 1.0f;
            style.display = HTMLComputedStyle::Display::Table;
            break;
        case HTMLTagType::Tr:
            style.display = HTMLComputedStyle::Display::TableRow;
            break;
        case HTMLTagType::Td:
        case HTMLTagType::Th:
            style.paddingTop = 4.0f;
            style.paddingRight = 4.0f;
            style.paddingBottom = 4.0f;
            style.paddingLeft = 4.0f;
            style.display = HTMLComputedStyle::Display::TableCell;
            if (type == HTMLTagType::Th) {
                style.fontWeight = 700;
                style.textAlign = HTMLComputedStyle::TextAlign::Center;
            }
            break;
            
        // Form elements
        case HTMLTagType::Button:
            style.paddingTop = 4.0f;
            style.paddingRight = 8.0f;
            style.paddingBottom = 4.0f;
            style.paddingLeft = 8.0f;
            style.cursor = HTMLComputedStyle::Cursor::Pointer;
            style.display = HTMLComputedStyle::Display::InlineBlock;
            break;
        case HTMLTagType::Input:
        case HTMLTagType::Textarea:
        case HTMLTagType::Select:
            style.paddingTop = 2.0f;
            style.paddingRight = 4.0f;
            style.paddingBottom = 2.0f;
            style.paddingLeft = 4.0f;
            style.borderTopWidth = 1.0f;
            style.borderRightWidth = 1.0f;
            style.borderBottomWidth = 1.0f;
            style.borderLeftWidth = 1.0f;
            style.display = HTMLComputedStyle::Display::InlineBlock;
            break;
            
        // Span (inline)
        case HTMLTagType::Span:
            style.display = HTMLComputedStyle::Display::Inline;
            break;
            
        // Block containers
        case HTMLTagType::Div:
        case HTMLTagType::Article:
        case HTMLTagType::Section:
        case HTMLTagType::Header:
        case HTMLTagType::Footer:
        case HTMLTagType::Nav:
        case HTMLTagType::Aside:
        case HTMLTagType::Main:
        case HTMLTagType::Figure:
        case HTMLTagType::Figcaption:
        case HTMLTagType::Address:
        case HTMLTagType::Details:
        case HTMLTagType::Summary:
        case HTMLTagType::Fieldset:
        case HTMLTagType::Form:
            style.display = HTMLComputedStyle::Display::Block;
            break;
            
        default:
            style.display = HTMLComputedStyle::Display::Inline;
            break;
    }
    
    return style;
}

void HTMLStyleResolver::InheritStyle(HTMLComputedStyle& style, const HTMLComputedStyle* parentStyle) {
    if (!parentStyle) return;
    
    // Inherited properties
    style.color = parentStyle->color;
    style.fontFamily = parentStyle->fontFamily;
    style.fontSize = parentStyle->fontSize;
    style.fontWeight = parentStyle->fontWeight;
    style.fontItalic = parentStyle->fontItalic;
    style.lineHeight = parentStyle->lineHeight;
    style.textAlign = parentStyle->textAlign;
    style.listStyleType = parentStyle->listStyleType;
    style.cursor = parentStyle->cursor;
}

void HTMLStyleResolver::ApplyMatchingRules(HTMLElement* element, HTMLComputedStyle& style) {
    for (const auto& stylesheet : stylesheets) {
        for (const auto& rule : stylesheet.rules) {
            for (const auto& selector : rule.selectors) {
                if (SelectorMatches(selector, element)) {
                    for (const auto& decl : rule.declarations) {
                        ApplyDeclaration(decl, style);
                    }
                    break;
                }
            }
        }
    }
}

bool HTMLStyleResolver::SelectorMatches(const UltraWeb::CSSSelector& selector, 
                                        const HTMLElement* element) {
    if (!element) return false;
    
    for (const auto& part : selector.parts) {
        switch (part.type) {
            case UltraWeb::CSSSelectorType::Element:
                if (element->GetTagName() != part.value) return false;
                break;
            case UltraWeb::CSSSelectorType::Class:
                if (!element->HasClass(part.value)) return false;
                break;
            case UltraWeb::CSSSelectorType::Id:
                if (element->GetId() != part.value) return false;
                break;
            case UltraWeb::CSSSelectorType::Universal:
                break;
            default:
                break;
        }
    }
    
    return true;
}

void HTMLStyleResolver::ApplyDeclaration(const UltraWeb::CSSDeclaration& decl, 
                                         HTMLComputedStyle& style) {
    const std::string& prop = decl.property;
    const auto& value = decl.value;
    
    // Display
    if (prop == "display") {
        if (value.keyword == "none") style.display = HTMLComputedStyle::Display::None;
        else if (value.keyword == "block") style.display = HTMLComputedStyle::Display::Block;
        else if (value.keyword == "inline") style.display = HTMLComputedStyle::Display::Inline;
        else if (value.keyword == "inline-block") style.display = HTMLComputedStyle::Display::InlineBlock;
        else if (value.keyword == "flex") style.display = HTMLComputedStyle::Display::Flex;
        else if (value.keyword == "grid") style.display = HTMLComputedStyle::Display::Grid;
        else if (value.keyword == "contents") style.display = HTMLComputedStyle::Display::Contents;
    }
    // Position
    else if (prop == "position") {
        if (value.keyword == "static") style.position = HTMLComputedStyle::Position::Static;
        else if (value.keyword == "relative") style.position = HTMLComputedStyle::Position::Relative;
        else if (value.keyword == "absolute") style.position = HTMLComputedStyle::Position::Absolute;
        else if (value.keyword == "fixed") style.position = HTMLComputedStyle::Position::Fixed;
        else if (value.keyword == "sticky") style.position = HTMLComputedStyle::Position::Sticky;
    }
    // Colors
    else if (prop == "color") {
        style.color = ParseColor(value);
    }
    else if (prop == "background-color") {
        style.backgroundColor = ParseColor(value);
    }
    else if (prop == "background") {
        // Simplified: only handle color
        style.backgroundColor = ParseColor(value);
    }
    else if (prop == "border-color") {
        style.borderColor = ParseColor(value);
    }
    // Typography
    else if (prop == "font-size") {
        style.fontSize = ParseLength(value, style.fontSize, 16.0f);
    }
    else if (prop == "font-weight") {
        if (value.type == UltraWeb::CSSValueType::Keyword) {
            if (value.keyword == "bold") style.fontWeight = 700;
            else if (value.keyword == "normal") style.fontWeight = 400;
            else if (value.keyword == "lighter") style.fontWeight = 300;
            else if (value.keyword == "bolder") style.fontWeight = 900;
        } else if (value.type == UltraWeb::CSSValueType::Number) {
            auto* num = value.AsNumber();
            if (num) style.fontWeight = static_cast<int>(num->value);
        }
    }
    else if (prop == "font-style") {
        style.fontItalic = (value.keyword == "italic" || value.keyword == "oblique");
    }
    else if (prop == "font-family") {
        if (value.type == UltraWeb::CSSValueType::String) {
            auto* str = value.AsString();
            if (str) style.fontFamily = *str;
        } else {
            style.fontFamily = value.keyword;
        }
    }
    else if (prop == "line-height") {
        if (value.type == UltraWeb::CSSValueType::Number) {
            auto* num = value.AsNumber();
            if (num) style.lineHeight = num->value;
        } else {
            style.lineHeight = ParseLength(value, style.fontSize, 16.0f) / style.fontSize;
        }
    }
    else if (prop == "text-align") {
        if (value.keyword == "left") style.textAlign = HTMLComputedStyle::TextAlign::Left;
        else if (value.keyword == "right") style.textAlign = HTMLComputedStyle::TextAlign::Right;
        else if (value.keyword == "center") style.textAlign = HTMLComputedStyle::TextAlign::Center;
        else if (value.keyword == "justify") style.textAlign = HTMLComputedStyle::TextAlign::Justify;
    }
    else if (prop == "text-decoration") {
        if (value.keyword == "none") style.textDecoration = HTMLComputedStyle::TextDecoration::None;
        else if (value.keyword == "underline") style.textDecoration = HTMLComputedStyle::TextDecoration::Underline;
        else if (value.keyword == "overline") style.textDecoration = HTMLComputedStyle::TextDecoration::Overline;
        else if (value.keyword == "line-through") style.textDecoration = HTMLComputedStyle::TextDecoration::LineThrough;
    }
    // Box model - Width/Height
    else if (prop == "width") {
        style.width = ParseLength(value, 0, 16.0f);
        style.widthAuto = (value.keyword == "auto");
    }
    else if (prop == "height") {
        style.height = ParseLength(value, 0, 16.0f);
        style.heightAuto = (value.keyword == "auto");
    }
    else if (prop == "min-width") {
        style.minWidth = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "min-height") {
        style.minHeight = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "max-width") {
        if (value.keyword == "none") style.maxWidth = -1;
        else style.maxWidth = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "max-height") {
        if (value.keyword == "none") style.maxHeight = -1;
        else style.maxHeight = ParseLength(value, 0, 16.0f);
    }
    // Margins
    else if (prop == "margin") {
        float m = ParseLength(value, 0, 16.0f);
        style.marginTop = style.marginRight = style.marginBottom = style.marginLeft = m;
    }
    else if (prop == "margin-top") {
        style.marginTop = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "margin-right") {
        style.marginRight = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "margin-bottom") {
        style.marginBottom = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "margin-left") {
        style.marginLeft = ParseLength(value, 0, 16.0f);
    }
    // Padding
    else if (prop == "padding") {
        float p = ParseLength(value, 0, 16.0f);
        style.paddingTop = style.paddingRight = style.paddingBottom = style.paddingLeft = p;
    }
    else if (prop == "padding-top") {
        style.paddingTop = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "padding-right") {
        style.paddingRight = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "padding-bottom") {
        style.paddingBottom = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "padding-left") {
        style.paddingLeft = ParseLength(value, 0, 16.0f);
    }
    // Border width
    else if (prop == "border-width") {
        float w = ParseLength(value, 0, 16.0f);
        style.borderTopWidth = style.borderRightWidth = style.borderBottomWidth = style.borderLeftWidth = w;
    }
    else if (prop == "border-top-width") {
        style.borderTopWidth = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "border-right-width") {
        style.borderRightWidth = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "border-bottom-width") {
        style.borderBottomWidth = ParseLength(value, 0, 16.0f);
    }
    else if (prop == "border-left-width") {
        style.borderLeftWidth = ParseLength(value, 0, 16.0f);
    }
    // Border radius
    else if (prop == "border-radius") {
        float r = ParseLength(value, 0, 16.0f);
        style.borderRadius = r;
        style.borderTopLeftRadius = style.borderTopRightRadius = r;
        style.borderBottomLeftRadius = style.borderBottomRightRadius = r;
    }
    // Flexbox
    else if (prop == "flex-direction") {
        if (value.keyword == "row") style.flexDirection = HTMLComputedStyle::FlexDirection::Row;
        else if (value.keyword == "row-reverse") style.flexDirection = HTMLComputedStyle::FlexDirection::RowReverse;
        else if (value.keyword == "column") style.flexDirection = HTMLComputedStyle::FlexDirection::Column;
        else if (value.keyword == "column-reverse") style.flexDirection = HTMLComputedStyle::FlexDirection::ColumnReverse;
    }
    else if (prop == "justify-content") {
        if (value.keyword == "flex-start") style.justifyContent = HTMLComputedStyle::JustifyContent::FlexStart;
        else if (value.keyword == "flex-end") style.justifyContent = HTMLComputedStyle::JustifyContent::FlexEnd;
        else if (value.keyword == "center") style.justifyContent = HTMLComputedStyle::JustifyContent::Center;
        else if (value.keyword == "space-between") style.justifyContent = HTMLComputedStyle::JustifyContent::SpaceBetween;
        else if (value.keyword == "space-around") style.justifyContent = HTMLComputedStyle::JustifyContent::SpaceAround;
        else if (value.keyword == "space-evenly") style.justifyContent = HTMLComputedStyle::JustifyContent::SpaceEvenly;
    }
    else if (prop == "align-items") {
        if (value.keyword == "flex-start") style.alignItems = HTMLComputedStyle::AlignItems::FlexStart;
        else if (value.keyword == "flex-end") style.alignItems = HTMLComputedStyle::AlignItems::FlexEnd;
        else if (value.keyword == "center") style.alignItems = HTMLComputedStyle::AlignItems::Center;
        else if (value.keyword == "stretch") style.alignItems = HTMLComputedStyle::AlignItems::Stretch;
        else if (value.keyword == "baseline") style.alignItems = HTMLComputedStyle::AlignItems::Baseline;
    }
    else if (prop == "flex-grow") {
        if (value.type == UltraWeb::CSSValueType::Number) {
            auto* num = value.AsNumber();
            if (num) style.flexGrow = num->value;
        }
    }
    else if (prop == "flex-shrink") {
        if (value.type == UltraWeb::CSSValueType::Number) {
            auto* num = value.AsNumber();
            if (num) style.flexShrink = num->value;
        }
    }
    else if (prop == "gap") {
        style.gap = ParseLength(value, 0, 16.0f);
    }
    // Opacity
    else if (prop == "opacity") {
        if (value.type == UltraWeb::CSSValueType::Number) {
            auto* num = value.AsNumber();
            if (num) style.opacity = std::clamp(num->value, 0.0f, 1.0f);
        }
    }
    // Visibility
    else if (prop == "visibility") {
        style.visible = (value.keyword != "hidden" && value.keyword != "collapse");
    }
    // Overflow
    else if (prop == "overflow") {
        HTMLComputedStyle::Overflow ov = HTMLComputedStyle::Overflow::Visible;
        if (value.keyword == "hidden") ov = HTMLComputedStyle::Overflow::Hidden;
        else if (value.keyword == "scroll") ov = HTMLComputedStyle::Overflow::Scroll;
        else if (value.keyword == "auto") ov = HTMLComputedStyle::Overflow::Auto;
        style.overflowX = style.overflowY = ov;
    }
    else if (prop == "overflow-x") {
        if (value.keyword == "visible") style.overflowX = HTMLComputedStyle::Overflow::Visible;
        else if (value.keyword == "hidden") style.overflowX = HTMLComputedStyle::Overflow::Hidden;
        else if (value.keyword == "scroll") style.overflowX = HTMLComputedStyle::Overflow::Scroll;
        else if (value.keyword == "auto") style.overflowX = HTMLComputedStyle::Overflow::Auto;
    }
    else if (prop == "overflow-y") {
        if (value.keyword == "visible") style.overflowY = HTMLComputedStyle::Overflow::Visible;
        else if (value.keyword == "hidden") style.overflowY = HTMLComputedStyle::Overflow::Hidden;
        else if (value.keyword == "scroll") style.overflowY = HTMLComputedStyle::Overflow::Scroll;
        else if (value.keyword == "auto") style.overflowY = HTMLComputedStyle::Overflow::Auto;
    }
    // Cursor
    else if (prop == "cursor") {
        if (value.keyword == "pointer") style.cursor = HTMLComputedStyle::Cursor::Pointer;
        else if (value.keyword == "text") style.cursor = HTMLComputedStyle::Cursor::Text;
        else if (value.keyword == "move") style.cursor = HTMLComputedStyle::Cursor::Move;
        else if (value.keyword == "not-allowed") style.cursor = HTMLComputedStyle::Cursor::NotAllowed;
        else if (value.keyword == "grab") style.cursor = HTMLComputedStyle::Cursor::Grab;
        else style.cursor = HTMLComputedStyle::Cursor::Default;
    }
    // Z-index
    else if (prop == "z-index") {
        if (value.keyword == "auto") {
            style.zIndexAuto = true;
        } else if (value.type == UltraWeb::CSSValueType::Number) {
            auto* num = value.AsNumber();
            if (num) {
                style.zIndex = static_cast<int>(num->value);
                style.zIndexAuto = false;
            }
        }
    }
    // List style
    else if (prop == "list-style-type") {
        if (value.keyword == "none") style.listStyleType = HTMLComputedStyle::ListStyleType::None;
        else if (value.keyword == "disc") style.listStyleType = HTMLComputedStyle::ListStyleType::Disc;
        else if (value.keyword == "circle") style.listStyleType = HTMLComputedStyle::ListStyleType::Circle;
        else if (value.keyword == "square") style.listStyleType = HTMLComputedStyle::ListStyleType::Square;
        else if (value.keyword == "decimal") style.listStyleType = HTMLComputedStyle::ListStyleType::Decimal;
        else if (value.keyword == "lower-alpha") style.listStyleType = HTMLComputedStyle::ListStyleType::LowerAlpha;
        else if (value.keyword == "upper-alpha") style.listStyleType = HTMLComputedStyle::ListStyleType::UpperAlpha;
    }
}

float HTMLStyleResolver::ParseLength(const UltraWeb::CSSValue& value, 
                                     float parentSize, float rootFontSize) {
    if (value.type == UltraWeb::CSSValueType::Length) {
        auto* length = value.AsLength();
        if (!length) return 0;
        
        switch (length->unit) {
            case UltraWeb::CSSLengthUnit::Px:
                return length->value;
            case UltraWeb::CSSLengthUnit::Em:
                return length->value * parentSize;
            case UltraWeb::CSSLengthUnit::Rem:
                return length->value * rootFontSize;
            case UltraWeb::CSSLengthUnit::Percent:
                return length->value * parentSize / 100.0f;
            case UltraWeb::CSSLengthUnit::Pt:
                return length->value * 1.333f;
            case UltraWeb::CSSLengthUnit::Cm:
                return length->value * 37.8f;
            case UltraWeb::CSSLengthUnit::Mm:
                return length->value * 3.78f;
            case UltraWeb::CSSLengthUnit::In:
                return length->value * 96.0f;
            default:
                return length->value;
        }
    } else if (value.type == UltraWeb::CSSValueType::Number) {
        auto* num = value.AsNumber();
        if (num) return num->value;
    }
    return 0;
}

Color HTMLStyleResolver::ParseColor(const UltraWeb::CSSValue& value) {
    if (value.type == UltraWeb::CSSValueType::Color) {
        auto* color = value.AsColor();
        if (color) {
            return Color(color->r, color->g, color->b, color->a);
        }
    }
    // Handle transparent keyword
    if (value.keyword == "transparent") {
        return Color(0, 0, 0, 0);
    }
    return Color(0, 0, 0, 255);
}

// ============================================================================
// HTML LAYOUT ENGINE IMPLEMENTATION
// ============================================================================

HTMLLayoutEngine::HTMLLayoutEngine() {
}

void HTMLLayoutEngine::Layout(HTMLDocument* document, float vpWidth, float vpHeight) {
    viewportWidth = vpWidth;
    viewportHeight = vpHeight;
    
    if (!document || !document->documentElement) return;
    
    HTMLElement* body = document->GetBody();
    if (!body) body = document->documentElement.get();
    
    body->layoutX = 0;
    body->layoutY = 0;
    body->layoutWidth = vpWidth;
    
    LayoutElement(body, vpWidth, vpHeight);
}

void HTMLLayoutEngine::LayoutElement(HTMLElement* element, float containerWidth, float containerHeight) {
    if (!element) return;
    
    const auto& style = element->computedStyle;
    
    if (style.display == HTMLComputedStyle::Display::None) {
        element->layoutWidth = 0;
        element->layoutHeight = 0;
        return;
    }
    
    switch (style.display) {
        case HTMLComputedStyle::Display::Block:
        case HTMLComputedStyle::Display::ListItem:
        case HTMLComputedStyle::Display::Table:
            LayoutBlock(element, containerWidth);
            break;
        case HTMLComputedStyle::Display::Inline:
        case HTMLComputedStyle::Display::InlineBlock:
            LayoutInline(element, containerWidth);
            break;
        case HTMLComputedStyle::Display::Flex:
            LayoutFlex(element, containerWidth);
            break;
        case HTMLComputedStyle::Display::Contents:
            // Layout children directly
            for (const auto& child : element->GetChildren()) {
                if (child->GetNodeType() == HTMLNodeType::Element) {
                    LayoutElement(static_cast<HTMLElement*>(child.get()), containerWidth, containerHeight);
                }
            }
            break;
        default:
            LayoutBlock(element, containerWidth);
            break;
    }
}

void HTMLLayoutEngine::LayoutBlock(HTMLElement* element, float containerWidth) {
    const auto& style = element->computedStyle;
    
    // Calculate width
    float width = containerWidth;
    if (!style.widthAuto && style.width > 0) {
        width = style.width;
    }
    width -= style.marginLeft + style.marginRight;
    
    float contentWidth = width - style.paddingLeft - style.paddingRight -
                         style.borderLeftWidth - style.borderRightWidth;
    
    // Layout children
    float currentY = style.paddingTop + style.borderTopWidth;
    float lastMarginBottom = 0;
    
    for (const auto& child : element->GetChildren()) {
        if (child->GetNodeType() == HTMLNodeType::Element) {
            auto* childElem = static_cast<HTMLElement*>(child.get());
            
            if (childElem->computedStyle.display == HTMLComputedStyle::Display::None) {
                continue;
            }
            
            // Margin collapsing
            float marginTop = childElem->computedStyle.marginTop;
            float collapsedMargin = std::max(lastMarginBottom, marginTop);
            currentY += collapsedMargin - lastMarginBottom;
            
            childElem->layoutX = style.paddingLeft + style.borderLeftWidth + 
                                 childElem->computedStyle.marginLeft;
            childElem->layoutY = currentY;
            
            LayoutElement(childElem, contentWidth);
            
            currentY += childElem->layoutHeight;
            lastMarginBottom = childElem->computedStyle.marginBottom;
        } else if (child->GetNodeType() == HTMLNodeType::Text) {
            auto* textNode = static_cast<HTMLTextNode*>(child.get());
            if (!textNode->IsWhitespaceOnly()) {
                LayoutText(textNode, contentWidth, style);
                textNode->layoutX = style.paddingLeft + style.borderLeftWidth;
                textNode->layoutY = currentY;
                currentY += textNode->layoutHeight;
                lastMarginBottom = 0;
            }
        }
    }
    
    currentY += style.paddingBottom + style.borderBottomWidth;
    
    element->layoutWidth = width;
    if (style.heightAuto) {
        element->layoutHeight = std::max(currentY, style.minHeight);
        if (style.maxHeight > 0) {
            element->layoutHeight = std::min(element->layoutHeight, style.maxHeight);
        }
    } else {
        element->layoutHeight = style.height;
    }
}

void HTMLLayoutEngine::LayoutInline(HTMLElement* element, float containerWidth) {
    const auto& style = element->computedStyle;
    
    std::string text = element->GetInnerText();
    float textWidth = MeasureTextWidth(text, style);
    float textHeight = style.fontSize * style.lineHeight;
    
    element->layoutWidth = textWidth + style.paddingLeft + style.paddingRight +
                          style.borderLeftWidth + style.borderRightWidth;
    element->layoutHeight = textHeight + style.paddingTop + style.paddingBottom +
                           style.borderTopWidth + style.borderBottomWidth;
}

void HTMLLayoutEngine::LayoutFlex(HTMLElement* element, float containerWidth) {
    const auto& style = element->computedStyle;
    
    float availableWidth = containerWidth - style.paddingLeft - style.paddingRight -
                           style.borderLeftWidth - style.borderRightWidth -
                           style.marginLeft - style.marginRight;
    
    std::vector<HTMLElement*> flexItems;
    float totalFlexGrow = 0;
    float totalFixedSize = 0;
    
    // Collect flex items
    for (const auto& child : element->GetChildren()) {
        if (child->GetNodeType() == HTMLNodeType::Element) {
            auto* childElem = static_cast<HTMLElement*>(child.get());
            if (childElem->computedStyle.display != HTMLComputedStyle::Display::None) {
                flexItems.push_back(childElem);
                totalFlexGrow += childElem->computedStyle.flexGrow;
                
                if (childElem->computedStyle.flexGrow == 0) {
                    if (!childElem->computedStyle.widthAuto && 
                        style.flexDirection == HTMLComputedStyle::FlexDirection::Row) {
                        totalFixedSize += childElem->computedStyle.width;
                    } else if (!childElem->computedStyle.heightAuto && 
                               style.flexDirection == HTMLComputedStyle::FlexDirection::Column) {
                        totalFixedSize += childElem->computedStyle.height;
                    }
                }
            }
        }
    }
    
    float gapTotal = style.gap * (flexItems.empty() ? 0 : flexItems.size() - 1);
    float remainingSpace = availableWidth - totalFixedSize - gapTotal;
    
    bool isColumn = style.flexDirection == HTMLComputedStyle::FlexDirection::Column ||
                    style.flexDirection == HTMLComputedStyle::FlexDirection::ColumnReverse;
    
    float currentPos = isColumn ? (style.paddingTop + style.borderTopWidth) :
                                  (style.paddingLeft + style.borderLeftWidth);
    float maxCrossSize = 0;
    
    for (auto* item : flexItems) {
        if (isColumn) {
            item->layoutX = style.paddingLeft + style.borderLeftWidth + item->computedStyle.marginLeft;
            item->layoutY = currentPos + item->computedStyle.marginTop;
            
            float itemWidth = availableWidth - item->computedStyle.marginLeft - item->computedStyle.marginRight;
            LayoutElement(item, itemWidth);
            
            currentPos += item->layoutHeight + item->computedStyle.marginTop + 
                         item->computedStyle.marginBottom + style.gap;
            maxCrossSize = std::max(maxCrossSize, item->layoutWidth);
        } else {
            item->layoutX = currentPos + item->computedStyle.marginLeft;
            item->layoutY = style.paddingTop + style.borderTopWidth + item->computedStyle.marginTop;
            
            float itemWidth;
            if (item->computedStyle.flexGrow > 0 && totalFlexGrow > 0) {
                itemWidth = (item->computedStyle.flexGrow / totalFlexGrow) * remainingSpace;
            } else if (!item->computedStyle.widthAuto) {
                itemWidth = item->computedStyle.width;
            } else {
                itemWidth = 100;
            }
            
            LayoutElement(item, itemWidth);
            item->layoutWidth = itemWidth;
            
            currentPos += itemWidth + item->computedStyle.marginLeft + 
                         item->computedStyle.marginRight + style.gap;
            maxCrossSize = std::max(maxCrossSize, item->layoutHeight);
        }
    }
    
    element->layoutWidth = containerWidth;
    if (isColumn) {
        element->layoutHeight = currentPos + style.paddingBottom + style.borderBottomWidth;
    } else {
        element->layoutHeight = maxCrossSize + style.paddingTop + style.paddingBottom +
                               style.borderTopWidth + style.borderBottomWidth;
    }
}

void HTMLLayoutEngine::LayoutText(HTMLTextNode* text, float containerWidth, 
                                  const HTMLComputedStyle& style) {
    std::string content = text->GetTrimmedText();
    
    float textWidth = MeasureTextWidth(content, style);
    text->layoutWidth = std::min(textWidth, containerWidth);
    text->layoutHeight = MeasureTextHeight(content, containerWidth, style);
}

float HTMLLayoutEngine::MeasureTextWidth(const std::string& text, const HTMLComputedStyle& style) {
    // Approximate: average character width is about 0.5 * fontSize
    float avgCharWidth = style.fontSize * 0.5f;
    
    // Adjust for font weight (bold is slightly wider)
    if (style.fontWeight >= 600) {
        avgCharWidth *= 1.05f;
    }
    
    // Adjust for monospace fonts
    if (style.fontFamily.find("mono") != std::string::npos ||
        style.fontFamily.find("courier") != std::string::npos) {
        avgCharWidth = style.fontSize * 0.6f;
    }
    
    return text.length() * avgCharWidth;
}

float HTMLLayoutEngine::MeasureTextHeight(const std::string& text, float maxWidth, 
                                          const HTMLComputedStyle& style) {
    float textWidth = MeasureTextWidth(text, style);
    int lines = static_cast<int>(std::ceil(textWidth / maxWidth));
    if (lines < 1) lines = 1;
    
    return lines * style.fontSize * style.lineHeight;
}

// ============================================================================
// CONVERSION OPTIONS FACTORY METHODS
// ============================================================================

HTMLConversionOptions HTMLConversionOptions::EBookMode() {
    HTMLConversionOptions options;
    options.viewportWidth = 600;
    options.viewportHeight = 800;
    options.enableImages = true;
    options.enableLinks = true;
    options.enableForms = false;
    options.enableScripts = false;
    options.reflowText = true;
    options.lineHeight = 1.6f;
    options.defaultFontSize = 18.0f;
    options.defaultFontFamily = "Georgia, serif";
    return options;
}

HTMLConversionOptions HTMLConversionOptions::BrowserMode() {
    HTMLConversionOptions options;
    options.viewportWidth = 1200;
    options.viewportHeight = 800;
    options.enableImages = true;
    options.enableLinks = true;
    options.enableForms = true;
    options.enableScripts = false;
    options.reflowText = false;
    options.lineHeight = 1.4f;
    options.defaultFontSize = 16.0f;
    options.defaultFontFamily = "Arial, sans-serif";
    return options;
}

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::shared_ptr<UltraCanvasContainer> ConvertHTMLToElements(
    const std::string& html,
    const HTMLConversionOptions& options) {
    auto converter = std::make_shared<HTMLConverter>();
    return converter->Convert(html, options);
}

std::shared_ptr<UltraCanvasContainer> ConvertXHTMLToElements(
    const std::string& xhtml,
    const HTMLConversionOptions& options) {
    auto converter = std::make_shared<HTMLConverter>();
    return converter->ConvertXHTML(xhtml, options);
}

std::string ExtractTextFromHTML(const std::string& html) {
    auto converter = std::make_shared<HTMLConverter>();
    return converter->ExtractText(html);
}

} // namespace UltraCanvas
