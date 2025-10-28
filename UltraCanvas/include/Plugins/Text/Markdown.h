// Markdown.h
// Markdown text display driver with full formatting and rendering support
// Version: 1.0.1
// Last Modified: 2025-10-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <regex>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <fstream>
#include <algorithm>

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
        float fontSize = 12.0f;
        Color textColor = Color(50, 50, 50);
        Color backgroundColor = Colors::Transparent;
        float lineHeight = 1.2f;

        // Header styles
        std::array<float, 6> headerSizes = {24.0f, 20.0f, 18.0f, 16.0f, 14.0f, 12.0f};
        std::array<Color, 6> headerColors = {
                Color(20, 20, 20), Color(30, 30, 30), Color(40, 40, 40),
                Color(50, 50, 50), Color(60, 60, 60), Color(70, 70, 70)
        };
        std::array<float, 6> headerMarginTop = {10.0f, 9.0f, 8.0f, 7.0f, 6.0f, 5.0f};
        std::array<float, 6> headerMarginBottom = {6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};

        // Code styling
        std::string codeFont = "Consolas";
        float codeFontSize = 12.0f;
        Color codeTextColor = Color(200, 50, 50);
        Color codeBackgroundColor = Color(245, 245, 245);
        Color codeBlockBackgroundColor = Color(248, 248, 248);
        Color codeBlockBorderColor = Color(220, 220, 220);
        int codeBlockPadding = 10;
        float codeBlockBorderWidth = 1;
        float codeBlockBorderRadius = 4.0;

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
        Color quoteBarColor = Color(200, 200, 200);
        Color quoteBackgroundColor = Color(250, 250, 250);
        float quoteBarWidth = 4.0f;
        float quoteMarginLeft = 15.0f;
        float quotePadding = 10.0f;

        // Table styling
        Color tableBorderColor = Color(200, 200, 200);
        Color tableHeaderBackground = Color(240, 240, 240);
        float tableBorderWidth = 1.0f;
        float tableCellPadding = 8.0f;

        // Spacing
        float paragraphSpacing = 10.0f;
        float blockSpacing = 15.0f;

        // Horizontal rule
        Color horizontalRuleColor = Color(200, 200, 200);
        float horizontalRuleWidth = 2.0f;
        float horizontalRuleMargin = 20.0f;

        // Scrolling
        bool enableScrolling = true;
        Color scrollbarColor = Color(180, 180, 180);
        Color scrollbarTrackColor = Color(240, 240, 240);
        int scrollbarWidth = 10;

        // Theme presets
        static MarkdownStyle Default() {
            return MarkdownStyle();
        }

        static MarkdownStyle DarkTheme() {
            MarkdownStyle style;
            style.textColor = Color(220, 220, 220);
            style.backgroundColor = Color(30, 30, 30);

            for (auto& color : style.headerColors) {
                color = Color(240, 240, 240);
            }

            style.codeTextColor = Color(255, 120, 120);
            style.codeBackgroundColor = Color(50, 50, 50);
            style.codeBlockBackgroundColor = Color(40, 40, 40);
            style.codeBlockBorderColor = Color(80, 80, 80);

            style.linkColor = Color(100, 180, 255);
            style.linkHoverColor = Color(150, 200, 255);
            style.linkVisitedColor = Color(180, 140, 255);

            style.quoteBarColor = Color(100, 100, 100);
            style.quoteBackgroundColor = Color(40, 40, 40);

            style.tableBorderColor = Color(80, 80, 80);
            style.tableHeaderBackground = Color(50, 50, 50);

            style.horizontalRuleColor = Color(100, 100, 100);
            style.scrollbarColor = Color(100, 100, 100);
            style.scrollbarTrackColor = Color(50, 50, 50);

            return style;
        }

        static MarkdownStyle DocumentStyle() {
            MarkdownStyle style;
            style.fontFamily = "Georgia";
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
        Rect2Di bounds;
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
    public:
        static std::vector<std::shared_ptr<MarkdownElement>> Parse(const std::string& markdown) {
            std::vector<std::shared_ptr<MarkdownElement>> elements;
            std::istringstream stream(markdown);
            std::string line;

            while (std::getline(stream, line)) {
                auto element = ParseLine(line);
                if (element) {
                    elements.push_back(element);
                }
            }

            return elements;
        }

    private:
        static std::shared_ptr<MarkdownElement> ParseLine(const std::string& line) {
            std::string trimmedLine = Trim(line);

            if (trimmedLine.empty()) {
                return nullptr;
            }

            // Check for headers
            if (trimmedLine[0] == '#') {
                int level = 0;
                while (level < trimmedLine.length() && trimmedLine[level] == '#') {
                    level++;
                }
                if (level <= 6) {
                    std::string content = Trim(trimmedLine.substr(level));
                    return MarkdownElement::CreateHeader(level, content);
                }
            }

            // Check for horizontal rule
            if (trimmedLine == "---" || trimmedLine == "***" || trimmedLine == "___") {
                return std::make_shared<MarkdownElement>(MarkdownElementType::HorizontalRule);
            }

            // Check for code blocks
            if (trimmedLine.substr(0, 3) == "```") {
                std::string language = trimmedLine.substr(3);
                return std::make_shared<MarkdownElement>(MarkdownElementType::CodeBlock);
            }

            // Check for quotes
            if (trimmedLine[0] == '>') {
                auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Quote);
                element->text = Trim(trimmedLine.substr(1));
                return element;
            }

            // Check for lists
            if (trimmedLine[0] == '-' || trimmedLine[0] == '*' || trimmedLine[0] == '+') {
                auto element = std::make_shared<MarkdownElement>(MarkdownElementType::ListItem);
                element->text = Trim(trimmedLine.substr(1));
                element->ordered = false;
                return element;
            }

            // Check for ordered lists
            if (std::isdigit(trimmedLine[0])) {
                size_t dotPos = trimmedLine.find('.');
                if (dotPos != std::string::npos) {
                    auto element = std::make_shared<MarkdownElement>(MarkdownElementType::ListItem);
                    element->text = Trim(trimmedLine.substr(dotPos + 1));
                    element->ordered = true;
                    return element;
                }
            }

            // Default to paragraph
            auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Paragraph);
            element->text = ParseInlineFormatting(trimmedLine);
            return element;
        }

        static std::string ParseInlineFormatting(const std::string& text) {
            // Simplified inline formatting - would need more complex parsing for full support
            return text;
        }

        static std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";

            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }
    };

// ===== MARKDOWN DISPLAY COMPONENT =====
    class UltraCanvasMarkdownDisplay : public UltraCanvasUIElement {
    private:
        // Markdown content and styling
        std::string markdownText;
        MarkdownStyle style;
        std::vector<std::shared_ptr<MarkdownElement>> elements;

        // Layout and rendering
        int contentHeight = 0;
        int verticalScrollOffset = 0;
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

            style = MarkdownStyle::Default();
        }

        virtual ~UltraCanvasMarkdownDisplay() = default;

        // ===== MARKDOWN CONTENT =====
        void SetMarkdownText(const std::string& markdown) {
            if (markdownText != markdown) {
                markdownText = markdown;
                needsReparse = true;
                needsRelayout = true;
                RequestRedraw();
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
            RequestRedraw();
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
        void ScrollTo(int position) {
            int maxScroll = std::max(0, contentHeight - GetHeight());
            verticalScrollOffset = std::clamp(position, 0, maxScroll);

            if (onScrollChanged) {
                onScrollChanged(GetScrollPercentage());
            }
        }

        void ScrollBy(int delta) {
            ScrollTo(verticalScrollOffset + delta);
        }

        int GetScrollPosition() const { return verticalScrollOffset; }

        float GetScrollPercentage() const {
            int maxScroll = std::max(0, contentHeight - GetHeight());
            return maxScroll > 0 ? (static_cast<float>(verticalScrollOffset) / static_cast<float>(maxScroll)) : 0.0f;
        }

        bool CanScrollUp() const { return verticalScrollOffset > 0; }
        bool CanScrollDown() const { return verticalScrollOffset < (contentHeight - GetHeight()); }

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

            IRenderContext* ctx = GetRenderContext();
            if (!ctx) return;

            ctx->PushState();

            // Parse markdown if needed
            if (needsReparse) {
                ParseMarkdown();
                needsRelayout = true;
            }

            // Layout elements if needed
            if (needsRelayout) {
                LayoutElements();
            }

            // Set up clipping
            Rect2Di bounds = GetBounds();
            ctx->SetClipRect(bounds);

            // Draw background
            if (style.backgroundColor.a > 0) {
                ctx->SetFillPaint(style.backgroundColor);
                ctx->FillRectangle(bounds);
            }

            // Render markdown elements
            RenderElements(ctx, bounds);

            // Draw scrollbar if needed
            if (style.enableScrolling && contentHeight > bounds.height) {
                DrawScrollbar(ctx, bounds);
            }

            ctx->ClearClipRect();
            ctx->PopState();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;

            switch (event.type) {
                case UCEventType::MouseDown:
                    return HandleMouseDown(event);

                case UCEventType::MouseMove:
                    return HandleMouseMove(event);

                case UCEventType::MouseUp:
                    return HandleMouseUp(event);

                case UCEventType::MouseWheel:
                    if (style.enableScrolling) {
                        ScrollBy(-event.wheelDelta * 3);
                        return true;
                    }
                    break;

                case UCEventType::KeyDown:
                    return HandleKeyDown(event.virtualKey);

                default:
                    break;
            }
            return false;
        }

        // ===== INFORMATION QUERIES =====
        std::vector<std::string> GetAllLinks() const {
            std::vector<std::string> links;
            for (const auto& element : elements) {
                CollectLinks(element, links);
            }
            return links;
        }

        std::string GetPlainText() const {
            std::string result;
            for (const auto& element : elements) {
                ExtractPlainText(element, result);
            }
            return result;
        }

        int GetElementCount() const {
            return static_cast<int>(elements.size());
        }

        float GetContentHeight() const {
            return contentHeight;
        }

    private:
        // ===== PARSING =====
        void ParseMarkdown() {
            elements = MarkdownParser::Parse(markdownText);
            needsReparse = false;
        }

        // ===== LAYOUT =====
        void LayoutElements() {
            int currentY = 0.0f;
            Rect2Di bounds = GetBounds();
            int availableWidth = bounds.width - 20; // Padding

            for (auto& element : elements) {
                // Add top margin for headers
                if (element->type == MarkdownElementType::Header) {
                    currentY += style.headerMarginTop[std::min(element->level - 1, 5)];
                }

                LayoutElement(element, 10, currentY, availableWidth);
                currentY += element->bounds.height;

                // Add bottom spacing
                if (element->type == MarkdownElementType::Header) {
                    currentY += style.headerMarginBottom[std::min(element->level - 1, 5)];
                } else if (element->type == MarkdownElementType::Paragraph) {
                    currentY += style.paragraphSpacing;
                } else {
                    currentY += style.blockSpacing;
                }
            }

            contentHeight = currentY;
            needsRelayout = false;
        }

        void LayoutElement(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
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

        void LayoutHeader(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            int level = std::clamp(element->level - 1, 0, 5);
            int fontSize = style.headerSizes[level];
            int textHeight = fontSize * style.lineHeight;

            element->bounds = Rect2Di(x, y, width, textHeight);
        }

        void LayoutParagraph(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            int textHeight = CalculateTextHeight(element->text, width, style.fontSize);
            element->bounds = Rect2Di(x, y, width, textHeight);
        }

        void LayoutCodeBlock(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            int textHeight = CalculateTextHeight(element->text, width - style.codeBlockPadding * 2, style.codeFontSize);
            int totalHeight = textHeight + style.codeBlockPadding * 2;
            element->bounds = Rect2Di(x, y, width, totalHeight);
        }

        void LayoutQuote(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            int availableWidth = width - style.quoteMarginLeft - style.quotePadding * 2;
            int textHeight = CalculateTextHeight(element->text, availableWidth, style.fontSize);
            int totalHeight = textHeight + style.quotePadding * 2;
            element->bounds = Rect2Di(x + style.quoteMarginLeft, y, width - style.quoteMarginLeft, totalHeight);
        }

        void LayoutListItem(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            int availableWidth = width - style.listIndent;
            int textHeight = CalculateTextHeight(element->text, availableWidth, style.fontSize);
            element->bounds = Rect2Di(x + style.listIndent, y, availableWidth, textHeight);
        }

        void LayoutTable(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            // Simplified table layout
            int rowHeight = style.fontSize * style.lineHeight + style.tableCellPadding * 2;
            int totalHeight = rowHeight * static_cast<int>(element->children.size());
            element->bounds = Rect2Di(x, y, width, totalHeight);
        }

        void LayoutHorizontalRule(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            element->bounds = Rect2Di(x, y + style.horizontalRuleMargin, width, style.horizontalRuleWidth);
        }

        void LayoutText(std::shared_ptr<MarkdownElement> element, int x, int y, int width) {
            int textHeight = CalculateTextHeight(element->text, width, style.fontSize);
            element->bounds = Rect2Di(x, y, width, textHeight);
        }

        void RenderElements(IRenderContext* ctx, const Rect2Di& bounds) {
            for (const auto& element : elements) {
                // Check if element is visible in current scroll area
                if (IsElementVisible(element, bounds)) {
                    RenderElement(ctx, element);
                }
            }
        }

        void RenderElement(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            switch (element->type) {
                case MarkdownElementType::Header:
                    RenderHeader(ctx, element);
                    break;

                case MarkdownElementType::Paragraph:
                    RenderParagraph(ctx, element);
                    break;

                case MarkdownElementType::CodeBlock:
                    RenderCodeBlock(ctx, element);
                    break;

                case MarkdownElementType::Quote:
                    RenderQuote(ctx, element);
                    break;

                case MarkdownElementType::ListItem:
                    RenderListItem(ctx, element);
                    break;

                case MarkdownElementType::HorizontalRule:
                    RenderHorizontalRule(ctx, element);
                    break;

                case MarkdownElementType::Link:
                    RenderLink(ctx, element);
                    break;

                default:
                    RenderText(ctx, element);
                    break;
            }
        }

        void RenderHeader(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            int level = std::clamp(element->level - 1, 0, 5);

            ctx->SetFontSize(style.headerSizes[level]);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(style.headerColors[level]);

            Point2Di position = GetAdjustedPosition(element->bounds);
            ctx->DrawText(element->text, position);
        }

        void RenderParagraph(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            ctx->SetFontSize(style.fontSize);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(style.textColor);

            DrawTextWrapped(ctx, element->text, GetAdjustedBounds(element->bounds), style.lineHeight);
        }

        void RenderCodeBlock(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

            // Draw background
            ctx->SetFillPaint(style.codeBlockBackgroundColor);
            ctx->FillRoundedRectangle(adjustedBounds, style.codeBlockBorderRadius);

            // Draw border
            ctx->SetStrokePaint(style.codeBlockBorderColor);
            ctx->SetStrokeWidth(style.codeBlockBorderWidth);
            ctx->DrawRoundedRectangle(adjustedBounds, style.codeBlockBorderRadius);

            // Draw text
            ctx->SetFontSize(style.codeFontSize);
            ctx->SetTextPaint(style.codeTextColor);

            Rect2Di textBounds = adjustedBounds;
            textBounds.x += style.codeBlockPadding;
            textBounds.y += style.codeBlockPadding;
            textBounds.width -= style.codeBlockPadding * 2;
            textBounds.height -= style.codeBlockPadding * 2;

            DrawTextWrapped(ctx, element->text, textBounds, 1.2f);
        }

        void RenderQuote(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

            // Draw background
            ctx->SetFillPaint(style.quoteBackgroundColor);
            ctx->FillRectangle(adjustedBounds);

            // Draw left bar
            Rect2Di barRect = adjustedBounds;
            barRect.width = style.quoteBarWidth;
            ctx->SetFillPaint(style.quoteBarColor);
            ctx->FillRectangle(barRect);

            // Draw text
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(style.textColor);

            Rect2Di textBounds = adjustedBounds;
            textBounds.x += style.quotePadding;
            textBounds.y += style.quotePadding;
            textBounds.width -= style.quotePadding * 2;

            DrawTextWrapped(ctx, element->text, textBounds, style.lineHeight);
        }

        void RenderListItem(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);
            Point2Di adjustedPosition = GetAdjustedPosition(element->bounds);

            // Draw bullet or number
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(style.bulletColor);

            if (element->ordered) {
                ctx->DrawText("1.", Point2Di(adjustedPosition.x - style.listIndent, adjustedPosition.y));
            } else {
                ctx->DrawText(style.bulletCharacter, Point2Di(adjustedPosition.x - style.listIndent, adjustedPosition.y));
            }

            // Draw text
            ctx->SetTextPaint(style.textColor);
            DrawTextWrapped(ctx, element->text, adjustedBounds, style.lineHeight);
        }

        void RenderHorizontalRule(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

            ctx->SetStrokePaint(style.horizontalRuleColor);
            ctx->SetStrokeWidth(style.horizontalRuleWidth);
            ctx->DrawLine(
                    Point2Di(adjustedBounds.x, adjustedBounds.y + adjustedBounds.height / 2),
                    Point2Di(adjustedBounds.x + adjustedBounds.width, adjustedBounds.y + adjustedBounds.height / 2)
            );
        }

        void RenderLink(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            ctx->SetFontSize(style.fontSize);

            // Determine link color
            Color linkColor = style.linkColor;
            if (std::find(visitedLinks.begin(), visitedLinks.end(), element->url) != visitedLinks.end()) {
                linkColor = style.linkVisitedColor;
            }
            if (element == hoveredElement) {
                linkColor = style.linkHoverColor;
            }

            ctx->SetTextPaint(linkColor);

            Point2Di position = GetAdjustedPosition(element->bounds);
            ctx->DrawText(element->text, position);

            // Draw underline if enabled
            if (style.linkUnderline) {
                int textWidth = ctx->GetTextLineWidth(element->text);
                ctx->SetStrokePaint(linkColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(
                        Point2Di(position.x, position.y + 2),
                        Point2Di(position.x + textWidth, position.y + 2)
                );
            }
        }

        void RenderText(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(style.textColor);

            Point2Di position = GetAdjustedPosition(element->bounds);
            ctx->DrawText(element->text, position);
        }

        // ===== HELPER FUNCTIONS =====
        int CalculateTextHeight(const std::string& text, int width, float fontSize) {
            // Simplified calculation - would need proper text measurement
            IRenderContext* ctx = GetRenderContext();
            if (!ctx) return fontSize * style.lineHeight;
            int w, h;
            ctx->SetFontSize(fontSize);
            ctx->SetTextLineHeight(style.lineHeight);
            ctx->GetTextDimensions(text, width, 0, w, h);
            return h;
        }

        void DrawTextWrapped(IRenderContext* ctx, const std::string& text, const Rect2Di& bounds, float lineHeight) {
            ctx->SetTextLineHeight(lineHeight);
            ctx->DrawTextInRect(text, bounds);
        }

        bool IsElementVisible(std::shared_ptr<MarkdownElement> element, const Rect2Di& viewBounds) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);
            return adjustedBounds.y + adjustedBounds.height >= viewBounds.y &&
                   adjustedBounds.y <= viewBounds.y + viewBounds.height;
        }

        Point2Di GetAdjustedPosition(const Rect2Di& bounds) {
            return Point2Di(bounds.x, bounds.y - verticalScrollOffset);
        }

        Rect2Di GetAdjustedBounds(const Rect2Di& bounds) {
            return Rect2Di(bounds.x, bounds.y - verticalScrollOffset, bounds.width, bounds.height);
        }

        void DrawScrollbar(IRenderContext* ctx, const Rect2Di& bounds) {
            int scrollbarX = bounds.x + bounds.width - style.scrollbarWidth;
            int scrollbarHeight = bounds.height;

            // Draw track
            Rect2Di trackRect(scrollbarX, bounds.y, style.scrollbarWidth, scrollbarHeight);
            ctx->SetFillPaint(style.scrollbarTrackColor);
            ctx->FillRectangle(trackRect);

            // Draw thumb
            int maxScroll = std::max(0, contentHeight - bounds.height);
            int thumbHeight = std::max(20.0f, (static_cast<float>(bounds.height) / static_cast<float>(contentHeight)) * scrollbarHeight);
            int thumbY = bounds.y + (static_cast<float>(verticalScrollOffset) / static_cast<float>(maxScroll)) * (scrollbarHeight - thumbHeight);

            Rect2Di thumbRect(scrollbarX, thumbY, style.scrollbarWidth, thumbHeight);
            ctx->SetFillPaint(style.scrollbarColor);
            ctx->FillRectangle(thumbRect);
        }

        bool HandleMouseDown(const UCEvent& event) {
            if (event.button == UCMouseButton::Left) {
                auto element = FindElementAtPosition(event.x, event.y);
                if (element && element->clickable) {
                    clickedElement = element;
                    return true;
                }
            }
            return false;
        }

        bool HandleMouseMove(const UCEvent& event) {
            auto element = FindElementAtPosition(event.x, event.y);
            if (element != hoveredElement) {
                hoveredElement = element;
                // Could trigger redraw for hover effects
            }
            return false;
        }

        bool HandleMouseUp(const UCEvent& event) {
            if (event.button == UCMouseButton::Left && clickedElement) {
                auto element = FindElementAtPosition(event.x, event.y);
                if (element == clickedElement && element->type == MarkdownElementType::Link) {
                    if (onLinkClicked) {
                        onLinkClicked(element->url);
                    }
                    visitedLinks.push_back(element->url);
                    return true;
                }
                clickedElement = nullptr;
            }
            return false;
        }

        bool HandleKeyDown(UCKeys key) {
            if (!style.enableScrolling) return false;

            switch (key) {
                case UCKeys::Up:
                    ScrollBy(-20.0f);
                    return true;
                case UCKeys::Down:
                    ScrollBy(20.0f);
                    return true;
                case UCKeys::PageUp:
                    ScrollBy(static_cast<int>(static_cast<float>(-GetHeight()) * 0.8f));
                    return true;
                case UCKeys::PageDown:
                    ScrollBy(static_cast<int>(static_cast<float>(GetHeight()) * 0.8f));
                    return true;
                case UCKeys::Home:
                    ScrollTo(0.0f);
                    return true;
                case UCKeys::End:
                    ScrollTo(contentHeight);
                    return true;
                default:
                    break;
            }
            return false;
        }

        std::shared_ptr<MarkdownElement> FindElementAtPosition(int x, int y) {
            y = y + verticalScrollOffset;

            for (const auto& element : elements) {
                if (element->bounds.Contains(x, y)) {
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
