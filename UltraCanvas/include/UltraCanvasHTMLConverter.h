// include/UltraCanvasHTMLConverter.h
// HTML/XHTML to UltraCanvas Element Converter
// Bridges standard HTML to UltraCanvas UI elements
// Reuses UltraWeb CSS parser for styling
// Shared between eBook viewer (EPUB) and web browser
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"

// Include UltraWeb CSS parser for style processing
#include "UltraWebCSSParser.h"
#include "UltraWebFormats.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <stack>
#include <optional>

namespace UltraCanvas {

// ===== FORWARD DECLARATIONS =====
class HTMLNode;
class HTMLElement;
class HTMLTextNode;
class HTMLDocument;
class HTMLConverter;

// ===== HTML NODE TYPES =====
enum class HTMLNodeType {
    Element,
    Text,
    Comment,
    Document,
    DocType,
    CData
};

// ===== HTML TAG TYPES =====
enum class HTMLTagType {
    // Document
    Html, Head, Body, Title, Meta, Link, Style, Script, Base,
    
    // Sections  
    Article, Section, Nav, Aside, Header, Footer, Main, Address,
    Div, Span,
    
    // Headings
    H1, H2, H3, H4, H5, H6,
    
    // Text content
    P, Pre, Blockquote, Hr, Br, Wbr,
    
    // Text formatting
    A, Em, Strong, Small, S, Cite, Q, Code, Var, Samp, Kbd,
    Sub, Sup, I, B, U, Mark, Del, Ins,
    
    // Lists
    Ul, Ol, Li, Dl, Dt, Dd,
    
    // Tables
    Table, Caption, Thead, Tbody, Tfoot, Tr, Th, Td, Col, Colgroup,
    
    // Forms
    Form, Label, Input, Button, Select, Option, Optgroup,
    Textarea, Fieldset, Legend, Datalist, Output,
    
    // Media
    Img, Picture, Source, Figure, Figcaption,
    Video, Audio, Track, Iframe, Embed, Object, Param,
    Canvas, Svg,
    
    // Interactive
    Details, Summary, Dialog, Menu,
    
    // Special
    Noscript, Template, Slot,
    
    // Unknown/Custom
    Custom,
    Unknown
};

// ===== HTML ATTRIBUTE =====
struct HTMLAttribute {
    std::string name;
    std::string value;
    
    HTMLAttribute() = default;
    HTMLAttribute(const std::string& n, const std::string& v) : name(n), value(v) {}
};

// ===== COMPUTED HTML STYLE =====
// Computed style values after CSS cascade
struct HTMLComputedStyle {
    // Display
    enum class Display { 
        Block, Inline, InlineBlock, Flex, Grid, Table, 
        TableRow, TableCell, ListItem, None, Contents 
    };
    Display display = Display::Block;
    
    enum class Position { Static, Relative, Absolute, Fixed, Sticky };
    Position position = Position::Static;
    
    // Box model (in pixels)
    float width = 0;
    float height = 0;
    float minWidth = 0;
    float minHeight = 0;
    float maxWidth = -1;    // -1 = none
    float maxHeight = -1;
    bool widthAuto = true;
    bool heightAuto = true;
    
    float marginTop = 0;
    float marginRight = 0;
    float marginBottom = 0;
    float marginLeft = 0;
    
    float paddingTop = 0;
    float paddingRight = 0;
    float paddingBottom = 0;
    float paddingLeft = 0;
    
    float borderTopWidth = 0;
    float borderRightWidth = 0;
    float borderBottomWidth = 0;
    float borderLeftWidth = 0;
    
    // Colors
    Color color = Color(0, 0, 0, 255);
    Color backgroundColor = Color(255, 255, 255, 0);  // Transparent
    Color borderColor = Color(0, 0, 0, 255);
    
    // Typography
    std::string fontFamily = "sans-serif";
    float fontSize = 16.0f;
    int fontWeight = 400;
    bool fontItalic = false;
    float lineHeight = 1.2f;
    
    enum class TextAlign { Left, Right, Center, Justify };
    TextAlign textAlign = TextAlign::Left;
    
    enum class TextDecoration { None, Underline, Overline, LineThrough };
    TextDecoration textDecoration = TextDecoration::None;
    
    // Visibility
    float opacity = 1.0f;
    bool visible = true;
    
    // Flexbox
    enum class FlexDirection { Row, RowReverse, Column, ColumnReverse };
    FlexDirection flexDirection = FlexDirection::Row;
    
    enum class JustifyContent { FlexStart, FlexEnd, Center, SpaceBetween, SpaceAround, SpaceEvenly };
    JustifyContent justifyContent = JustifyContent::FlexStart;
    
    enum class AlignItems { FlexStart, FlexEnd, Center, Stretch, Baseline };
    AlignItems alignItems = AlignItems::Stretch;
    
    float flexGrow = 0;
    float flexShrink = 1;
    float flexBasis = -1;   // -1 = auto
    
    float gap = 0;
    
    // Border radius
    float borderRadius = 0;
    float borderTopLeftRadius = 0;
    float borderTopRightRadius = 0;
    float borderBottomLeftRadius = 0;
    float borderBottomRightRadius = 0;
    
    // List
    enum class ListStyleType { None, Disc, Circle, Square, Decimal, LowerAlpha, UpperAlpha };
    ListStyleType listStyleType = ListStyleType::Disc;
    
    // Overflow
    enum class Overflow { Visible, Hidden, Scroll, Auto };
    Overflow overflowX = Overflow::Visible;
    Overflow overflowY = Overflow::Visible;
    
    // Cursor
    enum class Cursor { Default, Pointer, Text, Move, NotAllowed, Grab };
    Cursor cursor = Cursor::Default;
    
    // Z-index
    int zIndex = 0;
    bool zIndexAuto = true;
    
    // Helper methods
    bool IsBlockLevel() const {
        return display == Display::Block || display == Display::Flex ||
               display == Display::Grid || display == Display::Table ||
               display == Display::ListItem;
    }
    
    bool IsInlineLevel() const {
        return display == Display::Inline || display == Display::InlineBlock;
    }
    
    bool GeneratesBox() const {
        return display != Display::None && display != Display::Contents;
    }
};

// ===== HTML NODE BASE CLASS =====
class HTMLNode : public std::enable_shared_from_this<HTMLNode> {
public:
    virtual ~HTMLNode() = default;
    
    // Node type
    virtual HTMLNodeType GetNodeType() const = 0;
    
    // Text content
    virtual std::string GetTextContent() const;
    
    // Tree structure
    HTMLNode* GetParent() const { return parent; }
    const std::vector<std::shared_ptr<HTMLNode>>& GetChildren() const { return children; }
    
    void AppendChild(std::shared_ptr<HTMLNode> child);
    void RemoveChild(HTMLNode* child);
    void InsertBefore(std::shared_ptr<HTMLNode> newChild, HTMLNode* refChild);
    
    HTMLNode* GetFirstChild() const { return children.empty() ? nullptr : children.front().get(); }
    HTMLNode* GetLastChild() const { return children.empty() ? nullptr : children.back().get(); }
    HTMLNode* GetNextSibling() const;
    HTMLNode* GetPreviousSibling() const;
    
    // Computed style (set during styling phase)
    HTMLComputedStyle computedStyle;
    
    // Layout position (set during layout phase)
    float layoutX = 0;
    float layoutY = 0;
    float layoutWidth = 0;
    float layoutHeight = 0;
    
protected:
    HTMLNode* parent = nullptr;
    std::vector<std::shared_ptr<HTMLNode>> children;
};

// ===== HTML TEXT NODE =====
class HTMLTextNode : public HTMLNode {
public:
    HTMLTextNode(const std::string& text = "") : text(text) {}
    
    HTMLNodeType GetNodeType() const override { return HTMLNodeType::Text; }
    std::string GetTextContent() const override { return text; }
    
    const std::string& GetText() const { return text; }
    void SetText(const std::string& t) { text = t; }
    
    bool IsWhitespaceOnly() const;
    std::string GetTrimmedText() const;
    
private:
    std::string text;
};

// ===== HTML ELEMENT =====
class HTMLElement : public HTMLNode {
public:
    HTMLElement(const std::string& tagName);
    HTMLElement(HTMLTagType type);
    
    HTMLNodeType GetNodeType() const override { return HTMLNodeType::Element; }
    
    // Tag information
    HTMLTagType GetTagType() const { return tagType; }
    const std::string& GetTagName() const { return tagName; }
    
    // Attributes
    bool HasAttribute(const std::string& name) const;
    std::string GetAttribute(const std::string& name) const;
    void SetAttribute(const std::string& name, const std::string& value);
    void RemoveAttribute(const std::string& name);
    const std::vector<HTMLAttribute>& GetAttributes() const { return attributes; }
    
    // Common attribute shortcuts
    std::string GetId() const { return GetAttribute("id"); }
    void SetId(const std::string& id) { SetAttribute("id", id); }
    
    std::string GetClassName() const { return GetAttribute("class"); }
    void SetClassName(const std::string& cls) { SetAttribute("class", cls); }
    std::vector<std::string> GetClassList() const;
    bool HasClass(const std::string& className) const;
    
    std::string GetStyle() const { return GetAttribute("style"); }
    void SetStyle(const std::string& style) { SetAttribute("style", style); }
    
    // Element queries
    HTMLElement* GetElementById(const std::string& id);
    std::vector<HTMLElement*> GetElementsByTagName(const std::string& tagName);
    std::vector<HTMLElement*> GetElementsByClassName(const std::string& className);
    
    // Element type checks
    bool IsVoidElement() const;     // <br>, <img>, <input>, etc.
    bool IsBlockElement() const;
    bool IsInlineElement() const;
    bool IsFormElement() const;
    bool IsMediaElement() const;
    bool IsHeading() const;
    bool IsList() const;
    bool IsTableElement() const;
    
    // Link/Image helpers
    bool IsLink() const { return tagType == HTMLTagType::A && HasAttribute("href"); }
    std::string GetHref() const { return GetAttribute("href"); }
    bool IsImage() const { return tagType == HTMLTagType::Img; }
    std::string GetSrc() const { return GetAttribute("src"); }
    std::string GetAlt() const { return GetAttribute("alt"); }
    
    // Inner content
    std::string GetInnerHTML() const;
    std::string GetInnerText() const;
    std::string GetOuterHTML() const;
    
    // Convert tag name to type
    static HTMLTagType TagNameToType(const std::string& tagName);
    static std::string TypeToTagName(HTMLTagType type);
    
private:
    HTMLTagType tagType;
    std::string tagName;
    std::vector<HTMLAttribute> attributes;
    
    void CollectElementsById(const std::string& id, std::vector<HTMLElement*>& results);
    void CollectElementsByTagName(const std::string& tagName, std::vector<HTMLElement*>& results);
    void CollectElementsByClassName(const std::string& className, std::vector<HTMLElement*>& results);
};

// ===== HTML DOCUMENT =====
class HTMLDocument {
public:
    HTMLDocument();
    
    // Document info
    std::string title;
    std::string baseUrl;
    std::string charset = "UTF-8";
    std::string language;
    
    // Root element
    std::shared_ptr<HTMLElement> documentElement;  // <html>
    HTMLElement* GetHead() const;
    HTMLElement* GetBody() const;
    
    // Element queries
    HTMLElement* GetElementById(const std::string& id);
    std::vector<HTMLElement*> GetElementsByTagName(const std::string& tagName);
    std::vector<HTMLElement*> GetElementsByClassName(const std::string& className);
    
    // Stylesheets (extracted from <style> and <link>)
    std::vector<std::string> stylesheets;
    
    // Scripts (extracted from <script>)
    std::vector<std::string> scripts;
    
    // Meta information
    std::unordered_map<std::string, std::string> meta;
    
    // Statistics
    int GetElementCount() const;
    int GetTextNodeCount() const;
};

// ===== HTML PARSER =====
class HTMLParser {
public:
    HTMLParser();
    
    // Parse HTML string
    std::shared_ptr<HTMLDocument> Parse(const std::string& html);
    
    // Parse XHTML (stricter mode)
    std::shared_ptr<HTMLDocument> ParseXHTML(const std::string& xhtml);
    
    // Parse HTML fragment (no <html>, <head>, <body>)
    std::shared_ptr<HTMLElement> ParseFragment(const std::string& html);
    
    // Error handling
    const std::vector<std::string>& GetErrors() const { return errors; }
    bool HasErrors() const { return !errors.empty(); }
    
    // Configuration
    void SetStrictMode(bool strict) { strictMode = strict; }
    void SetPreserveWhitespace(bool preserve) { preserveWhitespace = preserve; }
    
private:
    std::vector<std::string> errors;
    bool strictMode = false;
    bool preserveWhitespace = false;
    
    // Parser state
    std::string input;
    size_t pos = 0;
    size_t line = 1;
    size_t column = 1;
    
    // Parsing methods
    void Reset(const std::string& html);
    char Current() const;
    char Peek(int offset = 1) const;
    void Advance();
    void SkipWhitespace();
    bool Match(const std::string& str);
    bool MatchInsensitive(const std::string& str);
    
    std::shared_ptr<HTMLElement> ParseElement();
    std::shared_ptr<HTMLTextNode> ParseText();
    void ParseComment();
    void ParseDoctype();
    std::string ParseTagName();
    std::vector<HTMLAttribute> ParseAttributes();
    std::string ParseAttributeValue();
    std::string ParseQuotedString(char quote);
    
    void Error(const std::string& msg);
    bool IsAtEnd() const { return pos >= input.length(); }
};

// ===== HTML STYLE RESOLVER =====
// Applies CSS styles to HTML elements using UltraWeb CSS parser
class HTMLStyleResolver {
public:
    HTMLStyleResolver();
    
    // Add stylesheet
    void AddStylesheet(const std::string& css);
    void AddInlineStyle(HTMLElement* element, const std::string& style);
    
    // Clear all styles
    void ClearStyles();
    
    // Resolve styles for entire document
    void ResolveStyles(HTMLDocument* document);
    
    // Resolve style for single element
    void ResolveElementStyle(HTMLElement* element, const HTMLComputedStyle* parentStyle);
    
    // Default styles
    HTMLComputedStyle GetDefaultStyle(HTMLTagType type);
    
private:
    std::vector<UltraWeb::CSSStylesheet> stylesheets;
    
    // Apply matching rules
    void ApplyMatchingRules(HTMLElement* element, HTMLComputedStyle& style);
    
    // Check if selector matches element
    bool SelectorMatches(const UltraWeb::CSSSelector& selector, const HTMLElement* element);
    
    // Apply CSS declaration to computed style
    void ApplyDeclaration(const UltraWeb::CSSDeclaration& decl, HTMLComputedStyle& style);
    
    // Parse CSS values
    float ParseLength(const UltraWeb::CSSValue& value, float parentSize, float rootFontSize);
    Color ParseColor(const UltraWeb::CSSValue& value);
    
    // Inheritance
    void InheritStyle(HTMLComputedStyle& style, const HTMLComputedStyle* parentStyle);
};

// ===== HTML LAYOUT ENGINE =====
// Simple layout engine for HTML→UltraCanvas conversion
class HTMLLayoutEngine {
public:
    HTMLLayoutEngine();
    
    // Perform layout
    void Layout(HTMLDocument* document, float viewportWidth, float viewportHeight);
    
    // Layout single element
    void LayoutElement(HTMLElement* element, float containerWidth, float containerHeight);
    
private:
    float viewportWidth = 800;
    float viewportHeight = 600;
    
    void LayoutBlock(HTMLElement* element, float containerWidth);
    void LayoutInline(HTMLElement* element, float containerWidth);
    void LayoutFlex(HTMLElement* element, float containerWidth);
    void LayoutText(HTMLTextNode* text, float containerWidth, const HTMLComputedStyle& style);
    
    float MeasureTextWidth(const std::string& text, const HTMLComputedStyle& style);
    float MeasureTextHeight(const std::string& text, float maxWidth, const HTMLComputedStyle& style);
};

// ===== CONVERSION OPTIONS =====
struct HTMLConversionOptions {
    // Viewport
    float viewportWidth = 800;
    float viewportHeight = 600;
    
    // Base URL for resolving relative links
    std::string baseUrl;
    
    // Default styling
    float defaultFontSize = 16.0f;
    std::string defaultFontFamily = "sans-serif";
    Color defaultTextColor = Color(0, 0, 0, 255);
    Color defaultBackgroundColor = Color(255, 255, 255, 255);
    Color defaultLinkColor = Color(0, 0, 238, 255);
    
    // Feature flags
    bool enableImages = true;
    bool enableLinks = true;
    bool enableForms = false;       // Disabled for eBook
    bool enableScripts = false;     // Disabled for eBook
    bool preserveWhitespace = false;
    
    // eBook-specific
    bool reflowText = false;
    float lineHeight = 1.5f;
    
    // Callbacks
    std::function<void(const std::string& url)> onLinkClick;
    std::function<bool(const std::string& src, std::vector<uint8_t>& data, int& w, int& h)> imageLoader;
    std::function<void(const std::string& error)> onError;
    
    static HTMLConversionOptions Default() { return HTMLConversionOptions(); }
    static HTMLConversionOptions EBookMode();
    static HTMLConversionOptions BrowserMode();
};

// ===== MAIN HTML CONVERTER =====
// Converts HTML document to UltraCanvas element tree
class HTMLConverter {
public:
    HTMLConverter();
    ~HTMLConverter();
    
    // ===== MAIN CONVERSION API =====
    
    // Convert HTML string to UltraCanvas container
    std::shared_ptr<UltraCanvasContainer> Convert(
        const std::string& html,
        const HTMLConversionOptions& options = HTMLConversionOptions::Default());
    
    // Convert XHTML string (for EPUB)
    std::shared_ptr<UltraCanvasContainer> ConvertXHTML(
        const std::string& xhtml,
        const HTMLConversionOptions& options = HTMLConversionOptions::Default());
    
    // Convert parsed document
    std::shared_ptr<UltraCanvasContainer> ConvertDocument(
        HTMLDocument* document,
        const HTMLConversionOptions& options);
    
    // ===== STYLESHEET API =====
    
    // Add CSS stylesheet (before conversion)
    void AddStylesheet(const std::string& css);
    
    // Clear all stylesheets
    void ClearStylesheets();
    
    // ===== TEXT EXTRACTION =====
    
    // Extract plain text from HTML
    std::string ExtractText(const std::string& html);
    
    // Extract text from document
    std::string ExtractTextFromDocument(HTMLDocument* document);
    
    // ===== RENDERING TO IMAGE =====
    
    // Render HTML to image (RGBA pixels)
    std::vector<uint8_t> RenderToImage(
        const std::string& html,
        int width, int height,
        const HTMLConversionOptions& options = HTMLConversionOptions::Default());
    
    // ===== PAGINATION =====
    
    // Calculate page count for given dimensions
    int CalculatePageCount(
        const std::string& html,
        int pageWidth, int pageHeight,
        const HTMLConversionOptions& options);
    
    // Render specific page
    std::shared_ptr<UltraCanvasContainer> RenderPage(
        const std::string& html,
        int pageNumber,
        int pageWidth, int pageHeight,
        const HTMLConversionOptions& options);
    
    // ===== ERROR HANDLING =====
    
    const std::vector<std::string>& GetErrors() const { return errors; }
    bool HasErrors() const { return !errors.empty(); }
    void ClearErrors() { errors.clear(); }
    
    // ===== CALLBACKS =====
    
    std::function<void(const std::string& url, const std::string& target)> onLinkClick;
    std::function<void(const std::string& src, bool success)> onImageLoad;
    std::function<void(const std::string& error)> onError;
    
private:
    std::unique_ptr<HTMLParser> parser;
    std::unique_ptr<HTMLStyleResolver> styleResolver;
    std::unique_ptr<HTMLLayoutEngine> layoutEngine;
    std::vector<std::string> errors;
    
    // Element ID counter
    long nextElementId = 70000;
    
    // Conversion methods
    std::shared_ptr<UltraCanvasUIElement> ConvertElement(
        HTMLElement* element,
        const HTMLConversionOptions& options);
    
    std::shared_ptr<UltraCanvasUIElement> ConvertTextNode(
        HTMLTextNode* text,
        const HTMLComputedStyle& style,
        const HTMLConversionOptions& options);
    
    // Specific element converters
    std::shared_ptr<UltraCanvasContainer> ConvertDiv(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasLabel> ConvertHeading(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasLabel> ConvertParagraph(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasLabel> ConvertSpan(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasButton> ConvertLink(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasImageElement> ConvertImage(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasContainer> ConvertList(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasContainer> ConvertTable(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasButton> ConvertButton(HTMLElement* element, const HTMLConversionOptions& options);
    std::shared_ptr<UltraCanvasTextInput> ConvertInput(HTMLElement* element, const HTMLConversionOptions& options);
    
    // Style application
    void ApplyStylesToElement(UltraCanvasUIElement* element, const HTMLComputedStyle& style);
    
    // Helper methods
    long GetNextElementId() { return nextElementId++; }
    void EmitError(const std::string& error);
};

// ===== FACTORY FUNCTIONS =====

// Create HTML converter
inline std::shared_ptr<HTMLConverter> CreateHTMLConverter() {
    return std::make_shared<HTMLConverter>();
}

// Quick conversion functions
std::shared_ptr<UltraCanvasContainer> ConvertHTMLToElements(
    const std::string& html,
    const HTMLConversionOptions& options = HTMLConversionOptions::Default());

std::shared_ptr<UltraCanvasContainer> ConvertXHTMLToElements(
    const std::string& xhtml,
    const HTMLConversionOptions& options = HTMLConversionOptions::Default());

std::string ExtractTextFromHTML(const std::string& html);

std::vector<uint8_t> RenderHTMLToImage(
    const std::string& html,
    int width, int height,
    const HTMLConversionOptions& options = HTMLConversionOptions::Default());

} // namespace UltraCanvas

/*
=== HTML CONVERTER FEATURES ===

✅ **Shared Technology for eBook + Browser**:
- Single HTML parser for both use cases
- Reuses UltraWeb's CSS parser (CSSTokenizer, CSSParser)
- Common style resolution and layout
- Converts to native UltraCanvas elements

✅ **HTML Parsing**:
- Full HTML5 tag support
- XHTML strict mode (for EPUB)
- Error recovery for malformed HTML
- Whitespace handling options

✅ **CSS Support (via UltraWeb)**:
- CSS selector matching
- Specificity calculation
- Cascade and inheritance
- Box model properties
- Typography properties
- Flexbox layout
- Colors and backgrounds

✅ **Element Conversion**:
- <div> → UltraCanvasContainer
- <p>, <h1>-<h6>, <span> → UltraCanvasLabel
- <a> → UltraCanvasButton (clickable)
- <img> → UltraCanvasImageElement
- <ul>, <ol> → UltraCanvasContainer with list items
- <table> → UltraCanvasContainer with grid layout
- <button> → UltraCanvasButton
- <input> → UltraCanvasTextInput

✅ **Usage - eBook Viewer (EPUB)**:
```cpp
auto converter = CreateHTMLConverter();

// Add EPUB stylesheet
converter->AddStylesheet(epubCss);

// Convert EPUB chapter (XHTML)
auto options = HTMLConversionOptions::EBookMode();
options.viewportWidth = 600;
options.viewportHeight = 800;

auto container = converter->ConvertXHTML(chapterXhtml, options);
window->AddChild(container);
```

✅ **Usage - Web Browser**:
```cpp
auto converter = CreateHTMLConverter();

auto options = HTMLConversionOptions::BrowserMode();
options.baseUrl = "https://example.com";
options.onLinkClick = [](const std::string& url) {
    // Navigate to URL
};
options.imageLoader = [](const std::string& src, ...) {
    // Load image from network
};

auto container = converter->Convert(htmlContent, options);
browserView->SetContent(container);
```

✅ **Build Configuration**:
```cmake
# HTML Converter (requires UltraWeb CSS parser)
set(ULTRACANVAS_HTML_CONVERTER ON)

# Link with UltraWeb
target_link_libraries(UltraCanvas UltraWeb)
```
*/
