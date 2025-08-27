// UltraCanvasTextArea.h
// Multi-line text editor component with scrollbars, line numbers, and syntax highlighting
// Version: 1.2.6
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== TEXT AREA CONFIGURATION =====
    enum class LineWrapMode {
        NoneWrap,
        Word,
        Character
    };

    enum class ScrollBarMode {
        Auto,
        AlwaysShow,  // Changed from "Always" to avoid X11 conflict
        Never
    };

    struct TextAreaStyle {
        UltraCanvas::Color backgroundColor = UltraCanvas::Colors::White;
        UltraCanvas::Color textColor = UltraCanvas::Colors::Black;
        UltraCanvas::Color borderColor = UltraCanvas::Colors::Gray;
        UltraCanvas::Color selectionColor = UltraCanvas::Color(173, 216, 230, 128); // Light blue with transparency
        UltraCanvas::Color lineNumberColor = UltraCanvas::Colors::Gray;
        UltraCanvas::Color lineNumberBackgroundColor = UltraCanvas::Color(248, 248, 248);
        UltraCanvas::Color scrollbarColor = UltraCanvas::Color(200, 200, 200);
        UltraCanvas::Color scrollbarThumbColor = UltraCanvas::Color(160, 160, 160);

        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        int lineHeight = 16;
        int characterWidth = 8;

        bool showLineNumbers = false;
        bool showScrollbars = true;
        LineWrapMode wrapMode = LineWrapMode::NoneWrap;
        ScrollBarMode verticalScrollMode = ScrollBarMode::Auto;
        ScrollBarMode horizontalScrollMode = ScrollBarMode::Auto;

        int tabSize = 4;
        bool convertTabsToSpaces = false;

        static TextAreaStyle Default() {
            return TextAreaStyle();
        }

        static TextAreaStyle Code() {
            TextAreaStyle style;
            style.fontFamily = "Courier New";
            style.showLineNumbers = true;
            style.backgroundColor = UltraCanvas::Color(248, 248, 248);
            style.lineNumberBackgroundColor = UltraCanvas::Color(240, 240, 240);
            return style;
        }
    };

// ===== MAIN TEXT AREA CLASS =====
    class UltraCanvasTextArea : public UltraCanvasElement {
    private:
        // Text content
        std::vector<std::string> lines;

        // Cursor and selection
        int cursorLine = 0;
        int cursorColumn = 0;
        bool hasSelection = false;
        int selectionStartLine = 0;
        int selectionStartColumn = 0;
        int selectionEndLine = 0;
        int selectionEndColumn = 0;

        // Scrolling
        int scrollOffsetX = 0;
        int scrollOffsetY = 0;
        int maxVisibleLines = 0;
        int maxVisibleColumns = 0;

        // Scrollbars
        bool hasVerticalScrollbar = false;
        bool hasHorizontalScrollbar = false;
        UltraCanvas::Rect2D verticalScrollThumb;
        UltraCanvas::Rect2D horizontalScrollThumb;
        bool isDraggingVerticalThumb = false;
        bool isDraggingHorizontalThumb = false;

        // Style and configuration
        TextAreaStyle style;
        bool readOnly = false;
        int lineNumberWidth = 0;
        int scrollbarWidth = 15;

        // Internal state
        bool cursorVisible = true;
        int blinkTimer = 0;

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasTextArea(const std::string& identifier, long id, long x, long y, long width, long height)
                : UltraCanvasElement(identifier, id, x, y, width, height) {
            lines.push_back(""); // Always have at least one line
            CalculateVisibleArea();
        }

        // ===== CONTENT MANAGEMENT =====
        void SetContent(const std::string& content) {
            lines.clear();

            if (content.empty()) {
                lines.push_back("");
                return;
            }

            std::string line;
            for (char c : content) {
                if (c == '\n') {
                    lines.push_back(line);
                    line.clear();
                } else if (c != '\r') { // Skip carriage returns
                    line += c;
                }
            }
            lines.push_back(line); // Add final line

            // Reset cursor and selection
            cursorLine = 0;
            cursorColumn = 0;
            hasSelection = false;
            scrollOffsetX = 0;
            scrollOffsetY = 0;

            CalculateVisibleArea();
            UpdateScrollbarThumbs();
            RequestRedraw();
        }

        std::string GetContent() const {
            return LinesToString();
        }

        void AppendText(const std::string& text) {
            if (readOnly) return;

            for (char c : text) {
                if (c == '\n') {
                    // Split current line and create new line
                    std::string remainingText = lines[cursorLine].substr(cursorColumn);
                    lines[cursorLine] = lines[cursorLine].substr(0, cursorColumn);
                    lines.insert(lines.begin() + cursorLine + 1, remainingText);
                    cursorLine++;
                    cursorColumn = 0;
                } else if (c != '\r') {
                    lines[cursorLine].insert(cursorColumn, 1, c);
                    cursorColumn++;
                }
            }

            CalculateVisibleArea();
            EnsureCursorVisible();
            RequestRedraw();
        }

        void InsertText(const std::string& text) {
            if (readOnly) return;

            // Delete selection first if it exists
            if (hasSelection) {
                DeleteSelection();
            }

            AppendText(text);
        }

        // ===== SELECTION MANAGEMENT =====
        void SelectAll() {
            hasSelection = true;
            selectionStartLine = 0;
            selectionStartColumn = 0;
            selectionEndLine = (int)lines.size() - 1;
            selectionEndColumn = (int)lines.back().length();
        }

        void ClearSelection() {
            hasSelection = false;
        }

        std::string GetSelectedText() const {
            if (!hasSelection) return "";

            std::string result;
            for (int line = selectionStartLine; line <= selectionEndLine; line++) {
                if (line >= (int)lines.size()) break;

                int startCol = (line == selectionStartLine) ? selectionStartColumn : 0;
                int endCol = (line == selectionEndLine) ? selectionEndColumn : (int)lines[line].length();

                if (startCol < endCol) {
                    result += lines[line].substr(startCol, endCol - startCol);
                }

                if (line < selectionEndLine) {
                    result += "\n";
                }
            }

            return result;
        }

        void DeleteSelection() {
            if (!hasSelection || readOnly) return;

            // Handle single line selection
            if (selectionStartLine == selectionEndLine) {
                std::string& line = lines[selectionStartLine];
                line.erase(selectionStartColumn, selectionEndColumn - selectionStartColumn);
                cursorLine = selectionStartLine;
                cursorColumn = selectionStartColumn;
            } else {
                // Multi-line selection
                std::string newLine = lines[selectionStartLine].substr(0, selectionStartColumn) +
                                      lines[selectionEndLine].substr(selectionEndColumn);

                // Remove lines in between
                lines.erase(lines.begin() + selectionStartLine + 1, lines.begin() + selectionEndLine + 1);
                lines[selectionStartLine] = newLine;

                cursorLine = selectionStartLine;
                cursorColumn = selectionStartColumn;
            }

            hasSelection = false;
            CalculateVisibleArea();
            EnsureCursorVisible();
        }

        // ===== STYLE AND CONFIGURATION =====
        void SetStyle(const TextAreaStyle& newStyle) {
            style = newStyle;
            CalculateVisibleArea();
            UpdateScrollbarThumbs();
        }

        const TextAreaStyle& GetStyle() const {
            return style;
        }

        void SetReadOnly(bool readonly) {
            readOnly = readonly;
        }

        bool IsReadOnly() const {
            return readOnly;
        }

        bool AcceptsFocus() const override { return true; }

        // ===== RENDERING =====
        void Render() override {
            UltraCanvas::RenderStateGuard _renderGuard;

            CalculateVisibleArea();
            DrawBackground();

            if (style.showLineNumbers) {
                DrawLineNumbers();
            }

            DrawTextContent();
            DrawSelection();

            if (IsFocused() && cursorVisible) {
                DrawCursor();
            }

            if (hasVerticalScrollbar) {
                DrawVerticalScrollbar();
            }

            if (hasHorizontalScrollbar) {
                DrawHorizontalScrollbar();
            }

            DrawBorder();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            switch (event.type) {
                case UCEventType::KeyDown:
                    return HandleKeyDown(event);
                case UCEventType::KeyUp:
                    return HandleKeyUp(event);
                    break;
                case UCEventType::MouseDown:
                    HandleMouseDown(event);
                    break;
                case UCEventType::MouseUp:
                    HandleMouseUp(event);
                    break;
                case UCEventType::MouseMove:
                    HandleMouseMove(event);
                    break;
                case UCEventType::MouseWheel:
                    HandleMouseWheel(event);
                    break;
            }
            return false;
        }

    private:
        // ===== INTERNAL CALCULATIONS =====
        void CalculateVisibleArea() {
            UltraCanvas::Rect2D textArea = GetTextAreaBounds();

            maxVisibleLines = (int)(textArea.height / style.lineHeight);
            maxVisibleColumns = (int)(textArea.width / style.characterWidth);

            // Calculate line number width if needed
            if (style.showLineNumbers) {
                int maxLineNumber = (int)lines.size();
                lineNumberWidth = std::to_string(maxLineNumber).length() * style.characterWidth + 10;
            } else {
                lineNumberWidth = 0;
            }

            // Determine if scrollbars are needed
            bool needVerticalScrollbar = (style.verticalScrollMode == ScrollBarMode::AlwaysShow) ||
                                         (style.verticalScrollMode == ScrollBarMode::Auto &&
                                          (int)lines.size() > maxVisibleLines);

            bool needHorizontalScrollbar = (style.horizontalScrollMode == ScrollBarMode::AlwaysShow) ||
                                           (style.horizontalScrollMode == ScrollBarMode::Auto &&
                                            GetMaxLineWidth() > maxVisibleColumns);

            hasVerticalScrollbar = needVerticalScrollbar && style.showScrollbars;
            hasHorizontalScrollbar = needHorizontalScrollbar && style.showScrollbars;

            // Adjust visible area for scrollbars
            if (hasHorizontalScrollbar) {
                maxVisibleLines = (int)((textArea.height - scrollbarWidth) / style.lineHeight);
            }
            if (hasVerticalScrollbar) {
                maxVisibleColumns = (int)((textArea.width - scrollbarWidth) / style.characterWidth);
            }
        }

        UltraCanvas::Rect2D GetTextAreaBounds() const {
            return UltraCanvas::Rect2D(
                    GetBounds().x + lineNumberWidth,
                    GetBounds().y,
                    GetBounds().width - lineNumberWidth - (hasVerticalScrollbar ? scrollbarWidth : 0),
                    GetBounds().height - (hasHorizontalScrollbar ? scrollbarWidth : 0)
            );
        }

        void UpdateScrollbarThumbs() {
            UltraCanvas::Rect2D bounds = GetBounds();

            // Vertical scrollbar thumb
            if (hasVerticalScrollbar && (int)lines.size() > maxVisibleLines) {
                float scrollbarHeight = bounds.height - (hasHorizontalScrollbar ? scrollbarWidth : 0);
                float thumbRatio = (float)maxVisibleLines / (int)lines.size();

                // Fix compilation error: ensure both parameters are same type
                int thumbHeight = std::max(20, (int)((maxVisibleLines * scrollbarHeight) / (int)lines.size()));

                float thumbPosition = (scrollOffsetY * (scrollbarHeight - thumbHeight)) /
                                      std::max(1, (int)lines.size() - maxVisibleLines);

                verticalScrollThumb = UltraCanvas::Rect2D(
                        bounds.x + bounds.width - scrollbarWidth,
                        bounds.y + thumbPosition,
                        scrollbarWidth,
                        thumbHeight
                );
            }

            // Horizontal scrollbar thumb
            if (hasHorizontalScrollbar) {
                int maxWidth = GetMaxLineWidth();
                if (maxWidth > maxVisibleColumns) {
                    float scrollbarWidthF = bounds.width - (hasVerticalScrollbar ? this->scrollbarWidth : 0);
                    float thumbRatio = (float)maxVisibleColumns / maxWidth;

                    // Fix compilation error: ensure both parameters are same type
                    int thumbWidth = std::max(20, (int)((maxVisibleColumns * scrollbarWidthF) / maxWidth));

                    float thumbPosition = (scrollOffsetX * (scrollbarWidthF - thumbWidth)) /
                                          std::max(1, maxWidth - maxVisibleColumns);

                    horizontalScrollThumb = UltraCanvas::Rect2D(
                            bounds.x + thumbPosition,
                            bounds.y + bounds.height - this->scrollbarWidth,
                            thumbWidth,
                            this->scrollbarWidth
                    );
                }
            }
        }

        int GetMaxLineWidth() const {
            int maxWidth = 0;
            for (const auto& line : lines) {
                maxWidth = std::max(maxWidth, (int)line.length());
            }
            return maxWidth;
        }

        void EnsureCursorVisible() {
            // Vertical scrolling
            if (cursorLine < scrollOffsetY) {
                scrollOffsetY = cursorLine;
            } else if (cursorLine >= scrollOffsetY + maxVisibleLines) {
                scrollOffsetY = cursorLine - maxVisibleLines + 1;
            }

            // Horizontal scrolling
            if (cursorColumn < scrollOffsetX) {
                scrollOffsetX = cursorColumn;
            } else if (cursorColumn >= scrollOffsetX + maxVisibleColumns) {
                scrollOffsetX = cursorColumn - maxVisibleColumns + 1;
            }

            UpdateScrollbarThumbs();
        }

        UltraCanvas::Point2D GetCursorScreenPosition() const {
            UltraCanvas::Rect2D textArea = GetTextAreaBounds();
            return UltraCanvas::Point2D(
                    textArea.x + (cursorColumn - scrollOffsetX) * style.characterWidth,
                    textArea.y + (cursorLine - scrollOffsetY) * style.lineHeight
            );
        }

        UltraCanvas::Point2D ScreenToTextPosition(int screenX, int screenY) const {
            UltraCanvas::Rect2D textArea = GetTextAreaBounds();

            int line = scrollOffsetY + (screenY - (int)textArea.y) / style.lineHeight;
            int column = scrollOffsetX + (screenX - (int)textArea.x) / style.characterWidth;

            line = std::max(0, std::min((int)lines.size() - 1, line));
            if (line < (int)lines.size()) {
                column = std::max(0, std::min((int)lines[line].length(), column));
            } else {
                column = 0;
            }

            return UltraCanvas::Point2D(column, line);
        }

        // ===== DRAWING FUNCTIONS =====
        void DrawBackground() {
            UltraCanvas::SetFillColor(style.backgroundColor);
            UltraCanvas::DrawRect(GetBounds());

            // Draw line number background
            if (style.showLineNumbers) {
                UltraCanvas::SetFillColor(style.lineNumberBackgroundColor);
                UltraCanvas::DrawRect(UltraCanvas::Rect2D(GetBounds().x, GetBounds().y, lineNumberWidth, GetBounds().height));
            }
        }

        void DrawBorder() {
            UltraCanvas::SetStrokeColor(style.borderColor);
            UltraCanvas::SetStrokeWidth(1.0f);
            // Draw border using four lines since DrawRectOutline doesn't exist
            UltraCanvas::Rect2D bounds = GetBounds();
            UltraCanvas::DrawLine(UltraCanvas::Point2D(bounds.x, bounds.y),
                                  UltraCanvas::Point2D(bounds.x + bounds.width, bounds.y));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(bounds.x + bounds.width, bounds.y),
                                  UltraCanvas::Point2D(bounds.x + bounds.width, bounds.y + bounds.height));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(bounds.x + bounds.width, bounds.y + bounds.height),
                                  UltraCanvas::Point2D(bounds.x, bounds.y + bounds.height));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(bounds.x, bounds.y + bounds.height),
                                  UltraCanvas::Point2D(bounds.x, bounds.y));
        }

        void DrawCursor() {
            UltraCanvas::Point2D cursorPos = GetCursorScreenPosition();
            UltraCanvas::SetStrokeColor(style.textColor);
            UltraCanvas::SetStrokeWidth(1.0f);

            UltraCanvas::DrawLine(
                    UltraCanvas::Point2D(cursorPos.x, cursorPos.y),
                    UltraCanvas::Point2D(cursorPos.x, cursorPos.y + style.lineHeight)
            );
        }

        void DrawLineNumbers() {
            if (!style.showLineNumbers) return;

            // Set up rendering for line numbers
            UltraCanvas::SetTextColor(style.lineNumberColor);
            UltraCanvas::SetFont(style.fontFamily, style.fontSize);

            for (int i = 0; i < maxVisibleLines && (scrollOffsetY + i) < (int)lines.size(); i++) {
                int lineNumber = scrollOffsetY + i + 1;
                std::string numberText = std::to_string(lineNumber);

                // Use GetTextWidth instead of MeasureText since it doesn't exist as standalone function
                auto* ctx = UltraCanvas::GetRenderContext();
                float textWidth = 0;
                if (ctx) {
                    textWidth = ctx->GetTextWidth(numberText);
                } else {
                    // Fallback estimate
                    textWidth = numberText.length() * style.characterWidth * 0.6f;
                }

                UltraCanvas::Point2D pos(
                        GetBounds().x + lineNumberWidth - textWidth - 5,
                        GetBounds().y + 2 + i * style.lineHeight + style.fontSize
                );

                UltraCanvas::DrawText(numberText, pos);
            }
        }

        void DrawTextContent() {
            UltraCanvas::Rect2D textArea = GetTextAreaBounds();

            UltraCanvas::SetTextColor(style.textColor);
            UltraCanvas::SetFont(style.fontFamily, style.fontSize);

            for (int i = 0; i < maxVisibleLines && (scrollOffsetY + i) < (int)lines.size(); i++) {
                int lineIndex = scrollOffsetY + i;
                const std::string& line = lines[lineIndex];

                // Calculate visible portion of the line
                int startCol = std::max(0, scrollOffsetX);
                int endCol = std::min((int)line.length(), scrollOffsetX + maxVisibleColumns);

                if (startCol < endCol) {
                    std::string visibleText = line.substr(startCol, endCol - startCol);

                    UltraCanvas::Point2D pos(
                            textArea.x + 5,
                            textArea.y + i * style.lineHeight
                    );

                    UltraCanvas::DrawText(visibleText, pos);
                }
            }
        }

        void DrawSelection() {
            if (!hasSelection) return;

            UltraCanvas::Rect2D textArea = GetTextAreaBounds();
            UltraCanvas::SetFillColor(style.selectionColor);

            for (int line = selectionStartLine; line <= selectionEndLine; line++) {
                if (line < scrollOffsetY || line >= scrollOffsetY + maxVisibleLines) continue;
                if (line >= (int)lines.size()) break;

                int startCol = (line == selectionStartLine) ? selectionStartColumn : 0;
                int endCol = (line == selectionEndLine) ? selectionEndColumn : (int)lines[line].length();

                // Adjust for scrolling
                startCol = std::max(startCol, scrollOffsetX);
                endCol = std::min(endCol, scrollOffsetX + maxVisibleColumns);

                if (startCol < endCol) {
                    int screenLine = line - scrollOffsetY;
                    int screenStartCol = startCol - scrollOffsetX;
                    int screenEndCol = endCol - scrollOffsetX;

                    UltraCanvas::DrawRect(UltraCanvas::Rect2D(
                            textArea.x + screenStartCol * style.characterWidth,
                            textArea.y + screenLine * style.lineHeight,
                            (screenEndCol - screenStartCol) * style.characterWidth,
                            style.lineHeight
                    ));
                }
            }
        }

        void DrawVerticalScrollbar() {
            if (!hasVerticalScrollbar) return;

            UltraCanvas::Rect2D bounds = GetBounds();
            UltraCanvas::Rect2D scrollbarArea(
                    bounds.x + bounds.width - scrollbarWidth,
                    bounds.y,
                    scrollbarWidth,
                    bounds.height - (hasHorizontalScrollbar ? scrollbarWidth : 0)
            );

            // Draw scrollbar background
            UltraCanvas::SetFillColor(style.scrollbarColor);
            UltraCanvas::DrawRect(scrollbarArea);
            UltraCanvas::SetStrokeColor(style.borderColor);
            // Draw scrollbar border using lines
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y),
                                  UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y),
                                  UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y + scrollbarArea.height));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y + scrollbarArea.height),
                                  UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y + scrollbarArea.height));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y + scrollbarArea.height),
                                  UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y));

            // Draw thumb
            UltraCanvas::SetFillColor(style.scrollbarThumbColor);
            UltraCanvas::DrawRect(verticalScrollThumb);
        }

        void DrawHorizontalScrollbar() {
            if (!hasHorizontalScrollbar) return;

            UltraCanvas::Rect2D bounds = GetBounds();
            UltraCanvas::Rect2D scrollbarArea(
                    bounds.x + lineNumberWidth,
                    bounds.y + bounds.height - scrollbarWidth,
                    bounds.width - lineNumberWidth - (hasVerticalScrollbar ? scrollbarWidth : 0),
                    scrollbarWidth
            );

            // Draw scrollbar background
            UltraCanvas::SetFillColor(style.scrollbarColor);
            UltraCanvas::FillRect(scrollbarArea);
            UltraCanvas::SetStrokeColor(style.borderColor);
            // Draw scrollbar border using lines
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y),
                                  UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y),
                                  UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y + scrollbarArea.height));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x + scrollbarArea.width, scrollbarArea.y + scrollbarArea.height),
                                  UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y + scrollbarArea.height));
            UltraCanvas::DrawLine(UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y + scrollbarArea.height),
                                  UltraCanvas::Point2D(scrollbarArea.x, scrollbarArea.y));

            // Draw thumb
            UltraCanvas::SetFillColor(style.scrollbarThumbColor);
            UltraCanvas::FillRect(horizontalScrollThumb);
        }

        // ===== EVENT HANDLERS =====
        bool HandleKeyDown(const UCEvent& event) {
            if (readOnly && event.virtualKey != UCKeys::Left && event.virtualKey != UCKeys::Right &&
                    event.virtualKey != UCKeys::Up && event.virtualKey != UCKeys::Down) {
                return false;
            }

            switch (event.virtualKey) {
                case UCKeys::Left:
                    if (cursorColumn > 0) {
                        cursorColumn--;
                    } else if (cursorLine > 0) {
                        cursorLine--;
                        cursorColumn = (int)lines[cursorLine].length();
                    }
                    break;
                case UCKeys::Right:
                    if (cursorColumn < (int)lines[cursorLine].length()) {
                        cursorColumn++;
                    } else if (cursorLine < (int)lines.size() - 1) {
                        cursorLine++;
                        cursorColumn = 0;
                    }
                    break;
                case UCKeys::Up:
                    if (cursorLine > 0) {
                        cursorLine--;
                        cursorColumn = std::min(cursorColumn, (int)lines[cursorLine].length());
                    }
                    break;
                case UCKeys::Down:
                    if (cursorLine < (int)lines.size() - 1) {
                        cursorLine++;
                        cursorColumn = std::min(cursorColumn, (int)lines[cursorLine].length());
                    }
                    break;
                case UCKeys::Home:
                    cursorColumn = 0;
                    break;
                case UCKeys::End:
                    cursorColumn = (int)lines[cursorLine].length();
                    break;
                case UCKeys::Return:
                    if (!readOnly) {
                        InsertText("\n");
                    }
                    break;
                case UCKeys::Backspace:
                    if (!readOnly && (cursorColumn > 0 || cursorLine > 0)) {
                        if (cursorColumn > 0) {
                            lines[cursorLine].erase(cursorColumn - 1, 1);
                            cursorColumn--;
                        } else {
                            // Join with previous line
                            cursorColumn = (int)lines[cursorLine - 1].length();
                            lines[cursorLine - 1] += lines[cursorLine];
                            lines.erase(lines.begin() + cursorLine);
                            cursorLine--;
                        }
                    }
                    break;
                case UCKeys::Delete:
                    if (!readOnly) {
                        if (cursorColumn < (int)lines[cursorLine].length()) {
                            lines[cursorLine].erase(cursorColumn, 1);
                        } else if (cursorLine < (int)lines.size() - 1) {
                            // Join with next line
                            lines[cursorLine] += lines[cursorLine + 1];
                            lines.erase(lines.begin() + cursorLine + 1);
                        }
                    }
                    break;
                default:
                    HandleTextInput(event);
                    break;
            }

            EnsureCursorVisible();
            RequestRedraw();
            return true;
        }

        bool HandleKeyUp(const UCEvent& /* event */) {
            // Handle key up events if needed (parameter marked as unused)
            return false;
        }

        void HandleMouseDown(const UCEvent& event) {
            // Set focus using correct function name
            SetFocus(true);

            // Check if clicking on scrollbars
            if (hasVerticalScrollbar && verticalScrollThumb.Contains(event.x, event.y)) {
                isDraggingVerticalThumb = true;
                return;
            }

            if (hasHorizontalScrollbar && horizontalScrollThumb.Contains(event.x, event.y)) {
                isDraggingHorizontalThumb = true;
                return;
            }

            // Regular text click
            UltraCanvas::Point2D textPos = ScreenToTextPosition(event.x, event.y);
            cursorLine = (int)textPos.y;
            cursorColumn = (int)textPos.x;

            // Start selection if shift is held
            if (event.shift) {
                if (!hasSelection) {
                    hasSelection = true;
                    selectionStartLine = cursorLine;
                    selectionStartColumn = cursorColumn;
                }
                selectionEndLine = cursorLine;
                selectionEndColumn = cursorColumn;
            } else {
                hasSelection = false;
            }
        }

        void HandleMouseUp(const UCEvent& /* event */) {
            isDraggingVerticalThumb = false;
            isDraggingHorizontalThumb = false;
        }

        void HandleMouseMove(const UCEvent& event) {
            // Fix compilation error: use correct enum comparison
            if (event.button == UCMouseButton::Left) {
                if (isDraggingVerticalThumb) {
                    // Handle vertical scrollbar dragging
                    UltraCanvas::Rect2D bounds = GetBounds();
                    float scrollbarHeight = bounds.height - (hasHorizontalScrollbar ? scrollbarWidth : 0);
                    float thumbHeight = verticalScrollThumb.height;
                    float maxThumbY = scrollbarHeight - thumbHeight;
                    float thumbY = std::max(0.0f, std::min(maxThumbY, (float)(event.y - bounds.y)));

                    scrollOffsetY = (int)((thumbY / maxThumbY) * std::max(0, (int)lines.size() - maxVisibleLines));
                    UpdateScrollbarThumbs();
                    return;
                }

                if (isDraggingHorizontalThumb) {
                    // Handle horizontal scrollbar dragging
                    UltraCanvas::Rect2D bounds = GetBounds();
                    float scrollbarWidthF = bounds.width - (hasVerticalScrollbar ? this->scrollbarWidth : 0);
                    float thumbWidth = horizontalScrollThumb.width;
                    float maxThumbX = scrollbarWidthF - thumbWidth;
                    float thumbX = std::max(0.0f, std::min(maxThumbX, (float)(event.x - bounds.x)));

                    scrollOffsetX = (int)((thumbX / maxThumbX) * std::max(0, GetMaxLineWidth() - maxVisibleColumns));
                    UpdateScrollbarThumbs();
                    return;
                }

                // Text selection
                if (hasSelection) {
                    UltraCanvas::Point2D textPos = ScreenToTextPosition(event.x, event.y);
                    selectionEndLine = (int)textPos.y;
                    selectionEndColumn = (int)textPos.x;
                }
            }
        }

        void HandleTextInput(const UCEvent& event) {
            if (readOnly || event.text.empty()) return;

            // Filter out control characters
            std::string filteredText;
            for (char c : event.text) {
                if (c >= 32 || c == '\t') { // Printable characters and tab
                    filteredText += c;
                }
            }

            if (!filteredText.empty()) {
                InsertText(filteredText);
            }
        }

        void HandleMouseWheel(const UCEvent& event) {
            // Scroll vertically by default, horizontally with Shift
            if (event.shift && hasHorizontalScrollbar) {
                scrollOffsetX = std::max(0, std::min(GetMaxLineWidth() - maxVisibleColumns,
                                                     scrollOffsetX - event.wheelDelta * 3));
            } else if (hasVerticalScrollbar) {
                scrollOffsetY = std::max(0, std::min((int)lines.size() - maxVisibleLines,
                                                     scrollOffsetY - event.wheelDelta));
            }

            UpdateScrollbarThumbs();
            EnsureCursorVisible();
        }

        // ===== UTILITY FUNCTIONS =====
        std::string LinesToString() const {
            std::string result;
            for (size_t i = 0; i < lines.size(); i++) {
                result += lines[i];
                if (i < lines.size() - 1) {
                    result += "\n";
                }
            }
            return result;
        }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasTextArea> CreateTextArea(
            const std::string& id, long uid, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasTextArea>(id, uid, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasTextArea> CreateTextArea(
            const std::string& id, long uid, const UltraCanvas::Rect2D& bounds) {
        return std::make_shared<UltraCanvasTextArea>(id, uid,
                                                     (long)bounds.x, (long)bounds.y, (long)bounds.width, (long)bounds.height);
    }

// ===== CONVENIENCE FUNCTIONS =====
    inline void SetTextAreaContent(UltraCanvasTextArea* textArea, const std::string& content) {
        if (textArea) {
            textArea->SetContent(content);
        }
    }

    inline std::string GetTextAreaContent(const UltraCanvasTextArea* textArea) {
        return textArea ? textArea->GetContent() : "";
    }

} // namespace UltraCanvas