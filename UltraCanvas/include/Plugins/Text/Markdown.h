// include/Plugins/Text/Markdown.h
// Markdown text display driver with full formatting and rendering support
// Version: 1.1.0
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
        Paragraph,       // Block of text
        Unknown
    };

// ===== MARKDOWN STYLING =====
    struct MarkdownStyle {
        // Base text style
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        Color textColor = Color(50, 50, 50);
        Color backgroundColor = Colors::White;
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
        int listIndent = 20;
        int listItemSpacing = 4;
        std::string bulletCharacter = "•";
        Color bulletColor = Color(80, 80, 80);

        // Quote styling
        int quoteIndent = 15;
        int quoteBarWidth = 4;
        Color quoteBarColor = Color(200, 200, 200);
        Color quoteBackgroundColor = Color(250, 250, 250);

        // Table styling
        Color tableBorderColor = Color(200, 200, 200);
        Color tableHeaderBackground = Color(240, 240, 240);
        float tablePadding = 8.0f;

        // Spacing
        float paragraphSpacing = 12.0f;
        float blockSpacing = 8.0f;

        // Horizontal rule
        Color horizontalRuleColor = Color(200, 200, 200);
        float horizontalRuleHeight = 2.0f;

        // Scrollbar
        Color scrollbarColor = Color(180, 180, 180);
        Color scrollbarTrackColor = Color(240, 240, 240);

        static MarkdownStyle Default() {
            return MarkdownStyle();
        }

        static MarkdownStyle DarkMode() {
            MarkdownStyle style;
            style.backgroundColor = Color(30, 30, 30);
            style.textColor = Color(220, 220, 220);

            for (auto& color : style.headerColors) {
                color = Color(240, 240, 240);
            }

            style.codeTextColor = Color(255, 150, 150);
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

// ===== INLINE STYLE CONVERSION FOR PANGO MARKUP =====
    class MarkdownInlineConverter {
    public:
        // Convert markdown inline styles to Pango markup
        static std::string ConvertToPangoMarkup(const std::string& text) {
            std::string result = text;

            // Escape existing XML/HTML special characters first
            result = EscapeXml(result);

            // Convert **bold** or __bold__ to <b>bold</b>
            result = ConvertBold(result);

            // Convert *italic* or _italic_ to <i>italic</i>
            result = ConvertItalic(result);

            // Convert `code` to <tt>code</tt>
            result = ConvertInlineCode(result);

            // Convert ~~strikethrough~~ to <s>strikethrough</s>
            result = ConvertStrikethrough(result);

            return result;
        }

    private:
        // Escape XML special characters
        static std::string EscapeXml(const std::string& text) {
            std::string result;
            result.reserve(text.length() * 1.2);

            for (char ch : text) {
                switch (ch) {
                    case '&':  result += "&amp;"; break;
                    case '<':  result += "&lt;"; break;
                    case '>':  result += "&gt;"; break;
                    case '"':  result += "&quot;"; break;
                    case '\'': result += "&apos;"; break;
                    default:   result += ch; break;
                }
            }
            return result;
        }

        // Convert **text** or __text__ to <b>text</b>
        static std::string ConvertBold(const std::string& text) {
            std::string result = text;

            // Handle **bold**
            size_t pos = 0;
            while ((pos = result.find("**", pos)) != std::string::npos) {
                size_t end = result.find("**", pos + 2);
                if (end != std::string::npos) {
                    std::string content = result.substr(pos + 2, end - pos - 2);
                    std::string replacement = "<b>" + content + "</b>";
                    result.replace(pos, end - pos + 2, replacement);
                    pos += replacement.length();
                } else {
                    break;
                }
            }

            // Handle __bold__
            pos = 0;
            while ((pos = result.find("__", pos)) != std::string::npos) {
                size_t end = result.find("__", pos + 2);
                if (end != std::string::npos) {
                    std::string content = result.substr(pos + 2, end - pos - 2);
                    std::string replacement = "<b>" + content + "</b>";
                    result.replace(pos, end - pos + 2, replacement);
                    pos += replacement.length();
                } else {
                    break;
                }
            }

            return result;
        }

        // Convert *text* or _text_ to <i>text</i>
        static std::string ConvertItalic(const std::string& text) {
            std::string result = text;

            // Handle *italic* (but not ** which is bold)
            size_t pos = 0;
            while ((pos = result.find("*", pos)) != std::string::npos) {
                // Skip if it's part of **
                if (pos > 0 && result[pos - 1] == '*') {
                    pos++;
                    continue;
                }
                if (pos + 1 < result.length() && result[pos + 1] == '*') {
                    pos += 2;
                    continue;
                }

                size_t end = pos + 1;
                while (end < result.length()) {
                    if (result[end] == '*' && (end + 1 >= result.length() || result[end + 1] != '*')) {
                        break;
                    }
                    end++;
                }

                if (end < result.length() && result[end] == '*') {
                    std::string content = result.substr(pos + 1, end - pos - 1);
                    std::string replacement = "<i>" + content + "</i>";
                    result.replace(pos, end - pos + 1, replacement);
                    pos += replacement.length();
                } else {
                    pos++;
                }
            }

            // Handle _italic_ (but not __ which is bold)
            pos = 0;
            while ((pos = result.find("_", pos)) != std::string::npos) {
                // Skip if it's part of __
                if (pos > 0 && result[pos - 1] == '_') {
                    pos++;
                    continue;
                }
                if (pos + 1 < result.length() && result[pos + 1] == '_') {
                    pos += 2;
                    continue;
                }

                size_t end = pos + 1;
                while (end < result.length()) {
                    if (result[end] == '_' && (end + 1 >= result.length() || result[end + 1] != '_')) {
                        break;
                    }
                    end++;
                }

                if (end < result.length() && result[end] == '_') {
                    std::string content = result.substr(pos + 1, end - pos - 1);
                    std::string replacement = "<i>" + content + "</i>";
                    result.replace(pos, end - pos + 1, replacement);
                    pos += replacement.length();
                } else {
                    pos++;
                }
            }

            return result;
        }

        // Convert `code` to <tt>code</tt>
        static std::string ConvertInlineCode(const std::string& text) {
            std::string result = text;
            size_t pos = 0;

            while ((pos = result.find("`", pos)) != std::string::npos) {
                size_t end = result.find("`", pos + 1);
                if (end != std::string::npos) {
                    std::string content = result.substr(pos + 1, end - pos - 1);
                    std::string replacement = "<tt>" + content + "</tt>";
                    result.replace(pos, end - pos + 1, replacement);
                    pos += replacement.length();
                } else {
                    break;
                }
            }

            return result;
        }

        // Convert ~~text~~ to <s>text</s>
        static std::string ConvertStrikethrough(const std::string& text) {
            std::string result = text;
            size_t pos = 0;

            while ((pos = result.find("~~", pos)) != std::string::npos) {
                size_t end = result.find("~~", pos + 2);
                if (end != std::string::npos) {
                    std::string content = result.substr(pos + 2, end - pos - 2);
                    std::string replacement = "<s>" + content + "</s>";
                    result.replace(pos, end - pos + 2, replacement);
                    pos += replacement.length();
                } else {
                    break;
                }
            }

            return result;
        }
    };

// ===== MARKDOWN PARSER =====
    class MarkdownParser {
    public:
        static std::vector<std::shared_ptr<MarkdownElement>> Parse(const std::string& markdown) {
            std::vector<std::shared_ptr<MarkdownElement>> elements;
            std::istringstream stream(markdown);
            std::string line;
            std::string previousLine;
            bool inCodeBlock = false;
            std::string codeBlockContent;
            std::string codeBlockLanguage;

            while (std::getline(stream, line)) {
                // Handle code blocks
                if (line.find("```") == 0) {
                    if (!inCodeBlock) {
                        // Starting code block
                        inCodeBlock = true;
                        codeBlockLanguage = line.substr(3);
                        codeBlockContent.clear();
                    } else {
                        // Ending code block
                        inCodeBlock = false;
                        auto element = MarkdownElement::CreateCodeBlock(codeBlockContent, codeBlockLanguage);
                        elements.push_back(element);
                        codeBlockContent.clear();
                        codeBlockLanguage.clear();
                    }
                    previousLine = line;
                    continue;
                }

                if (inCodeBlock) {
                    // Accumulate code block content
                    if (!codeBlockContent.empty()) {
                        codeBlockContent += "\n";
                    }
                    codeBlockContent += line;
                    previousLine = line;
                    continue;
                }

                // Parse regular lines
                auto element = ParseLine(line, previousLine);
                if (element) {
                    elements.push_back(element);
                }
                previousLine = line;
            }

            return elements;
        }

    private:
        static std::shared_ptr<MarkdownElement> ParseLine(const std::string& line, const std::string& previousLine) {
            std::string trimmed = TrimWhitespace(line);

            // Empty line - only create spacing element if previous line wasn't empty
            if (trimmed.empty()) {
                if (!previousLine.empty()) {
                    auto element = std::make_shared<MarkdownElement>(MarkdownElementType::LineBreak);
                    return element;
                }
                return nullptr; // Skip consecutive empty lines
            }

            // Headers
            if (trimmed[0] == '#') {
                return ParseHeader(trimmed);
            }

            // Horizontal rules (---, ***, ___)
            if (IsHorizontalRule(trimmed)) {
                return std::make_shared<MarkdownElement>(MarkdownElementType::HorizontalRule);
            }

            // Quotes
            if (trimmed[0] == '>') {
                return ParseQuote(trimmed);
            }

            // Lists - FIXED: Check for space after marker
            if (IsListItem(trimmed)) {
                return ParseListItem(trimmed);
            }

            // Default: paragraph text with inline styling
            return ParseParagraph(trimmed);
        }

        static std::shared_ptr<MarkdownElement> ParseHeader(const std::string& line) {
            size_t hashCount = 0;
            while (hashCount < line.length() && line[hashCount] == '#') {
                hashCount++;
            }

            if (hashCount > 6) hashCount = 6;

            std::string content = TrimWhitespace(line.substr(hashCount));
            // Apply inline styling to header content
            content = MarkdownInlineConverter::ConvertToPangoMarkup(content);
            return MarkdownElement::CreateHeader(static_cast<int>(hashCount), content);
        }

        static std::shared_ptr<MarkdownElement> ParseQuote(const std::string& line) {
            auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Quote);
            std::string content = TrimWhitespace(line.substr(1));
            // Apply inline styling to quote content
            element->text = MarkdownInlineConverter::ConvertToPangoMarkup(content);
            return element;
        }

        // FIXED: Proper list item detection with space requirement
        static bool IsListItem(const std::string& line) {
            if (line.empty()) return false;

            std::string trimmed = TrimWhitespace(line);
            if (trimmed.length() < 2) return false;

            // Check for unordered list: "- ", "* ", "+ " (requires space after marker)
            if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
                return true;
            }

            // Check for ordered list: "1. ", "2. ", etc.
            if (std::isdigit(trimmed[0])) {
                size_t dotPos = trimmed.find('.');
                if (dotPos != std::string::npos && dotPos + 1 < trimmed.length() && trimmed[dotPos + 1] == ' ') {
                    // Verify all characters before dot are digits
                    for (size_t i = 0; i < dotPos; i++) {
                        if (!std::isdigit(trimmed[i])) return false;
                    }
                    return true;
                }
            }

            return false;
        }

        static std::shared_ptr<MarkdownElement> ParseListItem(const std::string& line) {
            auto element = std::make_shared<MarkdownElement>(MarkdownElementType::ListItem);
            std::string trimmed = TrimWhitespace(line);

            // Determine if ordered or unordered
            if (std::isdigit(trimmed[0])) {
                element->ordered = true;
                size_t dotPos = trimmed.find('.');
                std::string content = TrimWhitespace(trimmed.substr(dotPos + 1));
                // Apply inline styling to list item content
                element->text = MarkdownInlineConverter::ConvertToPangoMarkup(content);
            } else {
                element->ordered = false;
                // Skip the marker (-, *, +) and space
                std::string content = TrimWhitespace(trimmed.substr(2));
                // Apply inline styling to list item content
                element->text = MarkdownInlineConverter::ConvertToPangoMarkup(content);
            }

            return element;
        }

        static std::shared_ptr<MarkdownElement> ParseParagraph(const std::string& line) {
            auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Paragraph);
            // Apply inline styling to paragraph content
            element->text = MarkdownInlineConverter::ConvertToPangoMarkup(line);
            return element;
        }

        static bool IsHorizontalRule(const std::string& line) {
            std::string trimmed = TrimWhitespace(line);
            if (trimmed.length() < 3) return false;

            // Check for ---, ***, or ___
            char ch = trimmed[0];
            if (ch != '-' && ch != '*' && ch != '_') return false;

            for (char c : trimmed) {
                if (c != ch && c != ' ') return false;
            }

            // Count non-space characters
            int count = 0;
            for (char c : trimmed) {
                if (c != ' ') count++;
            }

            return count >= 3;
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
        // Markdown content and styling
        std::string markdownText;
        MarkdownStyle style;
        std::vector<std::shared_ptr<MarkdownElement>> elements;

        // Layout and rendering
        int contentHeight = 0;
        int scrollOffset = 0;
        bool needsReparse = true;
        bool needsRelayout = true;

        // Interaction state
        std::shared_ptr<MarkdownElement> hoveredElement;
        std::shared_ptr<MarkdownElement> clickedElement;
        std::vector<std::string> visitedLinks;

        // Event callbacks
        std::function<void(const std::string& url)> onLinkClicked;
        std::function<void(const std::string& text)> onTextSelected;
        std::function<void(int position)> onScrollChanged;

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
                RequestRedraw();;
            }
        }

        const std::string& GetMarkdownText() const {
            return markdownText;
        }

        // ===== STYLING =====
        void SetStyle(const MarkdownStyle& newStyle) {
            style = newStyle;
            needsRelayout = true;
            RequestRedraw();
        }

        const MarkdownStyle& GetStyle() const {
            return style;
        }

        // ===== SCROLLING =====
        void SetScrollOffset(int offset) {
            int maxScroll = std::max(0, contentHeight - GetHeight());
            scrollOffset = std::clamp(offset, 0, maxScroll);
            RequestRedraw();
        }

        int GetScrollOffset() const {
            return scrollOffset;
        }

        int GetContentHeight() const {
            return contentHeight;
        }

        // ===== CALLBACKS =====
        void SetLinkClickCallback(std::function<void(const std::string&)> callback) {
            onLinkClicked = callback;
        }

        void SetTextSelectedCallback(std::function<void(const std::string&)> callback) {
            onTextSelected = callback;
        }

        void SetScrollChangedCallback(std::function<void(float)> callback) {
            onScrollChanged = callback;
        }

        // ===== RENDERING =====
        void Render() override {
            auto ctx = GetRenderContext();
            if (!properties.Visible || !ctx) return;

            ctx->PushState();
            // Parse if needed
            if (needsReparse) {
                elements = MarkdownParser::Parse(markdownText);
                needsReparse = false;
                needsRelayout = true;
            }

            // Layout if needed
            if (needsRelayout) {
                PerformLayout(ctx);
                needsRelayout = false;
            }

            // Draw background
            ctx->SetFillPaint(style.backgroundColor);
            ctx->FillRectangle(properties.x_pos, properties.y_pos,
                               properties.width_size, properties.height_size);

            // Set clipping region
            ctx->SetClipRect(properties.x_pos, properties.y_pos,
                             properties.width_size, properties.height_size);

            // Enable Pango markup for text rendering
            ctx->SetTextIsMarkup(true);

            // Render elements
            for (auto& element : elements) {
                if (element->visible) {
                    RenderElement(ctx, element);
                }
            }

            // Clear clipping
            ctx->ClearClipRect();

            // Draw scrollbar if needed
            if (contentHeight > properties.height_size) {
                DrawScrollbar(ctx);
            }
            ctx->PopState();
        }

    private:
        void PerformLayout(IRenderContext* ctx) {
            int currentY = GetY() + 10;
            int maxWidth = GetWidth() - 20;
            MarkdownElementType previousElementType = MarkdownElementType::Unknown;

            for (auto& element : elements) {
                // Calculate spacing based on element type and previous element
                float topSpacing = 0.0f;

                // Add block spacing only when there's an actual line break element
                if (element->type == MarkdownElementType::LineBreak) {
                    topSpacing = style.blockSpacing;
                } else if (previousElementType != MarkdownElementType::Unknown && previousElementType != MarkdownElementType::LineBreak) {
                    // No spacing between consecutive content elements
                    topSpacing = 0.0f;
                } else {
                    // Apply appropriate margins for specific element types
                    switch (element->type) {
                        case MarkdownElementType::Header:
                            topSpacing = style.headerMarginTop[std::clamp(element->level - 1, 0, 5)];
                            break;
                        case MarkdownElementType::CodeBlock:
                        case MarkdownElementType::Quote:
                            topSpacing = style.blockSpacing;
                            break;
                        default:
                            topSpacing = 0.0f;
                            break;
                    }
                }

                currentY += topSpacing;

                // Calculate element bounds
                int elementHeight = CalculateElementHeight(ctx, element, maxWidth);
                element->bounds = Rect2Di(
                        GetX() + 10,
                        currentY,
                        maxWidth,
                        elementHeight
                );

                currentY += elementHeight;

                // Add bottom spacing for specific elements
                switch (element->type) {
                    case MarkdownElementType::Header:
                        currentY += style.headerMarginBottom[std::clamp(element->level - 1, 0, 5)];
                        break;
                    case MarkdownElementType::Paragraph:
                        currentY += style.paragraphSpacing;
                        break;
                    default:
                        break;
                }

                // Track element type for next iteration (skip LineBreak elements)
                if (element->type != MarkdownElementType::LineBreak) {
                    previousElementType = element->type;
                }
            }

            contentHeight = currentY - GetY();
        }

        int CalculateElementHeight(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element, int maxWidth) {
            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);

            switch (element->type) {
                case MarkdownElementType::Header: {
                    int level = std::clamp(element->level - 1, 0, 5);
                    ctx->SetFontSize(style.headerSizes[level]);
                    int w, h;
                    ctx->GetTextLineDimensions(element->text, w, h);
                    return h + 10.0f;
                }

                case MarkdownElementType::Paragraph: {
                    ctx->SetFontSize(style.fontSize);
                    return CalculateWrappedTextHeight(ctx, element->text, maxWidth);
                }

                case MarkdownElementType::CodeBlock: {
                    ctx->SetFontSize(style.codeFontSize);
                    int lineCount = 1;
                    for (char c : element->text) {
                        if (c == '\n') lineCount++;
                    }
                    return lineCount * static_cast<float>(style.codeFontSize) * style.lineHeight * 1.2 +
                           style.codeBlockPadding * 2;
                }

                case MarkdownElementType::Quote: {
                    ctx->SetFontSize(style.fontSize);
                    return CalculateWrappedTextHeight(ctx, element->text, maxWidth - style.quoteIndent) + 20;
                }

                case MarkdownElementType::ListItem: {
                    ctx->SetFontSize(style.fontSize);
                    return CalculateWrappedTextHeight(ctx, element->text, maxWidth - style.listIndent) +
                           style.listItemSpacing;
                }

                case MarkdownElementType::HorizontalRule:
                    return style.horizontalRuleHeight + 20;

                case MarkdownElementType::LineBreak:
                    return style.blockSpacing;

                default:
                    return 20;
            }
        }

        int CalculateWrappedTextHeight(IRenderContext* ctx, const std::string& text, int maxWidth) {
            if (text.empty()) return 0;

            int w, h;
            ctx->GetTextDimensions(text, maxWidth, 0, w, h);
            return h;
        }

        Rect2Di GetAdjustedBounds(const Rect2Di& bounds) {
            return Rect2Di(
                    bounds.x,
                    bounds.y - scrollOffset,
                    bounds.width,
                    bounds.height
            );
        }

        Point2Di GetAdjustedPosition(const Rect2Di& bounds) {
            return Point2Di(bounds.x, bounds.y - scrollOffset);
        }

        void RenderElement(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            // Skip if element is scrolled out of view
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);
            if (adjustedBounds.y + adjustedBounds.height < GetY() ||
                adjustedBounds.y > GetY() + GetHeight()) {
                return;
            }
            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);

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
            ctx->DrawFilledRectangle(adjustedBounds, style.codeBlockBackgroundColor, style.codeBlockBorderWidth, style.codeBlockBorderColor, 0);

            // Draw code text (temporarily disable markup for code)
            ctx->SetTextIsMarkup(false);
            ctx->SetFontFace(style.codeFont, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.codeFontSize);
            ctx->SetTextPaint(style.codeTextColor);

            Point2Di textPos(
                    adjustedBounds.x + style.codeBlockPadding,
                    adjustedBounds.y + style.codeBlockPadding
            );

            // Split into lines and render
            std::istringstream stream(element->text);
            std::string line;
            int lineY = textPos.y;

            while (std::getline(stream, line)) {
                ctx->DrawText(line, textPos.x, lineY);
                lineY += static_cast<float>(style.codeFontSize) * style.lineHeight * 1.2;
            }
        }

        void RenderQuote(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

            // Draw background
            ctx->SetFillPaint(style.quoteBackgroundColor);
            ctx->FillRectangle(adjustedBounds.x, adjustedBounds.y,
                               adjustedBounds.width, adjustedBounds.height);

            // Draw left bar
            ctx->SetFillPaint(style.quoteBarColor);
            ctx->FillRectangle(adjustedBounds.x, adjustedBounds.y,
                               static_cast<int>(style.quoteBarWidth), adjustedBounds.height);

            // Draw text
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(style.textColor);

            Rect2Di textBounds(
                    adjustedBounds.x + style.quoteIndent,
                    adjustedBounds.y + 10,
                    adjustedBounds.width - style.quoteIndent,
                    adjustedBounds.height - 20
            );

            DrawTextWrapped(ctx, element->text, textBounds, style.lineHeight);
        }

        void RenderListItem(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(style.textColor);

            // Draw bullet or number
            Point2Di bulletPos(adjustedBounds.x, adjustedBounds.y);

            if (element->ordered) {
                // Draw number (would need to track item index in real implementation)
                ctx->DrawText("1.", bulletPos);
            } else {
                // Draw bullet
                ctx->SetTextPaint(style.bulletColor);
                ctx->DrawText(style.bulletCharacter, bulletPos);
            }

            // Draw item text
            ctx->SetTextPaint(style.textColor);
            Rect2Di textBounds(
                    adjustedBounds.x + style.listIndent,
                    adjustedBounds.y,
                    adjustedBounds.width - style.listIndent,
                    adjustedBounds.height
            );

            DrawTextWrapped(ctx, element->text, textBounds, style.lineHeight);
        }

        void RenderHorizontalRule(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

            ctx->SetStrokePaint(style.horizontalRuleColor);
            ctx->SetStrokeWidth(style.horizontalRuleHeight);
            ctx->DrawLine(
                    adjustedBounds.x,
                    adjustedBounds.y + adjustedBounds.height / 2,
                    adjustedBounds.x + adjustedBounds.width,
                    adjustedBounds.y + adjustedBounds.height / 2
            );
            ctx->Stroke();
        }

        void RenderLink(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Point2Di position = GetAdjustedPosition(element->bounds);

            bool isVisited = std::find(visitedLinks.begin(), visitedLinks.end(), element->url) != visitedLinks.end();
            bool isHovered = (hoveredElement == element);

            Color linkColor = isHovered ? style.linkHoverColor :
                              (isVisited ? style.linkVisitedColor : style.linkColor);

            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(linkColor);
            ctx->DrawText(element->text, position);

            if (style.linkUnderline) {
                int w, h;
                ctx->GetTextLineDimensions(element->text, w, h);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(position.x, position.y + h,
                              position.x + w, position.y + h, linkColor);
            }
        }

        void RenderText(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
            Point2Di position = GetAdjustedPosition(element->bounds);

            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(style.textColor);
            ctx->DrawText(element->text, position);
        }

        void DrawTextWrapped(IRenderContext* ctx, const std::string& text, const Rect2Di& bounds, float lineHeight) {
            if (text.empty()) return;

            ctx->DrawTextInRect(text, bounds.x, bounds.y, bounds.width, bounds.height);
        }

        void DrawScrollbar(IRenderContext* ctx) {
            int scrollbarWidth = 12;
            int scrollbarX = GetX() + GetWidth() - scrollbarWidth - 4;
            int scrollbarY = GetY() + 4;
            int scrollbarHeight = GetHeight() - 8;

            // Draw track
            ctx->SetFillPaint(style.scrollbarTrackColor);
            ctx->FillRectangle(scrollbarX, scrollbarY,
                               scrollbarWidth, scrollbarHeight);

            // Calculate thumb size and position
            int visibleRatio = static_cast<float>(GetHeight()) / static_cast<float>(contentHeight);
            int thumbHeight = scrollbarHeight * visibleRatio;
            int maxScroll = contentHeight - GetHeight();
            int thumbY = scrollbarY + (scrollOffset / maxScroll) * (scrollbarHeight - thumbHeight);

            // Draw thumb
            ctx->SetFillPaint(style.scrollbarColor);
            ctx->FillRectangle(scrollbarX, thumbY, scrollbarWidth, thumbHeight);
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!properties.Active || !properties.Visible) return false;

            switch (event.type) {
                case UCEventType::MouseWheel:
                    return HandleMouseWheel(event);

                case UCEventType::MouseDown:
                    return HandleMouseDown(event);

                case UCEventType::MouseMove:
                    return HandleMouseMove(event);

                default:
                    return false;
            }
        }

        bool HandleMouseWheel(const UCEvent& event) {
            int delta = event.wheelDelta * 20;
            int newOffset = scrollOffset - delta;
            SetScrollOffset(newOffset);

            if (onScrollChanged) {
                onScrollChanged(scrollOffset);
            }

            return true;
        }

        bool HandleMouseDown(const UCEvent& event) {
            // Check if clicked on a link
            for (auto& element : elements) {
                if (element->clickable && element->bounds.Contains(event.x, event.y)) {
                    if (element->type == MarkdownElementType::Link && onLinkClicked) {
                        onLinkClicked(element->url);
                        visitedLinks.push_back(element->url);
                        RequestRedraw();
                        return true;
                    }
                }
            }

            return false;
        }

        bool HandleMouseMove(const UCEvent& event) {
            std::shared_ptr<MarkdownElement> newHovered = nullptr;

            // Find hovered element
            for (auto& element : elements) {
                if (element->clickable && element->bounds.Contains(event.x, event.y)) {
                    newHovered = element;
                    break;
                }
            }

            if (newHovered != hoveredElement) {
                hoveredElement = newHovered;
                RequestRedraw();
                return true;
            }

            return false;
        }
    };

// ===== FACTORY FUNCTION =====
    inline std::shared_ptr<UltraCanvasMarkdownDisplay> CreateMarkdownDisplay(
            const std::string& identifier = "MarkdownDisplay",
            long id = 0, long x = 0, long y = 0, long w = 400, long h = 300) {

        return std::make_shared<UltraCanvasMarkdownDisplay>(identifier, id, x, y, w, h);
    }

} // namespace UltraCanvas