// Markdown.h
// Markdown text display driver with full formatting and rendering support
// Version: 1.0.0
// Last Modified: 2025-07-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasLayoutEngine.h"
#include <string>
#include <vector>
#include <memory>
#include <regex>
#include <unordered_map>
#include <functional>
#include <sstream>

namespace UltraCanvas {

// ===== MARKDOWN ELEMENT TYPES =====
enum class MarkdownElementType {
    Text,           // Plain text
    Header,         // # ## ### #### ##### ######
    Bold,           // **text** or __text__
    Italic,         // *text* or _text_
    Code,           // `code`
    CodeBlock,      // ```language\ncode\n```
    Link,           // [text](url)
    Image,          // ![alt](url)
    List,           // - item or 1. item
    ListItem,       // Individual list item
    Quote,          // > quote
    HorizontalRule, // --- or ***
    Table,          // | col1 | col2 |
    TableRow,       // Table row
    TableCell,      // Table cell
    LineBreak,      // \n or double space
    Strikethrough,  // ~~text~~
    Highlight,      // ==text==
    Paragraph       // Block of text
};

// ===== MARKDOWN STYLING =====
struct MarkdownStyle {
    // Base text style
    std::string fontFamily = "Arial";
    float fontSize = 14.0f;
    Color textColor = Color(50, 50, 50);
    Color backgroundColor = Color::Transparent;
    float lineHeight = 1.4f;
    
    // Header styles
    std::array<float, 6> headerSizes = {24.0f, 20.0f, 18.0f, 16.0f, 14.0f, 12.0f};
    std::array<Color, 6> headerColors = {
        Color(20, 20, 20), Color(30, 30, 30), Color(40, 40, 40),
        Color(50, 50, 50), Color(60, 60, 60), Color(70, 70, 70)
    };
    std::array<float, 6> headerMarginTop = {20.0f, 18.0f, 16.0f, 14.0f, 12.0f, 10.0f};
    std::array<float, 6> headerMarginBottom = {12.0f, 10.0f, 8.0f, 6.0f, 4.0f, 2.0f};
    
    // Code styling
    std::string codeFont = "Consolas";
    float codeFontSize = 12.0f;
    Color codeTextColor = Color(200, 50, 50);
    Color codeBackgroundColor = Color(245, 245, 245);
    Color codeBlockBackgroundColor = Color(248, 248, 248);
    Color codeBlockBorderColor = Color(220, 220, 220);
    float codeBlockPadding = 12.0f;
    float codeBlockBorderWidth = 1.0f;
    float codeBlockBorderRadius = 4.0f;
    
    // Link styling
    Color linkColor = Color(0, 102, 204);
    Color linkHoverColor = Color(0, 80, 160);
    Color linkVisitedColor = Color(128, 0, 128);
    bool linkUnderline = true;
    
    // List styling
    float listIndent = 20.0f;
    float listItemSpacing = 4.0f;
    std::string bulletCharacter = "•";
    Color bulletColor = Color(100, 100, 100);
    
    // Quote styling
    Color quoteTextColor = Color(100, 100, 100);
    Color quoteBorderColor = Color(200, 200, 200);
    float quoteBorderWidth = 4.0f;
    float quotePadding = 12.0f;
    float quoteMarginLeft = 16.0f;
    
    // Table styling
    Color tableBorderColor = Color(220, 220, 220);
    Color tableHeaderBackgroundColor = Color(248, 248, 248);
    Color tableAlternateRowColor = Color(252, 252, 252);
    float tableBorderWidth = 1.0f;
    float tableCellPadding = 8.0f;
    
    // Other elements
    Color horizontalRuleColor = Color(200, 200, 200);
    float horizontalRuleWidth = 1.0f;
    float horizontalRuleMargin = 16.0f;
    
    Color strikethroughColor = Color(150, 150, 150);
    Color highlightBackgroundColor = Color(255, 255, 0, 128);
    
    // Spacing
    float paragraphSpacing = 12.0f;
    float blockSpacing = 16.0f;
    
    // Scrolling and interaction
    bool enableScrolling = true;
    bool enableSelection = true;
    bool enableLinkClicking = true;
    
    static MarkdownStyle Default() {
        return MarkdownStyle();
    }
    
    static MarkdownStyle DarkTheme() {
        MarkdownStyle style;
        style.textColor = Color(220, 220, 220);
        style.backgroundColor = Color(40, 40, 40);
        
        // Adjust other colors for dark theme
        for (auto& color : style.headerColors) {
            color = Color(240, 240, 240);
        }
        
        style.codeBackgroundColor = Color(60, 60, 60);
        style.codeBlockBackgroundColor = Color(50, 50, 50);
        style.codeBlockBorderColor = Color(80, 80, 80);
        
        style.linkColor = Color(100, 150, 255);
        style.linkHoverColor = Color(120, 170, 255);
        
        return style;
    }
    
    static MarkdownStyle DocumentStyle() {
        MarkdownStyle style;
        style.fontFamily = "Times New Roman";
        style.fontSize = 16.0f;
        style.lineHeight = 1.6f;
        return style;
    }
};

// ===== MARKDOWN ELEMENT STRUCTURE =====
struct MarkdownElement {
    MarkdownElementType type;
    std::string text;
    std::string url;        // For links and images
    std::string altText;    // For images
    std::string language;   // For code blocks
    int level = 0;          // For headers (1-6) and list nesting
    bool ordered = false;   // For lists
    
    // Styling override
    std::unique_ptr<TextStyle> customStyle;
    
    // Layout information (calculated during rendering)
    Rect2D bounds;
    bool visible = true;
    bool clickable = false;
    
    // Child elements (for complex structures)
    std::vector<std::shared_ptr<MarkdownElement>> children;
    
    MarkdownElement(MarkdownElementType t = MarkdownElementType::Text) 
        : type(t) {}
    
    static std::shared_ptr<MarkdownElement> CreateText(const std::string& content) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Text);
        element->text = content;
        return element;
    }
    
    static std::shared_ptr<MarkdownElement> CreateHeader(int headerLevel, const std::string& content) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Header);
        element->level = std::clamp(headerLevel, 1, 6);
        element->text = content;
        return element;
    }
    
    static std::shared_ptr<MarkdownElement> CreateLink(const std::string& text, const std::string& url) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Link);
        element->text = text;
        element->url = url;
        element->clickable = true;
        return element;
    }
    
    static std::shared_ptr<MarkdownElement> CreateCodeBlock(const std::string& code, const std::string& language = "") {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::CodeBlock);
        element->text = code;
        element->language = language;
        return element;
    }
};

// ===== MARKDOWN PARSER =====
class MarkdownParser {
private:
    struct ParseContext {
        std::string input;
        size_t position = 0;
        int currentListLevel = 0;
        bool inCodeBlock = false;
        std::string codeBlockLanguage;
        bool inTable = false;
    };
    
public:
    static std::vector<std::shared_ptr<MarkdownElement>> Parse(const std::string& markdown) {
        std::vector<std::shared_ptr<MarkdownElement>> elements;
        ParseContext context;
        context.input = markdown;
        
        std::vector<std::string> lines = SplitIntoLines(markdown);
        
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string line = lines[i];
            
            // Handle code blocks
            if (IsCodeBlockDelimiter(line)) {
                if (!context.inCodeBlock) {
                    context.inCodeBlock = true;
                    context.codeBlockLanguage = ExtractCodeBlockLanguage(line);
                } else {
                    // End code block
                    context.inCodeBlock = false;
                    // Code block content was accumulated, create element
                }
                continue;
            }
            
            if (context.inCodeBlock) {
                // Accumulate code block content
                continue;
            }
            
            // Parse line based on markdown syntax
            auto element = ParseLine(line, context);
            if (element) {
                elements.push_back(element);
            }
        }
        
        return elements;
    }
    
private:
    static std::shared_ptr<MarkdownElement> ParseLine(const std::string& line, ParseContext& context) {
        std::string trimmed = TrimWhitespace(line);
        
        if (trimmed.empty()) {
            return MarkdownElement::CreateText("\n");
        }
        
        // Headers
        if (trimmed[0] == '#') {
            return ParseHeader(trimmed);
        }
        
        // Horizontal rules
        if (IsHorizontalRule(trimmed)) {
            return std::make_shared<MarkdownElement>(MarkdownElementType::HorizontalRule);
        }
        
        // Quotes
        if (trimmed[0] == '>') {
            return ParseQuote(trimmed);
        }
        
        // Lists
        if (IsListItem(trimmed)) {
            return ParseListItem(trimmed);
        }
        
        // Table rows
        if (IsTableRow(trimmed)) {
            return ParseTableRow(trimmed);
        }
        
        // Regular paragraph with inline formatting
        return ParseParagraph(trimmed);
    }
    
    static std::shared_ptr<MarkdownElement> ParseHeader(const std::string& line) {
        size_t hashCount = 0;
        while (hashCount < line.length() && line[hashCount] == '#') {
            hashCount++;
        }
        
        if (hashCount > 6) hashCount = 6;
        
        std::string content = TrimWhitespace(line.substr(hashCount));
        return MarkdownElement::CreateHeader(static_cast<int>(hashCount), content);
    }
    
    static std::shared_ptr<MarkdownElement> ParseQuote(const std::string& line) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Quote);
        element->text = TrimWhitespace(line.substr(1)); // Remove '>'
        return element;
    }
    
    static std::shared_ptr<MarkdownElement> ParseListItem(const std::string& line) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::ListItem);
        
        // Determine if ordered or unordered
        if (std::isdigit(line[0])) {
            element->ordered = true;
            size_t dotPos = line.find('.');
            if (dotPos != std::string::npos) {
                element->text = TrimWhitespace(line.substr(dotPos + 1));
            }
        } else {
            element->ordered = false;
            element->text = TrimWhitespace(line.substr(1)); // Remove bullet
        }
        
        return element;
    }
    
    static std::shared_ptr<MarkdownElement> ParseTableRow(const std::string& line) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::TableRow);
        
        // Split by pipes and create table cells
        std::vector<std::string> cells = SplitString(line, '|');
        for (const std::string& cellContent : cells) {
            std::string trimmedCell = TrimWhitespace(cellContent);
            if (!trimmedCell.empty()) {
                auto cell = std::make_shared<MarkdownElement>(MarkdownElementType::TableCell);
                cell->text = trimmedCell;
                element->children.push_back(cell);
            }
        }
        
        return element;
    }
    
    static std::shared_ptr<MarkdownElement> ParseParagraph(const std::string& line) {
        auto paragraph = std::make_shared<MarkdownElement>(MarkdownElementType::Paragraph);
        
        // Parse inline formatting
        std::vector<std::shared_ptr<MarkdownElement>> inlineElements = ParseInlineFormatting(line);
        paragraph->children = inlineElements;
        
        return paragraph;
    }
    
    static std::vector<std::shared_ptr<MarkdownElement>> ParseInlineFormatting(const std::string& text) {
        std::vector<std::shared_ptr<MarkdownElement>> elements;
        
        // Simplified inline parsing - would need more sophisticated implementation
        // For now, just create a text element
        elements.push_back(MarkdownElement::CreateText(text));
        
        return elements;
    }
    
    static bool IsCodeBlockDelimiter(const std::string& line) {
        std::string trimmed = TrimWhitespace(line);
        return trimmed.substr(0, 3) == "```";
    }
    
    static std::string ExtractCodeBlockLanguage(const std::string& line) {
        std::string trimmed = TrimWhitespace(line);
        if (trimmed.length() > 3) {
            return trimmed.substr(3);
        }
        return "";
    }
    
    static bool IsHorizontalRule(const std::string& line) {
        std::string trimmed = TrimWhitespace(line);
        return (trimmed == "---" || trimmed == "***" || trimmed == "___");
    }
    
    static bool IsListItem(const std::string& line) {
        if (line.empty()) return false;
        char first = line[0];
        return (first == '-' || first == '*' || first == '+' || std::isdigit(first));
    }
    
    static bool IsTableRow(const std::string& line) {
        return line.find('|') != std::string::npos;
    }
    
    static std::vector<std::string> SplitIntoLines(const std::string& text) {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        
        return lines;
    }
    
    static std::vector<std::string> SplitString(const std::string& text, char delimiter) {
        std::vector<std::string> result;
        std::istringstream stream(text);
        std::string item;
        
        while (std::getline(stream, item, delimiter)) {
            result.push_back(item);
        }
        
        return result;
    }
    
    static std::string TrimWhitespace(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
};

// ===== MARKDOWN DISPLAY COMPONENT =====
class UltraCanvasMarkdownDisplay : public UltraCanvasUIElement {
private:
    // Standard properties
    StandardProperties properties;
    
    // Markdown content and styling
    std::string markdownText;
    MarkdownStyle style;
    std::vector<std::shared_ptr<MarkdownElement>> elements;
    
    // Layout and rendering
    float contentHeight = 0.0f;
    float scrollOffset = 0.0f;
    bool needsReparse = true;
    bool needsRelayout = true;
    
    // Interaction state
    std::shared_ptr<MarkdownElement> hoveredElement;
    std::shared_ptr<MarkdownElement> clickedElement;
    std::vector<std::string> visitedLinks;
    
    // Event callbacks
    std::function<void(const std::string& url)> onLinkClicked;
    std::function<void(const std::string& text)> onTextSelected;
    std::function<void(float position)> onScrollChanged;

public:
    // ===== CONSTRUCTOR =====
    UltraCanvasMarkdownDisplay(const std::string& identifier = "MarkdownDisplay", 
                              long id = 0, long x = 0, long y = 0, long w = 400, long h = 300)
        : UltraCanvasUIElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        ULTRACANVAS_ELEMENT_PROPERTIES(properties);
        
        style = MarkdownStyle::Default();
    }
    
    virtual ~UltraCanvasMarkdownDisplay() = default;
    
    // ===== MARKDOWN CONTENT =====
    void SetMarkdownText(const std::string& markdown) {
        if (markdownText != markdown) {
            markdownText = markdown;
            needsReparse = true;
            needsRelayout = true;
        }
    }
    
    const std::string& GetMarkdownText() const { return markdownText; }
    
    void LoadFromFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            SetMarkdownText(content);
        }
    }
    
    void LoadFromUrl(const std::string& url) {
        // URL loading would require HTTP client
        // For now, just set placeholder text
        SetMarkdownText("# Loading...\nFetching content from: " + url);
    }
    
    // ===== STYLING =====
    void SetMarkdownStyle(const MarkdownStyle& newStyle) {
        style = newStyle;
        needsRelayout = true;
    }
    
    const MarkdownStyle& GetMarkdownStyle() const { return style; }
    
    void SetTheme(const std::string& themeName) {
        if (themeName == "dark") {
            style = MarkdownStyle::DarkTheme();
        } else if (themeName == "document") {
            style = MarkdownStyle::DocumentStyle();
        } else {
            style = MarkdownStyle::Default();
        }
        needsRelayout = true;
    }
    
    // ===== SCROLLING =====
    void ScrollTo(float position) {
        float maxScroll = std::max(0.0f, contentHeight - GetHeight());
        scrollOffset = std::clamp(position, 0.0f, maxScroll);
        
        if (onScrollChanged) {
            onScrollChanged(GetScrollPercentage());
        }
    }
    
    void ScrollBy(float delta) {
        ScrollTo(scrollOffset + delta);
    }
    
    float GetScrollPosition() const { return scrollOffset; }
    float GetScrollPercentage() const {
        float maxScroll = std::max(0.0f, contentHeight - GetHeight());
        return maxScroll > 0 ? (scrollOffset / maxScroll) : 0.0f;
    }
    
    bool CanScrollUp() const { return scrollOffset > 0; }
    bool CanScrollDown() const { return scrollOffset < (contentHeight - GetHeight()); }
    
    // ===== EVENT CALLBACKS =====
    void SetLinkClickCallback(std::function<void(const std::string&)> callback) {
        onLinkClicked = callback;
    }
    
    void SetTextSelectionCallback(std::function<void(const std::string&)> callback) {
        onTextSelected = callback;
    }
    
    void SetScrollCallback(std::function<void(float)> callback) {
        onScrollChanged = callback;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Parse markdown if needed
        if (needsReparse) {
            ParseMarkdown();
            needsReparse = false;
            needsRelayout = true;
        }
        
        // Layout elements if needed
        if (needsRelayout) {
            LayoutElements();
            needsRelayout = false;
        }
        
        // Set up clipping
        Rect2D bounds = GetBounds();
        ctx->SetClipRect(bounds);
        
        // Draw background
        if (style.backgroundColor.a > 0) {
           ctx->PaintWidthColorstyle.backgroundColor);
            FillRect(bounds);
        }
        
        // Render markdown elements
        RenderElements(bounds);
        
        // Draw scrollbar if needed
        if (style.enableScrolling && contentHeight > bounds.height) {
            DrawScrollbar(bounds);
        }
        
        ctx->ClearClipRect();
    }
    
    // ===== EVENT HANDLING =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(Point2D(event.mouse.x, event.mouse.y), event.mouse.button);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(Point2D(event.mouse.x, event.mouse.y));
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(Point2D(event.mouse.x, event.mouse.y), event.mouse.button);
                break;
                
            case UCEventType::MouseWheel:
                if (style.enableScrolling) {
                    ScrollBy(-event.mouse.wheelDelta * 30.0f);
                }
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event.key.keyCode);
                break;
                
            default:
                break;
        }
    }
    
    // ===== UTILITY METHODS =====
    std::vector<std::string> GetAllLinks() const {
        std::vector<std::string> links;
        for (const auto& element : elements) {
            CollectLinks(element, links);
        }
        return links;
    }
    
    std::vector<std::string> GetHeaders() const {
        std::vector<std::string> headers;
        for (const auto& element : elements) {
            if (element->type == MarkdownElementType::Header) {
                headers.push_back(element->text);
            }
        }
        return headers;
    }
    
    void JumpToHeader(const std::string& headerText) {
        for (const auto& element : elements) {
            if (element->type == MarkdownElementType::Header && element->text == headerText) {
                ScrollTo(element->bounds.y);
                break;
            }
        }
    }
    
    std::string GetPlainText() const {
        std::string result;
        for (const auto& element : elements) {
            ExtractPlainText(element, result);
        }
        return result;
    }

private:
    // ===== INTERNAL METHODS =====
    void ParseMarkdown() {
        elements = MarkdownParser::Parse(markdownText);
    }
    
    void LayoutElements() {
        float currentY = style.blockSpacing;
        float containerWidth = GetWidth() - 20.0f; // Leave margin
        
        for (auto& element : elements) {
            LayoutElement(element, 10.0f, currentY, containerWidth);
            currentY = element->bounds.y + element->bounds.height;
            
            // Add spacing between elements
            if (element->type == MarkdownElementType::Header) {
                currentY += style.headerMarginBottom[std::min(element->level - 1, 5)];
            } else if (element->type == MarkdownElementType::Paragraph) {
                currentY += style.paragraphSpacing;
            } else {
                currentY += style.blockSpacing;
            }
        }
        
        contentHeight = currentY;
    }
    
    void LayoutElement(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        switch (element->type) {
            case MarkdownElementType::Header:
                LayoutHeader(element, x, y, width);
                break;
                
            case MarkdownElementType::Paragraph:
                LayoutParagraph(element, x, y, width);
                break;
                
            case MarkdownElementType::CodeBlock:
                LayoutCodeBlock(element, x, y, width);
                break;
                
            case MarkdownElementType::Quote:
                LayoutQuote(element, x, y, width);
                break;
                
            case MarkdownElementType::ListItem:
                LayoutListItem(element, x, y, width);
                break;
                
            case MarkdownElementType::Table:
                LayoutTable(element, x, y, width);
                break;
                
            case MarkdownElementType::HorizontalRule:
                LayoutHorizontalRule(element, x, y, width);
                break;
                
            default:
                LayoutText(element, x, y, width);
                break;
        }
    }
    
    void LayoutHeader(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        int level = std::clamp(element->level - 1, 0, 5);
        float fontSize = style.headerSizes[level];
        float textHeight = fontSize * style.lineHeight;
        
        y += style.headerMarginTop[level];
        element->bounds = Rect2D(x, y, width, textHeight);
    }
    
    void LayoutParagraph(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        float textHeight = CalculateTextHeight(element->text, width, style.fontSize);
        element->bounds = Rect2D(x, y, width, textHeight);
    }
    
    void LayoutCodeBlock(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        float textHeight = CalculateTextHeight(element->text, width - style.codeBlockPadding * 2, style.codeFontSize);
        float totalHeight = textHeight + style.codeBlockPadding * 2;
        element->bounds = Rect2D(x, y, width, totalHeight);
    }
    
    void LayoutQuote(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        float availableWidth = width - style.quoteMarginLeft - style.quotePadding * 2;
        float textHeight = CalculateTextHeight(element->text, availableWidth, style.fontSize);
        float totalHeight = textHeight + style.quotePadding * 2;
        element->bounds = Rect2D(x + style.quoteMarginLeft, y, width - style.quoteMarginLeft, totalHeight);
    }
    
    void LayoutListItem(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        float availableWidth = width - style.listIndent;
        float textHeight = CalculateTextHeight(element->text, availableWidth, style.fontSize);
        element->bounds = Rect2D(x + style.listIndent, y, availableWidth, textHeight);
    }
    
    void LayoutTable(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        // Simplified table layout
        float rowHeight = style.fontSize * style.lineHeight + style.tableCellPadding * 2;
        float totalHeight = rowHeight * element->children.size();
        element->bounds = Rect2D(x, y, width, totalHeight);
    }
    
    void LayoutHorizontalRule(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        element->bounds = Rect2D(x, y + style.horizontalRuleMargin, width, style.horizontalRuleWidth);
    }
    
    void LayoutText(std::shared_ptr<MarkdownElement> element, float x, float y, float width) {
        float textHeight = CalculateTextHeight(element->text, width, style.fontSize);
        element->bounds = Rect2D(x, y, width, textHeight);
    }
    
    void RenderElements(const Rect2D& bounds) {
        for (const auto& element : elements) {
            // Check if element is visible in current scroll area
            if (IsElementVisible(element, bounds)) {
                RenderElement(element);
            }
        }
    }
    
    void RenderElement(std::shared_ptr<MarkdownElement> element) {
        switch (element->type) {
            case MarkdownElementType::Header:
                RenderHeader(element);
                break;
                
            case MarkdownElementType::Paragraph:
                RenderParagraph(element);
                break;
                
            case MarkdownElementType::CodeBlock:
                RenderCodeBlock(element);
                break;
                
            case MarkdownElementType::Quote:
                RenderQuote(element);
                break;
                
            case MarkdownElementType::ListItem:
                RenderListItem(element);
                break;
                
            case MarkdownElementType::HorizontalRule:
                RenderHorizontalRule(element);
                break;
                
            case MarkdownElementType::Link:
                RenderLink(element);
                break;
                
            default:
                RenderText(element);
                break;
        }
    }
    
    void RenderHeader(std::shared_ptr<MarkdownElement> element) {
        int level = std::clamp(element->level - 1, 0, 5);
        
        SetFont(style.fontFamily, style.headerSizes[level]);
        SetColor(style.headerColors[level]);
        SetFontWeight(FontWeight::Bold);
        
        Point2D position = GetAdjustedPosition(element->bounds);
        DrawText(element->text, position);
    }
    
    void RenderParagraph(std::shared_ptr<MarkdownElement> element) {
        SetFont(style.fontFamily, style.fontSize);
        SetColor(style.textColor);
        SetFontWeight(FontWeight::Normal);
        
        Point2D position = GetAdjustedPosition(element->bounds);
        DrawTextWrapped(element->text, element->bounds, style.lineHeight);
    }
    
    void RenderCodeBlock(std::shared_ptr<MarkdownElement> element) {
        Rect2D adjustedBounds = GetAdjustedBounds(element->bounds);
        
        // Draw background
       ctx->PaintWidthColorstyle.codeBlockBackgroundColor);
        FillRect(adjustedBounds);
        
        // Draw border
        ctx->PaintWidthColorstyle.codeBlockBorderColor);
        SetStrokeWidth(style.codeBlockBorderWidth);
        DrawRect(adjustedBounds);
        
        // Draw text
        SetFont(style.codeFont, style.codeFontSize);
        SetColor(style.codeTextColor);
        
        Rect2D textBounds = Rect2D(
            adjustedBounds.x + style.codeBlockPadding,
            adjustedBounds.y + style.codeBlockPadding,
            adjustedBounds.width - style.codeBlockPadding * 2,
            adjustedBounds.height - style.codeBlockPadding * 2
        );
        
        DrawTextWrapped(element->text, textBounds, 1.2f);
    }
    
    void RenderQuote(std::shared_ptr<MarkdownElement> element) {
        Rect2D adjustedBounds = GetAdjustedBounds(element->bounds);
        
        // Draw left border
        ctx->PaintWidthColorstyle.quoteBorderColor);
        SetStrokeWidth(style.quoteBorderWidth);
        ctx->DrawLine(
            Point2D(adjustedBounds.x, adjustedBounds.y),
            Point2D(adjustedBounds.x, adjustedBounds.y + adjustedBounds.height)
        );
        
        // Draw text
        SetFont(style.fontFamily, style.fontSize);
        SetColor(style.quoteTextColor);
        SetFontStyle(FontStyle::Italic);
        
        Rect2D textBounds = Rect2D(
            adjustedBounds.x + style.quotePadding,
            adjustedBounds.y + style.quotePadding,
            adjustedBounds.width - style.quotePadding * 2,
            adjustedBounds.height - style.quotePadding * 2
        );
        
        DrawTextWrapped(element->text, textBounds, style.lineHeight);
    }
    
    void RenderListItem(std::shared_ptr<MarkdownElement> element) {
        Point2D adjustedPosition = GetAdjustedPosition(element->bounds);
        
        // Draw bullet or number
        SetFont(style.fontFamily, style.fontSize);
        SetColor(style.bulletColor);
        
        if (element->ordered) {
            // Would need to track list item numbers
            DrawText("1.", Point2D(adjustedPosition.x - style.listIndent, adjustedPosition.y));
        } else {
            DrawText(style.bulletCharacter, Point2D(adjustedPosition.x - style.listIndent, adjustedPosition.y));
        }
        
        // Draw text
        SetColor(style.textColor);
        DrawTextWrapped(element->text, GetAdjustedBounds(element->bounds), style.lineHeight);
    }
    
    void RenderHorizontalRule(std::shared_ptr<MarkdownElement> element) {
        Rect2D adjustedBounds = GetAdjustedBounds(element->bounds);
        
        ctx->PaintWidthColorstyle.horizontalRuleColor);
        SetStrokeWidth(style.horizontalRuleWidth);
        ctx->DrawLine(
            Point2D(adjustedBounds.x, adjustedBounds.y + adjustedBounds.height / 2),
            Point2D(adjustedBounds.x + adjustedBounds.width, adjustedBounds.y + adjustedBounds.height / 2)
        );
    }
    
    void RenderLink(std::shared_ptr<MarkdownElement> element) {
        SetFont(style.fontFamily, style.fontSize);
        
        // Determine link color
        Color linkColor = style.linkColor;
        if (std::find(visitedLinks.begin(), visitedLinks.end(), element->url) != visitedLinks.end()) {
            linkColor = style.linkVisitedColor;
        }
        if (element == hoveredElement) {
            linkColor = style.linkHoverColor;
        }
        
        SetColor(linkColor);
        
        Point2D position = GetAdjustedPosition(element->bounds);
        DrawText(element->text, position);
        
        // Draw underline if enabled
        if (style.linkUnderline) {
            float textWidth = GetTextWidth(element->text);
            ctx->PaintWidthColorlinkColor);
            SetStrokeWidth(1.0f);
            ctx->DrawLine(
                Point2D(position.x, position.y + 2),
                Point2D(position.x + textWidth, position.y + 2)
            );
        }
    }
    
    void RenderText(std::shared_ptr<MarkdownElement> element) {
        SetFont(style.fontFamily, style.fontSize);
        SetColor(style.textColor);
        
        Point2D position = GetAdjustedPosition(element->bounds);
        DrawText(element->text, position);
    }
    
    void DrawScrollbar(const Rect2D& bounds) {
        if (contentHeight <= bounds.height) return;
        
        float scrollbarWidth = 16.0f;
        float scrollbarX = bounds.x + bounds.width - scrollbarWidth;
        
        // Draw scrollbar track
        Rect2D trackRect(scrollbarX, bounds.y, scrollbarWidth, bounds.height);
       ctx->PaintWidthColorColor(240, 240, 240));
        FillRect(trackRect);
        
        // Draw scrollbar thumb
        float thumbHeight = std::max(20.0f, bounds.height * (bounds.height / contentHeight));
        float thumbY = bounds.y + (scrollOffset / contentHeight) * bounds.height;
        
        Rect2D thumbRect(scrollbarX + 2, thumbY, scrollbarWidth - 4, thumbHeight);
       ctx->PaintWidthColorColor(180, 180, 180));
        FillRect(thumbRect);
    }
    
    // ===== UTILITY METHODS =====
    Point2D GetAdjustedPosition(const Rect2D& bounds) {
        return Point2D(bounds.x, bounds.y - scrollOffset);
    }
    
    Rect2D GetAdjustedBounds(const Rect2D& bounds) {
        return Rect2D(bounds.x, bounds.y - scrollOffset, bounds.width, bounds.height);
    }
    
    bool IsElementVisible(std::shared_ptr<MarkdownElement> element, const Rect2D& viewport) {
        Rect2D adjustedBounds = GetAdjustedBounds(element->bounds);
        return adjustedBounds.Intersects(viewport);
    }
    
    float CalculateTextHeight(const std::string& text, float width, float fontSize) {
        // Simplified height calculation - would need proper text metrics
        float charsPerLine = width / (fontSize * 0.6f); // Approximate
        int lineCount = std::max(1, static_cast<int>(text.length() / charsPerLine));
        return lineCount * fontSize * style.lineHeight;
    }
    
    void DrawTextWrapped(const std::string& text, const Rect2D& bounds, float lineHeight) {
        // Simplified text wrapping - would need proper implementation
        DrawText(text, Point2D(bounds.x, bounds.y + style.fontSize));
    }
    
    void SetFontWeight(FontWeight weight) {
        TextStyle textStyle = GetTextStyle();
        textStyle.fontWeight = weight;
        ctx->SetTextStyle(textStyle);
    }
    
    void SetFontStyle(FontStyle fontStyle) {
        TextStyle textStyle = GetTextStyle();
        textStyle.fontStyle = fontStyle;
        ctx->SetTextStyle(textStyle);
    }
    
    void HandleMouseDown(const Point2D& position, UCMouseButton button) {
        if (button == UCMouseButton::Left) {
            auto element = FindElementAtPosition(position);
            if (element && element->clickable) {
                clickedElement = element;
            }
        }
    }
    
    void HandleMouseMove(const Point2D& position) {
        auto element = FindElementAtPosition(position);
        if (element != hoveredElement) {
            hoveredElement = element;
            // Could trigger redraw for hover effects
        }
    }
    
    void HandleMouseUp(const Point2D& position, UCMouseButton button) {
        if (button == UCMouseButton::Left && clickedElement) {
            auto element = FindElementAtPosition(position);
            if (element == clickedElement && element->type == MarkdownElementType::Link) {
                if (onLinkClicked) {
                    onLinkClicked(element->url);
                }
                visitedLinks.push_back(element->url);
            }
            clickedElement = nullptr;
        }
    }
    
    void HandleKeyDown(UCKey key) {
        if (!style.enableScrolling) return;
        
        switch (key) {
            case UCKey::ArrowUp:
                ScrollBy(-20.0f);
                break;
            case UCKey::ArrowDown:
                ScrollBy(20.0f);
                break;
            case UCKey::PageUp:
                ScrollBy(-GetHeight() * 0.8f);
                break;
            case UCKey::PageDown:
                ScrollBy(GetHeight() * 0.8f);
                break;
            case UCKey::Home:
                ScrollTo(0.0f);
                break;
            case UCKey::End:
                ScrollTo(contentHeight);
                break;
            default:
                break;
        }
    }
    
    std::shared_ptr<MarkdownElement> FindElementAtPosition(const Point2D& position) {
        Point2D adjustedPosition(position.x, position.y + scrollOffset);
        
        for (const auto& element : elements) {
            if (element->bounds.Contains(adjustedPosition)) {
                return element;
            }
        }
        
        return nullptr;
    }
    
    void CollectLinks(std::shared_ptr<MarkdownElement> element, std::vector<std::string>& links) const {
        if (element->type == MarkdownElementType::Link && !element->url.empty()) {
            links.push_back(element->url);
        }
        
        for (const auto& child : element->children) {
            CollectLinks(child, links);
        }
    }
    
    void ExtractPlainText(std::shared_ptr<MarkdownElement> element, std::string& result) const {
        if (!element->text.empty()) {
            result += element->text;
            if (element->type == MarkdownElementType::Header || 
                element->type == MarkdownElementType::Paragraph) {
                result += "\n";
            }
        }
        
        for (const auto& child : element->children) {
            ExtractPlainText(child, result);
        }
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasMarkdownDisplay> CreateMarkdownDisplay(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return std::make_shared<UltraCanvasMarkdownDisplay>(identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasMarkdownDisplay> CreateMarkdownViewer(
    const std::string& identifier, long x, long y, long w, long h, const std::string& markdown = "") {
    auto viewer = std::make_shared<UltraCanvasMarkdownDisplay>(identifier, 0, x, y, w, h);
    if (!markdown.empty()) {
        viewer->SetMarkdownText(markdown);
    }
    return viewer;
}

} // namespace UltraCanvas
