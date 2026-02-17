// UltraCanvas/core/UltraCanvasTextArea_Markdown.cpp
// Markdown hybrid rendering enhancement for TextArea
// Shows current line as plain text, all other lines as formatted markdown
// Version: 2.0.0
// Last Modified: 2026-02-15
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUtils.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <regex>

namespace UltraCanvas {

// ===== MARKDOWN HYBRID STYLE CONFIGURATION =====
// Configurable styling for markdown rendering within TextArea
// Mirrors relevant properties from MarkdownStyle in UltraCanvasMarkdown.h

struct MarkdownHybridStyle {
    // Header colors per level (H1-H6)
    std::array<Color, 6> headerColors = {
            Color(20, 20, 20), Color(30, 30, 30), Color(40, 40, 40),
            Color(50, 50, 50), Color(60, 60, 60), Color(70, 70, 70)
    };

    // Header font size multipliers (relative to base font size)
    std::array<float, 6> headerSizeMultipliers = {1.1f, 1.1f, 1.1f, 1.0f, 1.0f, 1.0f};

    // Code styling
    Color codeTextColor = Color(200, 50, 50);
    Color codeBackgroundColor = Color(245, 245, 245);
    Color codeBlockBackgroundColor = Color(248, 248, 248);
    Color codeBlockBorderColor = Color(220, 220, 220);
    std::string codeFont = "Courier New";

    // Link styling
    Color linkColor = Color(0, 102, 204);
    Color linkHoverColor = Color(0, 80, 160);
    bool linkUnderline = true;

    // List styling
    int listIndent = 20;
    std::string bulletCharacter = "\xe2\x80\xa2"; // UTF-8 bullet •
    Color bulletColor = Color(80, 80, 80);

    // Quote styling
    Color quoteBarColor = Color(200, 200, 200);
    Color quoteBackgroundColor = Color(250, 250, 250);
    Color quoteTextColor = Color(100, 100, 100);
    int quoteBarWidth = 4;
    int quoteIndent = 15;

    // Horizontal rule
    Color horizontalRuleColor = Color(200, 200, 200);
    float horizontalRuleHeight = 2.0f;

    // Table styling
    Color tableBorderColor = Color(200, 200, 200);
    Color tableHeaderBackground = Color(240, 240, 240);
    float tableCellPadding = 4.0f;

    // Strikethrough
    Color strikethroughColor = Color(120, 120, 120);

    // Highlight
    Color highlightBackground = Color(255, 255, 0, 100);

    // Task list checkbox
    Color checkboxBorderColor = Color(150, 150, 150);
    Color checkboxCheckedColor = Color(0, 120, 215);
    Color checkboxCheckmarkColor = Color(255, 255, 255);
    int checkboxSize = 12;

    // Image placeholder
    Color imagePlaceholderBackground = Color(230, 230, 240);
    Color imagePlaceholderBorderColor = Color(180, 180, 200);
    Color imagePlaceholderTextColor = Color(100, 100, 140);

    static MarkdownHybridStyle Default() {
        return MarkdownHybridStyle();
    }

    static MarkdownHybridStyle DarkMode() {
        MarkdownHybridStyle s;
        s.headerColors = {
                Color(240, 240, 240), Color(220, 220, 240), Color(200, 200, 220),
                Color(190, 190, 210), Color(180, 180, 200), Color(170, 170, 190)
        };
        s.codeTextColor = Color(255, 150, 150);
        s.codeBackgroundColor = Color(50, 50, 50);
        s.codeBlockBackgroundColor = Color(40, 40, 40);
        s.codeBlockBorderColor = Color(80, 80, 80);
        s.linkColor = Color(100, 180, 255);
        s.linkHoverColor = Color(150, 200, 255);
        s.quoteBarColor = Color(100, 100, 100);
        s.quoteBackgroundColor = Color(40, 40, 40);
        s.quoteTextColor = Color(170, 170, 170);
        s.horizontalRuleColor = Color(100, 100, 100);
        s.tableBorderColor = Color(80, 80, 80);
        s.tableHeaderBackground = Color(50, 50, 50);
        s.strikethroughColor = Color(170, 170, 170);
        s.highlightBackground = Color(100, 100, 0, 100);
        s.bulletColor = Color(170, 170, 170);
        s.checkboxBorderColor = Color(120, 120, 120);
        s.imagePlaceholderBackground = Color(50, 50, 60);
        s.imagePlaceholderBorderColor = Color(80, 80, 100);
        s.imagePlaceholderTextColor = Color(140, 140, 170);
        return s;
    }
};


// ===== MARKDOWN INLINE ELEMENT =====
// Parsed inline element with formatting state

struct MarkdownInlineElement {
    std::string text;
    bool isBold = false;
    bool isItalic = false;
    bool isCode = false;
    bool isStrikethrough = false;
    bool isHighlight = false;
    bool isLink = false;
    bool isImage = false;
    std::string url;
    std::string altText;
};

// ===== TABLE COLUMN INFO =====
// Parsed table column alignment and widths

enum class TableColumnAlignment {
    Left,
    Center,
    Right
};

struct TableParseResult {
    std::vector<std::string> cells;
    bool isValid = false;
};

// ===== MARKDOWN INLINE RENDERER =====
// Renders markdown inline formatting directly in the text area

struct MarkdownInlineRenderer {

    // ---------------------------------------------------------------
    // INLINE PARSER — handles bold, italic, bold+italic, code,
    //                 strikethrough, highlight, links, images
    // ---------------------------------------------------------------

    static std::vector<MarkdownInlineElement> ParseInlineMarkdown(const std::string& line) {
        std::vector<MarkdownInlineElement> elements;
        size_t pos = 0;
        size_t len = line.length();

        while (pos < len) {
            MarkdownInlineElement elem;
            bool parsed = false;

            // --- Image: ![alt](url) ---
            if (pos + 1 < len && line[pos] == '!' && line[pos + 1] == '[') {
                size_t altEnd = line.find(']', pos + 2);
                if (altEnd != std::string::npos && altEnd + 1 < len && line[altEnd + 1] == '(') {
                    size_t urlEnd = line.find(')', altEnd + 2);
                    if (urlEnd != std::string::npos) {
                        elem.isImage = true;
                        elem.altText = line.substr(pos + 2, altEnd - pos - 2);
                        elem.url = line.substr(altEnd + 2, urlEnd - altEnd - 2);
                        elem.text = elem.altText.empty() ? "[image]" : elem.altText;
                        elements.push_back(elem);
                        pos = urlEnd + 1;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Link: [text](url) ---
            if (line[pos] == '[') {
                size_t textEnd = line.find(']', pos + 1);
                if (textEnd != std::string::npos && textEnd + 1 < len && line[textEnd + 1] == '(') {
                    size_t urlEnd = line.find(')', textEnd + 2);
                    if (urlEnd != std::string::npos) {
                        elem.isLink = true;
                        elem.text = line.substr(pos + 1, textEnd - pos - 1);
                        elem.url = line.substr(textEnd + 2, urlEnd - textEnd - 2);
                        elements.push_back(elem);
                        pos = urlEnd + 1;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Inline code: `text` ---
            if (line[pos] == '`' && (pos + 1 >= len || line[pos + 1] != '`')) {
                size_t end = line.find('`', pos + 1);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 1, end - pos - 1);
                    elem.isCode = true;
                    elements.push_back(elem);
                    pos = end + 1;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Bold+Italic: ***text*** or ___text___ ---
            if (pos + 2 < len) {
                std::string triple = line.substr(pos, 3);
                if (triple == "***" || triple == "___") {
                    char marker = triple[0];
                    std::string closeMarker(3, marker);
                    size_t end = line.find(closeMarker, pos + 3);
                    if (end != std::string::npos) {
                        elem.text = line.substr(pos + 3, end - pos - 3);
                        elem.isBold = true;
                        elem.isItalic = true;
                        elements.push_back(elem);
                        pos = end + 3;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Strikethrough: ~~text~~ ---
            if (pos + 1 < len && line[pos] == '~' && line[pos + 1] == '~') {
                size_t end = line.find("~~", pos + 2);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 2, end - pos - 2);
                    elem.isStrikethrough = true;
                    elements.push_back(elem);
                    pos = end + 2;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Highlight: ==text== ---
            if (pos + 1 < len && line[pos] == '=' && line[pos + 1] == '=') {
                size_t end = line.find("==", pos + 2);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 2, end - pos - 2);
                    elem.isHighlight = true;
                    elements.push_back(elem);
                    pos = end + 2;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Bold: **text** or __text__ ---
            if (pos + 1 < len) {
                char c = line[pos];
                if ((c == '*' || c == '_') && line[pos + 1] == c) {
                    std::string closeMarker(2, c);
                    size_t end = line.find(closeMarker, pos + 2);
                    if (end != std::string::npos) {
                        elem.text = line.substr(pos + 2, end - pos - 2);
                        elem.isBold = true;
                        elements.push_back(elem);
                        pos = end + 2;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Italic: *text* or _text_ ---
            if (line[pos] == '*' || line[pos] == '_') {
                char marker = line[pos];
                // Ensure not a double marker (already handled above)
                if (pos + 1 < len && line[pos + 1] != marker) {
                    size_t end = pos + 1;
                    while (end < len) {
                        if (line[end] == marker) {
                            // Check it's not a double marker
                            bool isDouble = (end + 1 < len && line[end + 1] == marker);
                            if (!isDouble) {
                                elem.text = line.substr(pos + 1, end - pos - 1);
                                elem.isItalic = true;
                                elements.push_back(elem);
                                pos = end + 1;
                                parsed = true;
                                break;
                            } else {
                                end += 2;
                                continue;
                            }
                        }
                        end++;
                    }
                }
            }

            if (parsed) continue;

            // --- Plain text: consume until next markdown marker ---
            {
                size_t nextMarker = std::string::npos;
                // Search for the nearest markdown marker
                const char* markers[] = {"***", "___", "**", "__", "~~", "==", "`", "*", "_", "[", "!["};
                for (const char* m : markers) {
                    size_t found = line.find(m, pos + 1);
                    if (found != std::string::npos && found < nextMarker) {
                        nextMarker = found;
                    }
                }

                if (nextMarker == std::string::npos || nextMarker <= pos) {
                    elem.text = line.substr(pos);
                    elements.push_back(elem);
                    break;
                } else {
                    elem.text = line.substr(pos, nextMarker - pos);
                    elements.push_back(elem);
                    pos = nextMarker;
                }
            }
        }

        return elements;
    }

    // ---------------------------------------------------------------
    // INLINE LINE RENDERER — renders parsed inline elements
    // ---------------------------------------------------------------

    static int RenderMarkdownLine(IRenderContext* ctx, const std::string& line,
                                  int x, int y, int lineHeight,
                                  const TextAreaStyle& style,
                                  const MarkdownHybridStyle& mdStyle,
                                  std::vector<MarkdownHitRect>* hitRects = nullptr) {

        std::vector<MarkdownInlineElement> elements = ParseInlineMarkdown(line);
        int currentX = x;

        for (const auto& elem : elements) {
            if (elem.text.empty() && !elem.isImage) continue;

            // --- Image element ---
            if (elem.isImage) {
                int imgSize = lineHeight - 2;
                int imgY = y + 1;

                // Draw image placeholder/thumbnail
                ctx->SetFillPaint(mdStyle.imagePlaceholderBackground);
                ctx->FillRectangle(currentX, imgY, imgSize, imgSize);
                ctx->SetStrokePaint(mdStyle.imagePlaceholderBorderColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(currentX, imgY, imgSize, imgSize);

                // Draw small icon indicator in center
                int iconCenterX = currentX + imgSize / 2;
                int iconCenterY = imgY + imgSize / 2;
                int iconR = std::max(2, imgSize / 6);
                ctx->SetFillPaint(mdStyle.imagePlaceholderTextColor);
                // Mountain icon: small triangle
                ctx->ClearPath();
                ctx->MoveTo(currentX + 2, imgY + imgSize - 2);
                ctx->LineTo(iconCenterX, imgY + 3);
                ctx->LineTo(currentX + imgSize - 2, imgY + imgSize - 2);
                ctx->ClosePath();
                ctx->Fill();

                // Store hit rect for click interaction
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, imgY, imgSize, imgSize};
                    hr.url = elem.url;
                    hr.altText = elem.altText;
                    hr.isImage = true;
                    hitRects->push_back(hr);
                }

                currentX += imgSize + 4;

                // Draw alt text after image icon
                if (!elem.altText.empty()) {
                    ctx->SetFontWeight(FontWeight::Normal);
                    ctx->SetFontSlant(FontSlant::Italic);
                    ctx->SetTextPaint(mdStyle.imagePlaceholderTextColor);
                    ctx->DrawText(elem.altText, currentX, y);
                    currentX += ctx->GetTextLineWidth(elem.altText) + 2;
                    ctx->SetFontSlant(FontSlant::Normal);
                }
                continue;
            }

            // --- Link element ---
            if (elem.isLink) {
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(mdStyle.linkColor);
                int textWidth = ctx->GetTextLineWidth(elem.text);

                ctx->DrawText(elem.text, currentX, y);

                // Draw underline
                if (mdStyle.linkUnderline) {
                    int underlineY = y + lineHeight - 3;
                    ctx->SetStrokePaint(mdStyle.linkColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine(currentX, underlineY, currentX + textWidth, underlineY);
                }

                // Store hit rect
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, y, textWidth, lineHeight};
                    hr.url = elem.url;
                    hr.isImage = false;
                    hitRects->push_back(hr);
                }

                currentX += textWidth;
                continue;
            }

            // --- Determine formatting ---
            FontWeight weight = elem.isBold ? FontWeight::Bold : FontWeight::Normal;
            FontSlant slant = elem.isItalic ? FontSlant::Italic : FontSlant::Normal;
            Color color = style.fontColor;

            if (elem.isCode) {
                color = mdStyle.codeTextColor;
            }

            ctx->SetFontWeight(weight);
            ctx->SetFontSlant(slant);

            int textWidth = ctx->GetTextLineWidth(elem.text);

            // --- Highlight background ---
            if (elem.isHighlight) {
                ctx->SetFillPaint(mdStyle.highlightBackground);
                ctx->FillRectangle(currentX - 1, y, textWidth + 2, lineHeight);
            }

            // --- Code background ---
            if (elem.isCode) {
                ctx->SetFillPaint(mdStyle.codeBackgroundColor);
                ctx->FillRoundedRectangle(currentX - 2, y + 1, textWidth + 4, lineHeight - 2, 3);
                ctx->SetFontFamily(mdStyle.codeFont);
            }

            // --- Draw the text ---
            ctx->SetTextPaint(elem.isStrikethrough ? mdStyle.strikethroughColor : color);
            ctx->DrawText(elem.text, currentX, y);

            // --- Strikethrough line ---
            if (elem.isStrikethrough) {
                int strikeY = y + lineHeight / 2;
                ctx->SetStrokePaint(mdStyle.strikethroughColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(currentX, strikeY, currentX + textWidth, strikeY);
            }

            // --- Restore font if code changed it ---
            if (elem.isCode) {
                ctx->SetFontFamily(style.fontStyle.fontFamily);
            }

            currentX += textWidth;
        }

        // Reset font state
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);

        return currentX - x; // Return total width rendered
    }

    // ---------------------------------------------------------------
    // HEADER RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownHeader(IRenderContext* ctx, const std::string& line,
                                     int x, int y, int lineHeight,
                                     const TextAreaStyle& style,
                                     const MarkdownHybridStyle& mdStyle,
                                     std::vector<MarkdownHitRect>* hitRects = nullptr) {
        // Detect header level
        int level = 0;
        size_t pos = 0;
        while (pos < line.length() && line[pos] == '#') {
            level++;
            pos++;
        }

        if (level == 0 || level > 6) {
            RenderMarkdownLine(ctx, line, x, y, lineHeight, style, mdStyle, hitRects);
            return;
        }

        // Skip whitespace after #
        while (pos < line.length() && line[pos] == ' ') pos++;

        std::string headerText = line.substr(pos);
        int levelIndex = std::clamp(level - 1, 0, 5);

        // Set header font size — clamped to not exceed line height
        float baseFontSize = style.fontStyle.fontSize;
        //float headerFontSize = baseFontSize;
        float headerFontSize = baseFontSize * mdStyle.headerSizeMultipliers[levelIndex];

        // Clamp font size so it doesn't overflow the fixed row height
        float maxFontSize = static_cast<float>(lineHeight) * 0.85f;
        headerFontSize = std::min(headerFontSize, maxFontSize);

        ctx->SetFontSize(headerFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(mdStyle.headerColors[levelIndex]);

        // Render inline markdown within header text (links, bold, etc.)
        // For headers, we render the text directly since inline formatting
        // within headers is a rare edge case and the header already has styling
        ctx->DrawText(headerText, x, y);

        // Draw underline for H1 and H2 (visual distinction)
        if (level <= 2) {
            int underlineY = y + lineHeight - 2;
            ctx->SetStrokePaint(mdStyle.horizontalRuleColor);
            ctx->SetStrokeWidth(level == 1 ? 2.0f : 1.0f);
            // We don't know the full width here, so use text width + some padding
            int textW = ctx->GetTextLineWidth(headerText);
            ctx->DrawLine(x, underlineY, x + textW + 20, underlineY);
        }

        // Restore font
        ctx->SetFontSize(baseFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
    }

    // ---------------------------------------------------------------
    // LIST ITEM RENDERER — unordered, ordered, and task lists
    // ---------------------------------------------------------------

    static void RenderMarkdownListItem(IRenderContext* ctx, const std::string& line,
                                       int x, int y, int lineHeight,
                                       const TextAreaStyle& style,
                                       const MarkdownHybridStyle& mdStyle,
                                       std::vector<MarkdownHitRect>* hitRects = nullptr) {
        size_t pos = 0;

        // Count leading whitespace for nesting depth
        int indent = 0;
        while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) {
            if (line[pos] == '\t') indent += 4;
            else indent++;
            pos++;
        }
        int nestingLevel = indent / 2;

        bool isOrdered = false;
        bool isTaskList = false;
        bool isTaskChecked = false;
        int orderNumber = 0;

        // Check for ordered list: digits followed by ". "
        if (pos < line.length() && std::isdigit(line[pos])) {
            size_t numStart = pos;
            while (pos < line.length() && std::isdigit(line[pos])) pos++;
            if (pos < line.length() && line[pos] == '.') {
                orderNumber = std::stoi(line.substr(numStart, pos - numStart));
                isOrdered = true;
                pos++; // skip '.'
            } else {
                pos = numStart; // reset, not a valid ordered list
            }
        }

        // Check for unordered list: -, *, +
        if (!isOrdered && pos < line.length() &&
            (line[pos] == '-' || line[pos] == '*' || line[pos] == '+')) {
            pos++; // skip marker
        }

        // Skip whitespace after marker
        while (pos < line.length() && line[pos] == ' ') pos++;

        // Check for task list: [ ] or [x] or [X]
        if (pos + 2 < line.length() && line[pos] == '[') {
            char check = line[pos + 1];
            if (line[pos + 2] == ']' && (check == ' ' || check == 'x' || check == 'X')) {
                isTaskList = true;
                isTaskChecked = (check == 'x' || check == 'X');
                pos += 3;
                // Skip space after ]
                while (pos < line.length() && line[pos] == ' ') pos++;
            }
        }

        std::string itemText = (pos < line.length()) ? line.substr(pos) : "";

        // Calculate x offset based on nesting
        int bulletX = x + (nestingLevel * mdStyle.listIndent);

        // --- Draw bullet/number/checkbox ---
        if (isTaskList) {
            // Draw checkbox
            int cbSize = mdStyle.checkboxSize;
            int cbX = bulletX;
            int cbY = y + (lineHeight - cbSize) / 2;

            ctx->SetStrokePaint(mdStyle.checkboxBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(cbX, cbY, cbSize, cbSize);

            if (isTaskChecked) {
                // Fill checkbox
                ctx->SetFillPaint(mdStyle.checkboxCheckedColor);
                ctx->FillRectangle(cbX + 1, cbY + 1, cbSize - 2, cbSize - 2);

                // Draw checkmark
                ctx->SetStrokePaint(mdStyle.checkboxCheckmarkColor);
                ctx->SetStrokeWidth(2.0f);
                int cx = cbX + cbSize / 2;
                int cy = cbY + cbSize / 2;
                ctx->DrawLine(cbX + 3, cy, cx - 1, cbY + cbSize - 3);
                ctx->DrawLine(cx - 1, cbY + cbSize - 3, cbX + cbSize - 3, cbY + 3);
            }

            bulletX += cbSize + 6;
        } else if (isOrdered) {
            // Draw order number
            std::string numberStr = std::to_string(orderNumber) + ".";
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(mdStyle.bulletColor);
            ctx->DrawText(numberStr, bulletX, y);
            int numWidth = ctx->GetTextLineWidth(numberStr);
            bulletX += numWidth + 4;
        } else {
            // Draw bullet character
            ctx->SetTextPaint(mdStyle.bulletColor);

            // Alternate bullet style per nesting level
            if (nestingLevel % 3 == 0) {
                // Filled circle (bullet character)
                ctx->DrawText(mdStyle.bulletCharacter, bulletX, y);
            } else if (nestingLevel % 3 == 1) {
                // Open circle
                int circleR = 3;
                int circleY = y + lineHeight / 2;
                ctx->SetStrokePaint(mdStyle.bulletColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawCircle(bulletX + circleR + 1, circleY, circleR);
            } else {
                // Small filled square
                int sqSize = 5;
                int sqY = y + (lineHeight - sqSize) / 2;
                ctx->SetFillPaint(mdStyle.bulletColor);
                ctx->FillRectangle(bulletX + 1, sqY, sqSize, sqSize);
            }

            int bulletCharWidth = ctx->GetTextLineWidth(mdStyle.bulletCharacter);
            bulletX += bulletCharWidth + 4;
        }

        // --- Draw item text with inline formatting ---
        if (isTaskChecked) {
            // Render checked items with strikethrough styling
            ctx->SetTextPaint(mdStyle.strikethroughColor);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->DrawText(itemText, bulletX, y);
            int textWidth = ctx->GetTextLineWidth(itemText);
            int strikeY = y + lineHeight / 2;
            ctx->SetStrokePaint(mdStyle.strikethroughColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(bulletX, strikeY, bulletX + textWidth, strikeY);
        } else {
            RenderMarkdownLine(ctx, itemText, bulletX, y, lineHeight, style, mdStyle, hitRects);
        }
    }

    // ---------------------------------------------------------------
    // BLOCKQUOTE RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownBlockquote(IRenderContext* ctx, const std::string& line,
                                         int x, int y, int lineHeight, int width,
                                         const TextAreaStyle& style,
                                         const MarkdownHybridStyle& mdStyle,
                                         std::vector<MarkdownHitRect>* hitRects = nullptr) {
        // Count nesting depth (>>)
        size_t pos = 0;
        int depth = 0;
        std::string trimmed = line;
        // Trim leading whitespace
        while (pos < trimmed.length() && trimmed[pos] == ' ') pos++;
        while (pos < trimmed.length() && trimmed[pos] == '>') {
            depth++;
            pos++;
            // Skip space after >
            if (pos < trimmed.length() && trimmed[pos] == ' ') pos++;
        }
        std::string quoteText = (pos < trimmed.length()) ? trimmed.substr(pos) : "";

        // Draw quote background
        int quoteX = x + (depth - 1) * (mdStyle.quoteBarWidth + 8);
        ctx->SetFillPaint(mdStyle.quoteBackgroundColor);
        ctx->FillRectangle(quoteX, y, width - (quoteX - x), lineHeight);

        // Draw vertical bar(s) for each nesting level
        for (int d = 0; d < depth; d++) {
            int barX = x + d * (mdStyle.quoteBarWidth + 8);
            ctx->SetFillPaint(mdStyle.quoteBarColor);
            ctx->FillRectangle(barX, y, mdStyle.quoteBarWidth, lineHeight);
        }

        // Draw text with inline formatting
        int textX = quoteX + mdStyle.quoteIndent;
        ctx->SetFontSlant(FontSlant::Italic);
        ctx->SetTextPaint(mdStyle.quoteTextColor);
        RenderMarkdownLine(ctx, quoteText, textX, y, lineHeight, style, mdStyle, hitRects);
        ctx->SetFontSlant(FontSlant::Normal);
    }

    // ---------------------------------------------------------------
    // CODE BLOCK LINE RENDERER — for lines inside ``` blocks
    // ---------------------------------------------------------------

    static void RenderMarkdownCodeBlockLine(IRenderContext* ctx, const std::string& line,
                                            int x, int y, int lineHeight, int width,
                                            const TextAreaStyle& style,
                                            const MarkdownHybridStyle& mdStyle) {
        // Draw background for code block line
        ctx->SetFillPaint(mdStyle.codeBlockBackgroundColor);
        ctx->FillRectangle(x - 4, y, width, lineHeight);

        // Draw left border accent
        ctx->SetFillPaint(mdStyle.codeBlockBorderColor);
        ctx->FillRectangle(x - 4, y, 3, lineHeight);

        // Draw code text in monospace
        ctx->SetFontFamily(mdStyle.codeFont);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);
        ctx->SetTextPaint(mdStyle.codeTextColor);
        ctx->DrawText(line, x + 4, y);

        // Restore font
        ctx->SetFontFamily(style.fontStyle.fontFamily);
    }

    // ---------------------------------------------------------------
    // CODE BLOCK DELIMITER RENDERER — for ``` lines themselves
    // ---------------------------------------------------------------

    static void RenderMarkdownCodeBlockDelimiter(IRenderContext* ctx, const std::string& line,
                                                  int x, int y, int lineHeight, int width,
                                                  const TextAreaStyle& style,
                                                  const MarkdownHybridStyle& mdStyle) {
        // Draw background
        ctx->SetFillPaint(mdStyle.codeBlockBackgroundColor);
        ctx->FillRectangle(x - 4, y, width, lineHeight);

        // Draw left border accent
        ctx->SetFillPaint(mdStyle.codeBlockBorderColor);
        ctx->FillRectangle(x - 4, y, 3, lineHeight);

        // Extract language label if present (e.g., ```cpp)
        std::string langLabel;
        if (line.length() > 3) {
            langLabel = line.substr(3);
            // Trim whitespace
            size_t start = langLabel.find_first_not_of(' ');
            if (start != std::string::npos) {
                langLabel = langLabel.substr(start);
            } else {
                langLabel.clear();
            }
        }

        if (!langLabel.empty()) {
            ctx->SetFontFamily(mdStyle.codeFont);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(mdStyle.codeBlockBorderColor);
            ctx->DrawText(langLabel, x + 4, y);
            ctx->SetFontFamily(style.fontStyle.fontFamily);
            ctx->SetFontWeight(FontWeight::Normal);
        }
    }

    // ---------------------------------------------------------------
    // HORIZONTAL RULE RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownHorizontalRule(IRenderContext* ctx,
                                             int x, int y, int lineHeight, int width,
                                             const MarkdownHybridStyle& mdStyle) {
        int ruleY = y + lineHeight / 2;
        ctx->SetStrokePaint(mdStyle.horizontalRuleColor);
        ctx->SetStrokeWidth(mdStyle.horizontalRuleHeight);
        ctx->DrawLine(x, ruleY, x + width, ruleY);
    }

    // ---------------------------------------------------------------
    // TABLE ROW RENDERER
    // ---------------------------------------------------------------

    static TableParseResult ParseTableRow(const std::string& line) {
        TableParseResult result;
        std::string trimmed = line;

        // Trim whitespace
        size_t start = trimmed.find_first_not_of(' ');
        if (start != std::string::npos) trimmed = trimmed.substr(start);
        size_t end = trimmed.find_last_not_of(' ');
        if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);

        // Must start and end with |
        if (trimmed.empty() || trimmed[0] != '|') return result;

        // Remove leading/trailing |
        if (trimmed.front() == '|') trimmed = trimmed.substr(1);
        if (!trimmed.empty() && trimmed.back() == '|') trimmed = trimmed.substr(0, trimmed.length() - 1);

        // Split by |
        std::istringstream ss(trimmed);
        std::string cell;
        while (std::getline(ss, cell, '|')) {
            // Trim cell whitespace
            size_t cs = cell.find_first_not_of(' ');
            size_t ce = cell.find_last_not_of(' ');
            if (cs != std::string::npos && ce != std::string::npos) {
                result.cells.push_back(cell.substr(cs, ce - cs + 1));
            } else {
                result.cells.push_back("");
            }
        }

        result.isValid = !result.cells.empty();
        return result;
    }

    static bool IsTableSeparatorRow(const std::string& line) {
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(' ');
        if (start != std::string::npos) trimmed = trimmed.substr(start);

        if (trimmed.empty() || trimmed[0] != '|') return false;

        // Check if all cells contain only -, :, |, and spaces
        for (char c : trimmed) {
            if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
        }
        // Must contain at least one -
        return trimmed.find('-') != std::string::npos;
    }

    static std::vector<TableColumnAlignment> ParseTableAlignments(const std::string& separatorLine) {
        std::vector<TableColumnAlignment> alignments;
        auto parsed = ParseTableRow(separatorLine);
        for (const auto& cell : parsed.cells) {
            bool leftColon = (!cell.empty() && cell.front() == ':');
            bool rightColon = (!cell.empty() && cell.back() == ':');

            if (leftColon && rightColon) {
                alignments.push_back(TableColumnAlignment::Center);
            } else if (rightColon) {
                alignments.push_back(TableColumnAlignment::Right);
            } else {
                alignments.push_back(TableColumnAlignment::Left);
            }
        }
        return alignments;
    }

    static void RenderMarkdownTableRow(IRenderContext* ctx, const std::string& line,
                                       int x, int y, int lineHeight, int width,
                                       bool isHeaderRow,
                                       const std::vector<TableColumnAlignment>& alignments,
                                       int columnCount,
                                       const TextAreaStyle& style,
                                       const MarkdownHybridStyle& mdStyle,
                                       std::vector<MarkdownHitRect>* hitRects = nullptr) {
        auto parsed = ParseTableRow(line);
        if (!parsed.isValid) return;

        int availableWidth = width;
        int cellWidth = (columnCount > 0) ? availableWidth / columnCount : availableWidth;
        int padding = static_cast<int>(mdStyle.tableCellPadding);

        // Draw header background
        if (isHeaderRow) {
            ctx->SetFillPaint(mdStyle.tableHeaderBackground);
            ctx->FillRectangle(x, y, availableWidth, lineHeight);
        }

        // Draw top border line
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(x, y, x + availableWidth, y);

        // Draw bottom border line
        ctx->DrawLine(x, y + lineHeight, x + availableWidth, y + lineHeight);

        // Draw cells
        int cellX = x;
        for (size_t col = 0; col < parsed.cells.size() && col < static_cast<size_t>(columnCount); col++) {
            // Draw vertical separator
            ctx->SetStrokePaint(mdStyle.tableBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(cellX, y, cellX, y + lineHeight);

            // Determine text alignment for this column
            TextAlignment align = TextAlignment::Left;
            if (col < alignments.size()) {
                switch (alignments[col]) {
                    case TableColumnAlignment::Center: align = TextAlignment::Center; break;
                    case TableColumnAlignment::Right: align = TextAlignment::Right; break;
                    default: align = TextAlignment::Left; break;
                }
            }

            // Set font weight for header
            ctx->SetFontWeight(isHeaderRow ? FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(style.fontColor);

            // Draw cell text
            const std::string& cellText = parsed.cells[col];
            int textX = cellX + padding;
            int textWidth = ctx->GetTextLineWidth(cellText);
            int contentWidth = cellWidth - padding * 2;

            if (align == TextAlignment::Center) {
                textX = cellX + (cellWidth - textWidth) / 2;
            } else if (align == TextAlignment::Right) {
                textX = cellX + cellWidth - padding - textWidth;
            }

            // Render cell content with inline markdown
            RenderMarkdownLine(ctx, cellText, textX, y, lineHeight, style, mdStyle, hitRects);

            cellX += cellWidth;
        }

        // Draw right border of last column
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(cellX, y, cellX, y + lineHeight);

        // Reset font
        ctx->SetFontWeight(FontWeight::Normal);
    }

    static void RenderMarkdownTableSeparator(IRenderContext* ctx,
                                             int x, int y, int lineHeight, int width,
                                             int columnCount,
                                             const MarkdownHybridStyle& mdStyle) {
        int cellWidth = (columnCount > 0) ? width / columnCount : width;

        // Draw thick separator line
        int separatorY = y + lineHeight / 2;
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLine(x, separatorY, x + width, separatorY);

        // Draw vertical lines at column boundaries
        ctx->SetStrokeWidth(1.0f);
        for (int col = 0; col <= columnCount; col++) {
            int colX = x + col * cellWidth;
            ctx->DrawLine(colX, y, colX, y + lineHeight);
        }
    }
};

// ===== HELPER FUNCTIONS =====

// Check if a line is a horizontal rule (---, ***, ___)
static bool IsMarkdownHorizontalRule(const std::string& trimmed) {
    if (trimmed.length() < 3) return false;

    char ruleChar = trimmed[0];
    if (ruleChar != '-' && ruleChar != '*' && ruleChar != '_') return false;

    for (char c : trimmed) {
        if (c != ruleChar && c != ' ') return false;
    }

    // Count actual rule characters (excluding spaces)
    int count = 0;
    for (char c : trimmed) {
        if (c == ruleChar) count++;
    }
    return count >= 3;
}

// Check if a line is a table row (starts with |)
static bool IsMarkdownTableRow(const std::string& trimmed) {
    if (trimmed.empty()) return false;
    return trimmed[0] == '|';
}

// ===== MAIN DRAWING METHOD =====

/**
 * @brief Draw text with hybrid markdown rendering
 *
 * This method replaces the standard DrawHighlightedText when markdown mode is enabled.
 * Current line (with cursor) shows raw markdown with syntax highlighting.
 * All other lines show formatted markdown (bold, italic, headers, code, etc.)
 *
 * Multi-line state tracking:
 * - Code blocks (``` ... ```) are tracked by pre-scanning the lines array
 * - Table context (header/separator/data rows) is tracked during rendering
 */
void UltraCanvasTextArea::DrawMarkdownHybridText(IRenderContext* context) {
    if (!syntaxTokenizer) return;

    context->PushState();
    context->ClipRect(visibleTextArea);
    context->SetFontStyle(style.fontStyle);

    // Get current line index where cursor is located
    auto [cursorLine, cursorCol] = GetLineColumnFromPosition(cursorGraphemePosition);

    // Calculate visible lines
    int startLine = std::max(0, firstVisibleLine - 1);
    int endLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);

    // --- Pre-scan: build code block state map for visible range ---
    // We need to scan from line 0 to determine code block state
    // because a code block could have started before the visible area
    std::vector<bool> isInsideCodeBlock(lines.size(), false);
    {
        bool inCode = false;
        for (size_t i = 0; i < lines.size(); i++) {
            std::string trimmed = TrimWhitespace(lines[i]);
            if (trimmed.find("```") == 0) {
                if (!inCode) {
                    inCode = true;
                    isInsideCodeBlock[i] = true; // The delimiter itself is "inside"
                } else {
                    isInsideCodeBlock[i] = true; // Closing delimiter is also "inside"
                    inCode = false;
                }
            } else {
                isInsideCodeBlock[i] = inCode;
            }
        }
    }

    // --- Pre-scan: detect table context for visible range ---
    // For each visible line, determine if it's part of a table and its role
    enum class TableLineRole { NoneRole, Header, Separator, DataRow };

    std::vector<TableLineRole> tableRoles(lines.size(), TableLineRole::NoneRole);
    std::vector<int> tableColumnCounts(lines.size(), 0);
    std::vector<std::vector<TableColumnAlignment>> tableAlignments(lines.size());
    {
        for (size_t i = 0; i < lines.size(); i++) {
            std::string trimmed = TrimWhitespace(lines[i]);
            if (!IsMarkdownTableRow(trimmed)) continue;

            // Check if next line is a separator (this line is header)
            if (i + 1 < lines.size()) {
                std::string nextTrimmed = TrimWhitespace(lines[i + 1]);
                if (MarkdownInlineRenderer::IsTableSeparatorRow(nextTrimmed)) {
                    // This is a header row
                    auto headerParsed = MarkdownInlineRenderer::ParseTableRow(trimmed);
                    int colCount = static_cast<int>(headerParsed.cells.size());
                    tableRoles[i] = TableLineRole::Header;
                    tableColumnCounts[i] = colCount;

                    // Parse alignments from separator
                    tableAlignments[i] = MarkdownInlineRenderer::ParseTableAlignments(nextTrimmed);

                    // Mark separator
                    tableRoles[i + 1] = TableLineRole::Separator;
                    tableColumnCounts[i + 1] = colCount;
                    tableAlignments[i + 1] = tableAlignments[i];

                    // Mark subsequent data rows
                    for (size_t j = i + 2; j < lines.size(); j++) {
                        std::string dataTrimmed = TrimWhitespace(lines[j]);
                        if (IsMarkdownTableRow(dataTrimmed)) {
                            tableRoles[j] = TableLineRole::DataRow;
                            tableColumnCounts[j] = colCount;
                            tableAlignments[j] = tableAlignments[i];
                        } else {
                            break; // End of table
                        }
                    }

                    // Skip past the table we just processed
                    // (the outer loop will still iterate, but roles are already set)
                }
            }
        }
    }

    // --- Markdown hybrid style ---
    MarkdownHybridStyle mdStyle = MarkdownHybridStyle::Default();

    // Clear previous hit rects
    markdownHitRects.clear();

    int baseY = visibleTextArea.y - (firstVisibleLine - startLine) * computedLineHeight;

    for (int i = startLine; i < endLine; i++) {
        const std::string& line = lines[i];
        int textY = baseY + (i - startLine) * computedLineHeight;
        int x = visibleTextArea.x - horizontalScrollOffset;

        // --- CURRENT LINE: Show raw markdown with syntax highlighting ---
        if (i == cursorLine) {
            auto tokens = syntaxTokenizer->TokenizeLine(line);

            for (const auto& token : tokens) {
                context->SetFontWeight(GetStyleForTokenType(token.type).bold ?
                                       FontWeight::Bold : FontWeight::Normal);
                int tokenWidth = context->GetTextLineWidth(token.text);

                if (x + tokenWidth >= visibleTextArea.x &&
                    x <= visibleTextArea.x + visibleTextArea.width) {
                    TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                    context->SetTextPaint(tokenStyle.color);
                    context->DrawText(token.text, x, textY);
                }

                x += tokenWidth;
            }
            continue;
        }

        // --- OTHER LINES: Show formatted markdown ---

        // Reset to default font settings
        context->SetFontWeight(FontWeight::Normal);
        context->SetFontSlant(FontSlant::Normal);
        context->SetFontSize(style.fontStyle.fontSize);
        context->SetFontFamily(style.fontStyle.fontFamily);

        // Empty lines
        if (line.empty()) continue;

        std::string trimmed = TrimWhitespace(line);
        if (trimmed.empty()) continue;

        // --- Code block content ---
        if (isInsideCodeBlock[i]) {
            if (trimmed.find("```") == 0) {
                MarkdownInlineRenderer::RenderMarkdownCodeBlockDelimiter(
                        context, trimmed, x, textY, computedLineHeight,
                        visibleTextArea.width, style, mdStyle);
            } else {
                MarkdownInlineRenderer::RenderMarkdownCodeBlockLine(
                        context, line, x, textY, computedLineHeight,
                        visibleTextArea.width, style, mdStyle);
            }
            continue;
        }

        // --- Table rows ---
        if (tableRoles[i] != TableLineRole::NoneRole) {
            if (tableRoles[i] == TableLineRole::Separator) {
                MarkdownInlineRenderer::RenderMarkdownTableSeparator(
                        context, x, textY, computedLineHeight,
                        visibleTextArea.width, tableColumnCounts[i], mdStyle);
            } else {
                bool isHeader = (tableRoles[i] == TableLineRole::Header);
                MarkdownInlineRenderer::RenderMarkdownTableRow(
                        context, line, x, textY, computedLineHeight,
                        visibleTextArea.width, isHeader,
                        tableAlignments[i], tableColumnCounts[i],
                        style, mdStyle, &markdownHitRects);
            }
            continue;
        }

        // --- Headers ---
        if (trimmed[0] == '#') {
            MarkdownInlineRenderer::RenderMarkdownHeader(
                    context, trimmed, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- Horizontal rules ---
        if (IsMarkdownHorizontalRule(trimmed)) {
            MarkdownInlineRenderer::RenderMarkdownHorizontalRule(
                    context, x, textY, computedLineHeight,
                    visibleTextArea.width, mdStyle);
            continue;
        }

        // --- Blockquotes ---
        if (trimmed[0] == '>') {
            MarkdownInlineRenderer::RenderMarkdownBlockquote(
                    context, line, x, textY, computedLineHeight,
                    visibleTextArea.width, style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- List items ---
        if (IsMarkdownListItem(trimmed)) {
            MarkdownInlineRenderer::RenderMarkdownListItem(
                    context, line, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- Regular text with inline formatting ---
        MarkdownInlineRenderer::RenderMarkdownLine(
                context, line, x, textY, computedLineHeight,
                style, mdStyle, &markdownHitRects);
    }

    context->PopState();
}

// ---------------------------------------------------------------
// HELPER: Detect markdown list items
// ---------------------------------------------------------------
bool UltraCanvasTextArea::IsMarkdownListItem(const std::string& line) const {
    if (line.empty()) return false;

    size_t pos = 0;
    // Skip leading whitespace
    while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

    if (pos >= line.length()) return false;

    // Check for unordered list markers: -, *, + followed by space
    if (line[pos] == '-' || line[pos] == '*' || line[pos] == '+') {
        // Make sure it's not a horizontal rule (--- or ***)
        if (pos + 1 < line.length() && line[pos + 1] == ' ') {
            return true;
        }
        return false;
    }

    // Check for ordered list: digit(s) followed by ". "
    if (std::isdigit(line[pos])) {
        while (pos < line.length() && std::isdigit(line[pos])) pos++;
        if (pos < line.length() && line[pos] == '.') {
            if (pos + 1 < line.length() && line[pos + 1] == ' ') {
                return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------
// HELPER: Trim whitespace from string
// ---------------------------------------------------------------
std::string UltraCanvasTextArea::TrimWhitespace(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// ---------------------------------------------------------------
// MARKDOWN LINK/IMAGE CLICK HANDLING
// ---------------------------------------------------------------

bool UltraCanvasTextArea::HandleMarkdownClick(int mouseX, int mouseY) {
    if (!markdownHybridMode) return false;

    for (const auto& hitRect : markdownHitRects) {
        if (mouseX >= hitRect.bounds.x && mouseX <= hitRect.bounds.x + hitRect.bounds.width &&
            mouseY >= hitRect.bounds.y && mouseY <= hitRect.bounds.y + hitRect.bounds.height) {

            if (hitRect.isImage) {
                // Image click — trigger callback with image path
                if (onMarkdownImageClick) {
                    onMarkdownImageClick(hitRect.url, hitRect.altText);
                    return true;
                }
            } else {
                // Link click — trigger callback with URL
                if (onMarkdownLinkClick) {
                    onMarkdownLinkClick(hitRect.url);
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------
// MARKDOWN HOVER HANDLING — update cursor for links/images
// ---------------------------------------------------------------

bool UltraCanvasTextArea::HandleMarkdownHover(int mouseX, int mouseY) {
    if (!markdownHybridMode) return false;

    for (const auto& hitRect : markdownHitRects) {
        if (mouseX >= hitRect.bounds.x && mouseX <= hitRect.bounds.x + hitRect.bounds.width &&
            mouseY >= hitRect.bounds.y && mouseY <= hitRect.bounds.y + hitRect.bounds.height) {
            SetMouseCursor(UCMouseCursor::Hand);
            return true;
        }
    }
    return false;
}
} // namespace UltraCanvas