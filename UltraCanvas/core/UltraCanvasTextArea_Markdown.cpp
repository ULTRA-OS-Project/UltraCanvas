// UltraCanvas/core/UltraCanvasTextArea_Markdown.cpp
// Markdown hybrid rendering enhancement for TextArea
// Shows current line as plain text, all other lines as formatted markdown
// Version: 1.0.0
// Last Modified: 2026-01-26
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

// ===== MARKDOWN INLINE RENDERER =====
// Renders markdown inline formatting (bold, italic, code) directly in the text area

struct MarkdownInlineRenderer {
    
    // Parse and render a single line of markdown with inline formatting
    static void RenderMarkdownLine(IRenderContext* ctx, const std::string& line, 
                                   int x, int y, const TextAreaStyle& style) {
        // Process inline markdown elements
        std::vector<MarkdownInlineElement> elements = ParseInlineMarkdown(line);
        
        int currentX = x;
        
        for (const auto& elem : elements) {
            // Set font properties based on element type
            FontWeight weight = elem.isBold ? FontWeight::Bold : FontWeight::Normal;
            bool isItalic = elem.isItalic;
            Color color = elem.isCode ? style.tokenStyles.stringStyle.color : style.fontColor;
            
            // Apply styling
            ctx->SetFontWeight(weight);
            ctx->SetFontSlant(isItalic ? FontSlant::Italic : FontSlant::Normal);
            ctx->SetTextPaint(color);
            
            // Special background for code
            if (elem.isCode) {
                int textWidth = ctx->GetTextLineWidth(elem.text);
                ctx->SetFillPaint(Color(240, 240, 240, 255)); // Light gray background
                ctx->FillRectangle(currentX - 2, y - 2, textWidth + 4, 20);
            }
            
            // Draw the text
            ctx->DrawText(elem.text, currentX, y);
            
            // Advance X position
            currentX += ctx->GetTextLineWidth(elem.text);
        }
    }
    
    // Render markdown header with proper styling
    static void RenderMarkdownHeader(IRenderContext* ctx, const std::string& line,
                                     int x, int y, const TextAreaStyle& style) {
        // Detect header level
        int level = 0;
        size_t pos = 0;
        while (pos < line.length() && line[pos] == '#') {
            level++;
            pos++;
        }
        
        if (level == 0 || level > 6) {
            // Not a valid header, render as normal
            RenderMarkdownLine(ctx, line, x, y, style);
            return;
        }
        
        // Skip whitespace after #
        while (pos < line.length() && line[pos] == ' ') pos++;
        
        std::string headerText = line.substr(pos);
        
        // Set font size based on header level
        int fontSize = style.fontStyle.fontSize;
        switch (level) {
            case 1: fontSize = (int)(fontSize * 2.0); break;
            case 2: fontSize = (int)(fontSize * 1.5); break;
            case 3: fontSize = (int)(fontSize * 1.3); break;
            case 4: fontSize = (int)(fontSize * 1.2); break;
            case 5: fontSize = (int)(fontSize * 1.1); break;
            case 6: fontSize = fontSize; break;
        }
        
        ctx->SetFontSize(fontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(style.tokenStyles.keywordStyle.color);
        ctx->DrawText(headerText, x, y);
        
        // Reset font size
        ctx->SetFontSize(style.fontStyle.fontSize);
    }
    
    // Render markdown list item
    static void RenderMarkdownListItem(IRenderContext* ctx, const std::string& line,
                                       int x, int y, const TextAreaStyle& style) {
        // Detect list type
        bool isOrdered = false;
        size_t pos = 0;
        
        // Skip leading whitespace for indentation
        int indent = 0;
        while (pos < line.length() && line[pos] == ' ') {
            indent++;
            pos++;
        }
        
        // Check for ordered list (1. 2. etc.)
        if (pos < line.length() && std::isdigit(line[pos])) {
            while (pos < line.length() && std::isdigit(line[pos])) pos++;
            if (pos < line.length() && line[pos] == '.') {
                isOrdered = true;
                pos++;
            }
        }
        
        // Check for unordered list (- * +)
        if (!isOrdered && pos < line.length() && 
            (line[pos] == '-' || line[pos] == '*' || line[pos] == '+')) {
            pos++;
        }
        
        // Skip whitespace
        while (pos < line.length() && line[pos] == ' ') pos++;
        
        std::string itemText = line.substr(pos);
        
        // Draw bullet or number
        int bulletX = x + (indent * 8); // 8 pixels per indent level
        
        if (isOrdered) {
            // Number already in the line, skip drawing extra
        } else {
            // Draw bullet point
            ctx->SetFillPaint(style.fontColor);
            ctx->FillCircle(bulletX + 4, y + 6, 3);
        }
        
        // Draw list item text
        RenderMarkdownLine(ctx, itemText, bulletX + 20, y, style);
    }
    
    // Render markdown code block indicator
    static void RenderMarkdownCodeBlock(IRenderContext* ctx, const std::string& line,
                                        int x, int y, int width, const TextAreaStyle& style) {
        // Code block - render with background
        ctx->SetFillPaint(Color(245, 245, 245, 255)); // Light background
        ctx->FillRectangle(x - 4, y - 2, width, 20);
        
        // Draw code text
        ctx->SetFontFamily("Courier New"); // Set font family("Courier New");
        ctx->SetTextPaint(style.tokenStyles.commentStyle.color);
        
        // Remove ``` markers if present
        std::string codeText = line;
        if (codeText.find("```") == 0) {
            codeText = codeText.substr(3);
        }
        
        ctx->DrawText(codeText, x, y);
        ctx->SetFontFamily(style.fontStyle.fontFamily);
    }
    
    // Render markdown blockquote
    static void RenderMarkdownBlockquote(IRenderContext* ctx, const std::string& line,
                                         int x, int y, int width, const TextAreaStyle& style) {
        // Draw quote bar
        ctx->SetFillPaint(Color(200, 200, 200, 255));
        ctx->FillRectangle(x, y - 2, 4, 20);
        
        // Extract quote text (remove leading >)
        std::string quoteText = line;
        size_t pos = 0;
        while (pos < quoteText.length() && quoteText[pos] == '>') pos++;
        while (pos < quoteText.length() && quoteText[pos] == ' ') pos++;
        quoteText = quoteText.substr(pos);
        
        // Draw quote text with italic
        ctx->SetFontSlant(FontSlant::Italic);
        ctx->SetTextPaint(Color(100, 100, 100, 255));
        RenderMarkdownLine(ctx, quoteText, x + 10, y, style);
        ctx->SetFontSlant(FontSlant::Normal);
    }

private:
    struct MarkdownInlineElement {
        std::string text;
        bool isBold = false;
        bool isItalic = false;
        bool isCode = false;
        bool isLink = false;
        std::string url;
    };
    
    static std::vector<MarkdownInlineElement> ParseInlineMarkdown(const std::string& line) {
        std::vector<MarkdownInlineElement> elements;
        size_t pos = 0;
        
        while (pos < line.length()) {
            MarkdownInlineElement elem;
            bool parsed = false;
            
            // Check for code `text`
            if (line[pos] == '`') {
                size_t end = line.find('`', pos + 1);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 1, end - pos - 1);
                    elem.isCode = true;
                    elements.push_back(elem);
                    pos = end + 1;
                    parsed = true;
                }
            }
            
            // Check for bold **text** or __text__ (must check BEFORE italic)
            if (!parsed && pos + 1 < line.length()) {
                if ((line[pos] == '*' && line[pos + 1] == '*') ||
                    (line[pos] == '_' && line[pos + 1] == '_')) {
                    char marker = line[pos];
                    std::string endMarker(2, marker);
                    size_t end = pos + 2;
                    
                    // Find matching closing marker
                    while (end < line.length()) {
                        if (end + 1 < line.length() && 
                            line[end] == marker && line[end + 1] == marker) {
                            // Found closing marker
                            elem.text = line.substr(pos + 2, end - pos - 2);
                            elem.isBold = true;
                            elements.push_back(elem);
                            pos = end + 2;
                            parsed = true;
                            break;
                        }
                        end++;
                    }
                }
            }
            
            // Check for italic *text* or _text_ (single character)
            if (!parsed && (line[pos] == '*' || line[pos] == '_')) {
                char marker = line[pos];
                size_t end = pos + 1;
                
                // Find matching closing marker (but NOT double marker)
                while (end < line.length()) {
                    if (line[end] == marker) {
                        // Check it's not a double marker
                        bool isDouble = (end + 1 < line.length() && line[end + 1] == marker);
                        if (!isDouble) {
                            // Found single closing marker
                            elem.text = line.substr(pos + 1, end - pos - 1);
                            elem.isItalic = true;
                            elements.push_back(elem);
                            pos = end + 1;
                            parsed = true;
                            break;
                        } else {
                            // Skip double marker
                            end += 2;
                            continue;
                        }
                    }
                    end++;
                }
            }
            
            // If nothing was parsed, treat as regular text
            if (!parsed) {
                // Find next markdown marker
                size_t nextMarker = line.find_first_of("*_`", pos + 1);
                
                if (nextMarker == std::string::npos) {
                    // No more markers - take rest of line
                    elem.text = line.substr(pos);
                    elements.push_back(elem);
                    break;
                } else {
                    // Take text up to next marker
                    elem.text = line.substr(pos, nextMarker - pos);
                    elements.push_back(elem);
                    pos = nextMarker;
                }
            }
        }
        
        return elements;
    }
};

// ===== ADD TO UltraCanvasTextArea CLASS =====

/**
 * @brief New method to draw text with hybrid markdown rendering
 * 
 * This replaces the standard DrawHighlightedText when markdown mode is enabled.
 * Current line (with cursor) shows raw markdown with syntax highlighting.
 * All other lines show formatted markdown (bold, italic, headers, etc.)
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
    int baseY = visibleTextArea.y - (firstVisibleLine - startLine) * computedLineHeight;
    
    for (int i = startLine; i < endLine; i++) {
        const std::string& line = lines[i];
        int textY = baseY + (i - startLine) * computedLineHeight;
        int x = visibleTextArea.x - horizontalScrollOffset;
        
        if (line.empty()) continue;
        
        // HYBRID RENDERING LOGIC
        if (i == cursorLine) {
            // CURRENT LINE: Show raw markdown with syntax highlighting
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
        } else {
            // OTHER LINES: Show formatted markdown
            
            // Reset to default font settings
            context->SetFontWeight(FontWeight::Normal);
            context->SetFontSlant(FontSlant::Normal);
            
            // Detect line type and render accordingly
            std::string trimmed = TrimWhitespace(line);
            
            // Headers
            if (!trimmed.empty() && trimmed[0] == '#') {
                MarkdownInlineRenderer::RenderMarkdownHeader(context, line, x, textY, style);
            }
            // Code blocks
            else if (trimmed.find("```") == 0) {
                MarkdownInlineRenderer::RenderMarkdownCodeBlock(context, line, x, textY, 
                                                               visibleTextArea.width, style);
            }
            // Blockquotes
            else if (!trimmed.empty() && trimmed[0] == '>') {
                MarkdownInlineRenderer::RenderMarkdownBlockquote(context, line, x, textY,
                                                                 visibleTextArea.width, style);
            }
            // List items
            else if (IsMarkdownListItem(trimmed)) {
                MarkdownInlineRenderer::RenderMarkdownListItem(context, line, x, textY, style);
            }
            // Regular text with inline formatting
            else {
                MarkdownInlineRenderer::RenderMarkdownLine(context, line, x, textY, style);
            }
        }
    }
    
    context->PopState();
}

// Helper method to detect markdown list items
bool UltraCanvasTextArea::IsMarkdownListItem(const std::string& line) const {
    if (line.empty()) return false;
    
    size_t pos = 0;
    // Skip leading whitespace
    while (pos < line.length() && line[pos] == ' ') pos++;
    
    if (pos >= line.length()) return false;
    
    // Check for unordered list markers
    if (line[pos] == '-' || line[pos] == '*' || line[pos] == '+') {
        // Must be followed by space
        return (pos + 1 < line.length() && line[pos + 1] == ' ');
    }
    
    // Check for ordered list (number followed by dot)
    if (std::isdigit(line[pos])) {
        while (pos < line.length() && std::isdigit(line[pos])) pos++;
        return (pos < line.length() && line[pos] == '.');
    }
    
    return false;
}

// Helper method to trim whitespace
std::string UltraCanvasTextArea::TrimWhitespace(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace UltraCanvas