// Plugins/Text/Markdown.cpp
// Markdown text display driver with full formatting and rendering support
// Version: 1.1.0
// Last Modified: 2025-10-28
// Author: UltraCanvas Framework
#include "Plugins/Text/UltraCanvasMarkdown.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasApplication.h"
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
    static int CalculateNestingDepth(const std::string& line) {
        int leadingSpaces = 0;
        for (char ch : line) {
            if (ch == ' ') {
                leadingSpaces++;
            } else if (ch == '\t') {
                leadingSpaces += 4; // Count tab as 4 spaces
            } else {
                break;
            }
        }
        // Each nesting level is typically 2-4 spaces
        // Use 2 spaces as standard for nesting detection
        return leadingSpaces / 2;
    }

    std::shared_ptr<MarkdownElement> MarkdownElement::CreateText(const std::string &content) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Text);
        element->text = content;
        return element;
    }

    std::shared_ptr<MarkdownElement> MarkdownElement::CreateHeader(int headerLevel, const std::string &content) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Header);
        element->level = std::clamp(headerLevel, 1, 6);
        element->text = content;
        return element;
    }

    std::shared_ptr<MarkdownElement> MarkdownElement::CreateLink(const std::string &text, const std::string &url) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Link);
        element->text = text;
        element->url = url;
        element->clickable = true;
        return element;
    }

    std::shared_ptr<MarkdownElement>
    MarkdownElement::CreateCodeBlock(const std::string &code, const std::string &language) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::CodeBlock);
        element->text = code;
        element->language = language;
        return element;
    }

    std::string MarkdownInlineConverter::ConvertToPangoMarkup(const std::string &text) {
        std::string result = text;

        // Escape existing XML/HTML special characters first
        result = EscapeXml(result);

        result = ConvertBoldItalic(result);

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

    std::string MarkdownInlineConverter::EscapeXml(const std::string &text) {
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

    std::string MarkdownInlineConverter::ConvertBoldItalic(const std::string &text) {
        std::string result = text;

        // Handle ***bold italic***
        size_t pos = 0;
        while ((pos = result.find("***", pos)) != std::string::npos) {
            size_t end = result.find("***", pos + 3);
            if (end != std::string::npos) {
                std::string content = result.substr(pos + 3, end - pos - 3);
                std::string replacement = "<b><i>" + content + "</i></b>";
                result.replace(pos, end - pos + 3, replacement);
                pos += replacement.length();
            } else {
                break;
            }
        }

        // Handle ___bold italic___
        pos = 0;
        while ((pos = result.find("___", pos)) != std::string::npos) {
            size_t end = result.find("___", pos + 3);
            if (end != std::string::npos) {
                std::string content = result.substr(pos + 3, end - pos - 3);
                std::string replacement = "<b><i>" + content + "</i></b>";
                result.replace(pos, end - pos + 3, replacement);
                pos += replacement.length();
            } else {
                break;
            }
        }

        return result;
    }

    std::string MarkdownInlineConverter::ConvertBold(const std::string &text) {
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

    std::string MarkdownInlineConverter::ConvertItalic(const std::string &text) {
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

    std::string MarkdownInlineConverter::ConvertInlineCode(const std::string &text) {
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

    std::string MarkdownInlineConverter::ConvertStrikethrough(const std::string &text) {
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

    std::vector<std::shared_ptr<MarkdownElement>> MarkdownParser::Parse(const std::string &markdown) {
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

    std::shared_ptr<MarkdownElement> MarkdownParser::ParseLine(const std::string &line, const std::string &previousLine) {
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
            return ParseListItem(line);
        }

        // Default: paragraph text with inline styling
        return ParseParagraph(trimmed);
    }

    std::shared_ptr<MarkdownElement> MarkdownParser::ParseHeader(const std::string &line) {
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

    std::shared_ptr<MarkdownElement> MarkdownParser::ParseQuote(const std::string &line) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Quote);
        std::string content = TrimWhitespace(line.substr(1));
        // Apply inline styling to quote content
        element->text = MarkdownInlineConverter::ConvertToPangoMarkup(content);
        return element;
    }

    bool MarkdownParser::IsListItem(const std::string &line) {
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

    std::shared_ptr<MarkdownElement> MarkdownParser::ParseListItem(const std::string& line) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::ListItem);

        // Calculate nesting depth from leading spaces BEFORE trimming
        element->level = CalculateNestingDepth(line);

        std::string trimmed = TrimWhitespace(line);

        // Determine if ordered or unordered
        if (std::isdigit(trimmed[0])) {
            element->ordered = true;
            size_t dotPos = trimmed.find('.');

            // Extract the actual number from the list item
            std::string numberStr = trimmed.substr(0, dotPos);
            element->orderNumber = std::stoi(numberStr);  // Store the actual item number

            std::string content = TrimWhitespace(trimmed.substr(dotPos + 1));
            // Apply inline styling to list item content
            element->text = MarkdownInlineConverter::ConvertToPangoMarkup(content);
        } else {
            element->ordered = false;
            element->orderNumber = 0;  // Unordered lists don't use level for numbering
            // Skip the marker (-, *, +) and space
            std::string content = TrimWhitespace(trimmed.substr(2));
            // Apply inline styling to list item content
            element->text = MarkdownInlineConverter::ConvertToPangoMarkup(content);
        }

        return element;
    }

    std::shared_ptr<MarkdownElement> MarkdownParser::ParseParagraph(const std::string &line) {
        auto element = std::make_shared<MarkdownElement>(MarkdownElementType::Paragraph);
        // Apply inline styling to paragraph content
        element->text = MarkdownInlineConverter::ConvertToPangoMarkup(line);
        return element;
    }

    bool MarkdownParser::IsHorizontalRule(const std::string &line) {
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

    std::string MarkdownParser::TrimWhitespace(const std::string &str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";

        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }


    void UltraCanvasMarkdownDisplay::SetMarkdownText(const std::string &markdown) {
        if (markdownText != markdown) {
            markdownText = markdown;
            needsReparse = true;
            needsRelayout = true;
            RequestRedraw();;
        }
    }

    void UltraCanvasMarkdownDisplay::SetStyle(const MarkdownStyle &newStyle) {
        style = newStyle;
        needsRelayout = true;
        RequestRedraw();
    }

    void UltraCanvasMarkdownDisplay::ScrollTo(int offset) {
        int maxScroll = std::max(0, contentHeight - GetHeight());
        verticalScrollOffset = std::clamp(offset, 0, maxScroll);
        RequestRedraw();
        if (onScrollChanged) {
            onScrollChanged(verticalScrollOffset);
        }
    }

    void UltraCanvasMarkdownDisplay::ScrollBy(int delta) {
        ScrollTo(verticalScrollOffset + delta);
    }

    void UltraCanvasMarkdownDisplay::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

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
        auto bounds = GetBounds();

        // Draw background
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(bounds);
        ctx->PushState();
        // Set clipping region
        ctx->ClipRect(bounds);

        // Render elements
        for (auto& element : elements) {
            if (element->visible) {
                RenderElement(ctx, element);
            }
        }
        ctx->PopState();

        // Draw scrollbar if needed
        if (contentHeight > bounds.height) {
            UpdateScrollbarGeometry(bounds);
            DrawScrollbar(ctx);
        }

        ctx->PopState();
    }

    int UltraCanvasMarkdownDisplay::CalculateElementHeight(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element, int maxWidth) {
        ctx->SetTextIsMarkup(true);

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
                // Calculate indent based on nesting depth
                int totalIndent = style.listIndent * (element->level + 1);
                return CalculateWrappedTextHeight(ctx, element->text, maxWidth - totalIndent) +
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

    void UltraCanvasMarkdownDisplay::PerformLayout(IRenderContext* ctx) {
        int currentY = GetY() + 10;
        int maxWidth = GetWidth() - 20 - style.scrollbarWidth;

        MarkdownElementType previousElementType = MarkdownElementType::Unknown;

        for (auto& element : elements) {
            // Calculate spacing based on element type and previous element
            float topSpacing = 0.0f;

            // Add block spacing only when there's an actual line break element
            if (element->type == MarkdownElementType::LineBreak) {
                topSpacing = style.blockSpacing;
            } else if (previousElementType != MarkdownElementType::Unknown && previousElementType != MarkdownElementType::LineBreak) {
                topSpacing = style.paragraphSpacing;
            }

            currentY += topSpacing;

            // Calculate element dimensions
            int elementHeight = CalculateElementHeight(ctx, element, maxWidth);

            // Calculate X position with nesting indent for list items
            int elementX = GetX() + 10;
            if (element->type == MarkdownElementType::ListItem) {
                // Add indentation based on nesting depth
                elementX += style.listIndent * element->level;
            }

            // Set element bounds
            element->bounds = Rect2Di(
                    elementX,
                    currentY,
                    maxWidth - (elementX - GetX() - 10),  // Adjust width for indentation
                    elementHeight
            );

            currentY += elementHeight;
            previousElementType = element->type;
        }

        contentHeight = currentY - GetY() + 10;
    }


    int
    UltraCanvasMarkdownDisplay::CalculateWrappedTextHeight(IRenderContext *ctx, const std::string &text, int maxWidth) {
        if (text.empty()) return 0;

        int w, h;
        ctx->GetTextDimensions(text, maxWidth, 0, w, h);
        return h;
    }

    Rect2Di UltraCanvasMarkdownDisplay::GetAdjustedBounds(const Rect2Di &bounds) {
        return Rect2Di(
                bounds.x,
                bounds.y - verticalScrollOffset,
                bounds.width,
                bounds.height
        );
    }

    Point2Di UltraCanvasMarkdownDisplay::GetAdjustedPosition(const Rect2Di &bounds) {
        return Point2Di(bounds.x, bounds.y - verticalScrollOffset);
    }

    void UltraCanvasMarkdownDisplay::RenderElement(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
        // Skip if element is scrolled out of view
        Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);
        if (adjustedBounds.y + adjustedBounds.height < GetY() ||
            adjustedBounds.y > GetY() + GetHeight()) {
            return;
        }
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetTextIsMarkup(true);

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

    void UltraCanvasMarkdownDisplay::RenderHeader(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
        int level = std::clamp(element->level - 1, 0, 5);

        ctx->SetFontSize(style.headerSizes[level]);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(style.headerColors[level]);

        Point2Di position = GetAdjustedPosition(element->bounds);
        ctx->DrawText(element->text, position);
    }

    void UltraCanvasMarkdownDisplay::RenderParagraph(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
        ctx->SetFontSize(style.fontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(style.textColor);

        DrawTextWrapped(ctx, element->text, GetAdjustedBounds(element->bounds), style.lineHeight);
    }

    void UltraCanvasMarkdownDisplay::RenderCodeBlock(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
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

    void UltraCanvasMarkdownDisplay::RenderQuote(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
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

    void UltraCanvasMarkdownDisplay::RenderListItem(IRenderContext* ctx, std::shared_ptr<MarkdownElement> element) {
        Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);

        ctx->SetFontSize(style.fontSize);
        ctx->SetTextPaint(style.textColor);

        // Draw bullet or number
        Point2Di bulletPos(adjustedBounds.x, adjustedBounds.y);

        if (element->ordered) {
            // Draw the actual item number from element->level
            std::string numberText = std::to_string(element->orderNumber) + ".";
            ctx->DrawText(numberText, bulletPos.x, bulletPos.y);
        } else {
            // Draw bullet
            ctx->SetTextPaint(style.bulletColor);
            ctx->DrawText(style.bulletCharacter, bulletPos.x, bulletPos.y);
        }

        // Draw text with proper indent
        ctx->SetTextPaint(style.textColor);
        Rect2Di textBounds(
                adjustedBounds.x + style.listIndent,
                adjustedBounds.y,
                adjustedBounds.width - style.listIndent,
                adjustedBounds.height
        );

        DrawTextWrapped(ctx, element->text, textBounds, style.lineHeight);
    }

    void UltraCanvasMarkdownDisplay::RenderHorizontalRule(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
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

    void UltraCanvasMarkdownDisplay::RenderLink(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
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

    void UltraCanvasMarkdownDisplay::RenderText(IRenderContext *ctx, std::shared_ptr<MarkdownElement> element) {
        Point2Di position = GetAdjustedPosition(element->bounds);

        ctx->SetFontSize(style.fontSize);
        ctx->SetTextPaint(style.textColor);
        ctx->DrawText(element->text, position);
    }

    void
    UltraCanvasMarkdownDisplay::DrawTextWrapped(IRenderContext *ctx, const std::string &text, const Rect2Di &bounds,
                                                float lineHeight) {
        if (text.empty()) return;

        ctx->DrawTextInRect(text, bounds.x, bounds.y, bounds.width, bounds.height);
    }

    void UltraCanvasMarkdownDisplay::UpdateScrollbarGeometry(const Rect2Di &bounds) {
        if (contentHeight <= bounds.height) return;

        // Calculate scrollbar track rectangle
        scrollbarTrackRect = Rect2Di(
                bounds.x + bounds.width - style.scrollbarWidth,
                bounds.y,
                style.scrollbarWidth,
                bounds.height
        );

        // Calculate thumb size and position
        int maxScroll = std::max(0, contentHeight - bounds.height);
        float visibleRatio = static_cast<float>(bounds.height) / static_cast<float>(contentHeight);
        int thumbHeight = std::max(20, static_cast<int>(visibleRatio * bounds.height));

        int availableTrackHeight = bounds.height - thumbHeight;
        int thumbY = bounds.y;
        if (maxScroll > 0) {
            thumbY += static_cast<int>((static_cast<float>(verticalScrollOffset) / static_cast<float>(maxScroll)) * availableTrackHeight);
        }

        scrollbarThumbRect = Rect2Di(
                scrollbarTrackRect.x,
                thumbY,
                style.scrollbarWidth,
                thumbHeight
        );
    }

    void UltraCanvasMarkdownDisplay::DrawScrollbar(IRenderContext *ctx) {
        // Draw track
        ctx->SetFillPaint(style.scrollbarTrackColor);
        ctx->FillRectangle(scrollbarTrackRect);

        // Determine thumb color based on interaction state
        Color thumbColor = style.scrollbarColor;
        if (isDraggingThumb) {
            thumbColor = style.scrollbarPressedColor;
        } else if (isHoveringThumb) {
            thumbColor = style.scrollbarHoverColor;
        }

        // Draw thumb
        ctx->SetFillPaint(thumbColor);
        ctx->FillRectangle(scrollbarThumbRect);
    }

    bool UltraCanvasMarkdownDisplay::HandleMouseDown(const UCEvent &event) {
        Point2Di mousePos(event.x, event.y);

        // Check if clicking on scrollbar
        if (contentHeight > GetHeight()) {
            // Check if clicking on thumb
            if (scrollbarThumbRect.Contains(mousePos)) {
                isDraggingThumb = true;
                dragStartY = event.globalY;
                dragStartScrollOffset = verticalScrollOffset;
                UltraCanvasApplication::GetInstance()->CaptureMouse(this);
                return true;
            }

            // Check if clicking on track (page up/down)
            if (scrollbarTrackRect.Contains(mousePos)) {
                if (mousePos.y < scrollbarThumbRect.y) {
                    // Clicked above thumb - page up
                    ScrollBy(-GetHeight());
                } else if (mousePos.y > scrollbarThumbRect.y + scrollbarThumbRect.height) {
                    // Clicked below thumb - page down
                    ScrollBy(GetHeight());
                }
                return true;
            }
        }

        // Check for link clicks
        if (event.button == UCMouseButton::Left) {
            auto element = FindElementAtPosition(event.x, event.y);
            if (element && element->clickable) {
                clickedElement = element;
                return true;
            }
        }

        return false;
    }

    bool UltraCanvasMarkdownDisplay::HandleMouseMove(const UCEvent &event) {
        Point2Di mousePos(event.x, event.y);

        // Handle thumb dragging
        if (isDraggingThumb) {
            int deltaY = event.globalY - dragStartY;

            // Calculate scroll offset based on drag distance
            int maxScroll = std::max(0, contentHeight - GetHeight());
            int availableTrackHeight = GetHeight() - scrollbarThumbRect.height;

            if (availableTrackHeight > 0) {
                float scrollRatio = static_cast<float>(deltaY) / static_cast<float>(availableTrackHeight);
                int newScrollOffset = dragStartScrollOffset + static_cast<int>(scrollRatio * maxScroll);
                ScrollTo(newScrollOffset);
            }

            return true;
        }

        // Update hover states
        bool wasHoveringThumb = isHoveringThumb;
        isHoveringThumb = scrollbarThumbRect.Contains(mousePos);
        isHoveringScrollbar = scrollbarTrackRect.Contains(mousePos);

        // Update element hover for links
        auto element = FindElementAtPosition(event.x, event.y);
        if (element != hoveredElement) {
            hoveredElement = element;
            // Could trigger redraw for hover effects
        }

        // Return true if hover state changed to trigger redraw
        return (wasHoveringThumb != isHoveringThumb);
    }

    bool UltraCanvasMarkdownDisplay::HandleMouseUp(const UCEvent &event) {
        bool wasHandled = false;

        // End thumb dragging
        if (isDraggingThumb) {
            isDraggingThumb = false;
            wasHandled = true;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
        }

        // Handle link clicks
        if (clickedElement && clickedElement->clickable) {
            if (clickedElement == FindElementAtPosition(event.x, event.y)) {
                if (onLinkClicked) {
                    onLinkClicked(clickedElement->url);
                }
                MarkLinkAsVisited(clickedElement->url);
            }
            clickedElement = nullptr;
            wasHandled = true;
        }

        return wasHandled;
    }

    bool UltraCanvasMarkdownDisplay::HandleMouseWheel(const UCEvent &event) {
        ScrollBy(-event.wheelDelta * 10);

        return true;
    }

    bool UltraCanvasMarkdownDisplay::OnEvent(const UCEvent &event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);

            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                if (HandleMouseMove(event)) {
                    RequestRedraw();
                    return true;
                }
                break;
            default:
                return false;
        }
    }

    std::shared_ptr<MarkdownElement> UltraCanvasMarkdownDisplay::FindElementAtPosition(int x, int y) {
        Point2Di mousePos(x, y);
        for (const auto& element : elements) {
            Rect2Di adjustedBounds = GetAdjustedBounds(element->bounds);
            if (adjustedBounds.Contains(mousePos)) {
                return element;
            }
        }
        return nullptr;
    }

    bool UltraCanvasMarkdownDisplay::IsLinkVisited(const std::string &url) const {
        return std::find(visitedLinks.begin(), visitedLinks.end(), url) != visitedLinks.end();
    }

    void UltraCanvasMarkdownDisplay::MarkLinkAsVisited(const std::string &url) {
        if (!IsLinkVisited(url)) {
            visitedLinks.push_back(url);
        }
    }
} // namespace UltraCanvas