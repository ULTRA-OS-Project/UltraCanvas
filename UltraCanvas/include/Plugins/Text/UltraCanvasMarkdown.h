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
#include <unordered_map>
#include <functional>
#include <sstream>
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
        std::string fontFamily = "Sans";
        float fontSize = 10.0f;
        Color textColor = Color(50, 50, 50);
        Color backgroundColor = Colors::White;
        float lineHeight = 1.2f;

        // Header styles
        std::array<float, 6> headerSizes = {20.0f, 18.0f, 16.0f, 14.0f, 12.0f, 10.0f};
        std::array<Color, 6> headerColors = {
                Color(20, 20, 20), Color(30, 30, 30), Color(40, 40, 40),
                Color(50, 50, 50), Color(60, 60, 60), Color(70, 70, 70)
        };
        std::array<float, 6> headerMarginTop = {9.0f, 8.0f, 7.0f, 6.0f, 5.0f, 3.0f};
        std::array<float, 6> headerMarginBottom = {6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};

        // Code styling
        std::string codeFont = "Consolas";
        float codeFontSize = 10.0f;
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
        float paragraphSpacing = 6.0f;
        float blockSpacing = 4.0f;

        // Horizontal rule
        Color horizontalRuleColor = Color(200, 200, 200);
        float horizontalRuleHeight = 2.0f;

        // Scrollbar
        int scrollbarWidth = 16;
        Color scrollbarColor = Color(180, 180, 180);
        Color scrollbarTrackColor = Color(240, 240, 240);
        Color scrollbarPressedColor = Color(90, 90, 90);
        Color scrollbarHoverColor = Color(120, 120, 120);

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
        int orderNumber = 0;          // For headers (1-6) and list nesting
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

        static std::shared_ptr<MarkdownElement> CreateText(const std::string& content);
        static std::shared_ptr<MarkdownElement> CreateHeader(int headerLevel, const std::string& content);
        static std::shared_ptr<MarkdownElement> CreateLink(const std::string& text, const std::string& url);
        static std::shared_ptr<MarkdownElement> CreateCodeBlock(const std::string& code, const std::string& language = "");
    };

// ===== INLINE STYLE CONVERSION FOR PANGO MARKUP =====
    class MarkdownInlineConverter {
    public:
        // Convert markdown inline styles to Pango markup
        static std::string ConvertToPangoMarkup(const std::string& text);

    private:
        // Escape XML special characters
        static std::string EscapeXml(const std::string& text);

        static std::string ConvertBoldItalic(const std::string& text);

        // Convert **text** or __text__ to <b>text</b>
        static std::string ConvertBold(const std::string& text);

        // Convert *text* or _text_ to <i>text</i>
        static std::string ConvertItalic(const std::string& text);

        // Convert `code` to <tt>code</tt>
        static std::string ConvertInlineCode(const std::string& text);

        // Convert ~~text~~ to <s>text</s>
        static std::string ConvertStrikethrough(const std::string& text);
    };

// ===== MARKDOWN PARSER =====
    class MarkdownParser {
    public:
        static std::vector<std::shared_ptr<MarkdownElement>> Parse(const std::string& markdown);

    private:
        static std::shared_ptr<MarkdownElement> ParseLine(const std::string& line, const std::string& previousLine);

        static std::shared_ptr<MarkdownElement> ParseHeader(const std::string& line);

        static std::shared_ptr<MarkdownElement> ParseQuote(const std::string& line);

        // FIXED: Proper list item detection with space requirement
        static bool IsListItem(const std::string& line);

        static std::shared_ptr<MarkdownElement> ParseListItem(const std::string& line);

        static std::shared_ptr<MarkdownElement> ParseParagraph(const std::string& line);

        static bool IsHorizontalRule(const std::string& line);

        static std::string TrimWhitespace(const std::string& str);
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

// Scrollbar interaction state
        Rect2Di scrollbarTrackRect;
        Rect2Di scrollbarThumbRect;
        bool isHoveringScrollbar = false;
        bool isHoveringThumb = false;
        bool isDraggingThumb = false;
        int dragStartY = 0;
        int dragStartScrollOffset = 0;

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
        void SetMarkdownText(const std::string& markdown);

        const std::string& GetMarkdownText() const {
            return markdownText;
        }

        // ===== STYLING =====
        void SetStyle(const MarkdownStyle& newStyle);

        const MarkdownStyle& GetStyle() const {
            return style;
        }

        // ===== SCROLLING =====
        void ScrollTo(int offset);

        void ScrollBy(int delta);

        int GetScrollOffset() const {
            return verticalScrollOffset;
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
        void Render(IRenderContext* ctx) override;

    private:
        void PerformLayout(IRenderContext* ctx);
        int CalculateElementHeight(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element, int maxWidth);
        int CalculateWrappedTextHeight(IRenderContext* ctx, const std::string& text, int maxWidth);
        Rect2Di GetAdjustedBounds(const Rect2Di& bounds);
        Point2Di GetAdjustedPosition(const Rect2Di& bounds);
        void RenderElement(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderHeader(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderParagraph(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderCodeBlock(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderQuote(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderListItem(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderHorizontalRule(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderLink(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void RenderText(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element);
        void DrawTextWrapped(IRenderContext* ctx, const std::string& text, const Rect2Di& bounds, float lineHeight);
        void UpdateScrollbarGeometry(const Rect2Di& bounds);
        void DrawScrollbar(IRenderContext* ctx);

// ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;
        std::shared_ptr<MarkdownElement> FindElementAtPosition(int x, int y);
        bool IsLinkVisited(const std::string& url) const;
        void MarkLinkAsVisited(const std::string& url);
    };

// ===== FACTORY FUNCTION =====
    inline std::shared_ptr<UltraCanvasMarkdownDisplay> CreateMarkdownDisplay(
            const std::string& identifier = "MarkdownDisplay",
            long id = 0, long x = 0, long y = 0, long w = 400, long h = 300) {

        return std::make_shared<UltraCanvasMarkdownDisplay>(identifier, id, x, y, w, h);
    }

} // namespace UltraCanvas