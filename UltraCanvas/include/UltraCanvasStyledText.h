// include/UltraCanvasStyledText.h
// Rich text component with advanced formatting, editing, and styling capabilities
// Version: 1.3.2
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <fstream>
#include <algorithm>

namespace UltraCanvas {

// ===== TEXT FORMATTING ENUMS =====
    enum class TextFormat {
        PlainText,
        RichText,
        HTML,
        Markdown,
        BBCode
    };

    enum class TextDecoration {
        NoDecoration = 0,
        Underline = 1,
        Strikethrough = 2,
        Overline = 4
    };

// ===== EXTENDED TEXT STYLE (ADDS TO FRAMEWORK TextStyle) =====
    struct ExtendedTextStyle : public TextStyle {
        Color backgroundColor = Colors::Transparent;
        int textDecoration = static_cast<int>(TextDecoration::NoDecoration);

        // Extended margins and padding
        float marginTop = 0.0f;
        float marginBottom = 0.0f;
        float marginLeft = 0.0f;
        float marginRight = 0.0f;
        float paddingTop = 4.0f;
        float paddingBottom = 4.0f;
        float paddingLeft = 8.0f;
        float paddingRight = 8.0f;

        ExtendedTextStyle() : TextStyle() {}

        ExtendedTextStyle(const TextStyle& base) : TextStyle(base) {}

        static ExtendedTextStyle Default() {
            ExtendedTextStyle style;
            style.fontFamily = "Sans";
            style.fontSize = 12.0f;
            style.fontWeight = FontWeight::Normal;
            style.fontStyle = FontStyle::Normal;
            style.textColor = Colors::Black;
            return style;
        }

        static ExtendedTextStyle Header() {
            ExtendedTextStyle style;
            style.fontFamily = "Sans";
            style.fontSize = 18.0f;
            style.fontWeight = FontWeight::Bold;
            style.marginBottom = 10.0f;
            return style;
        }

        static ExtendedTextStyle Code() {
            ExtendedTextStyle style;
            style.fontFamily = "Courier New";
            style.fontSize = 11.0f;
            style.backgroundColor = Color(245, 245, 245);
            style.paddingLeft = 12.0f;
            style.paddingRight = 12.0f;
            return style;
        }
    };

// ===== TEXT BLOCK =====
    struct TextBlock {
        std::string text;
        ExtendedTextStyle style;
        Rect2D bounds;
        std::vector<Rect2D> lineRects;
        bool needsLayout = true;

        TextBlock() = default;
        TextBlock(const std::string& content, const ExtendedTextStyle& textStyle = ExtendedTextStyle::Default())
                : text(content), style(textStyle) {}
    };

// ===== TEXT SELECTION =====
    struct TextSelection {
        size_t startPos = 0;
        size_t endPos = 0;
        Color selectionColor = Color(0, 120, 215, 100);

        bool IsValid() const { return startPos != endPos; }
        size_t GetLength() const { return (endPos > startPos) ? endPos - startPos : startPos - endPos; }
        size_t GetStart() const { return std::min(startPos, endPos); }
        size_t GetEnd() const { return std::max(startPos, endPos); }
    };

// ===== SEARCH FUNCTIONALITY =====
    struct SearchResult {
        size_t position;
        size_t length;
        Rect2D bounds;

        SearchResult(size_t pos, size_t len) : position(pos), length(len) {}
    };

// ===== MAIN STYLED TEXT CLASS =====
    class UltraCanvasStyledText : public UltraCanvasUIElement {
    private:
        // Content
        std::vector<TextBlock> textBlocks;
        std::string rawText;
        TextFormat currentFormat = TextFormat::PlainText;

        // Layout and rendering
        bool needsReflow = true;
        std::vector<Rect2D> lineRects;
        Point2D scrollOffset = Point2D(0, 0);
        Point2D maxScrollOffset = Point2D(0, 0);

        // Selection and editing
        bool editable = false;
        bool selectionEnabled = true;
        TextSelection selection;
        bool hasSelection = false;

        // Caret
        size_t caretPosition = 0;
        bool caretVisible = true;
        std::chrono::steady_clock::time_point lastCaretBlink;
        float caretBlinkRate = 1.0f; // seconds

        // Search
        std::vector<SearchResult> searchResults;
        size_t currentSearchIndex = 0;
        std::string lastSearchTerm;

        // Scrolling
        bool scrollEnabled = true;
        bool autoScroll = true;

        // Events
        std::function<void()> onTextChanged;
        std::function<void()> onSelectionChanged;
        std::function<void(size_t)> onCaretMoved;
        std::function<void()> onModified;

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasStyledText(const std::string& identifier, long id, long x, long y, long w, long h)
                : UltraCanvasUIElement(identifier, id, x, y, w, h) {
            lastCaretBlink = std::chrono::steady_clock::now();

            // Add default text block
            textBlocks.emplace_back("", ExtendedTextStyle::Default());
        }

        virtual ~UltraCanvasStyledText() = default;

        // ===== CONTENT MANAGEMENT =====
        void SetText(const std::string& text, TextFormat format = TextFormat::PlainText) {
            rawText = text;
            currentFormat = format;

            // Clear existing blocks and create new one
            textBlocks.clear();
            textBlocks.emplace_back(text, ExtendedTextStyle::Default());

            needsReflow = true;
            caretPosition = 0;
            selection = TextSelection();
            hasSelection = false;

            if (onTextChanged) onTextChanged();
        }

        std::string GetText() const {
            return rawText;
        }

        void AppendText(const std::string& text, const ExtendedTextStyle& style = ExtendedTextStyle::Default()) {
            textBlocks.emplace_back(text, style);
            rawText += text;
            needsReflow = true;

            if (onTextChanged) onTextChanged();
        }

        void InsertText(const std::string& text, size_t position = SIZE_MAX) {
            if (position == SIZE_MAX) {
                position = caretPosition;
            }

            // Insert text at caret position
            if (position <= rawText.length()) {
                rawText.insert(position, text);
                caretPosition = position + text.length();
                needsReflow = true;

                // Update text blocks (simplified - would need more sophisticated handling)
                if (!textBlocks.empty()) {
                    textBlocks[0].text = rawText;
                    textBlocks[0].needsLayout = true;
                }

                if (onTextChanged) onTextChanged();
                if (onCaretMoved) onCaretMoved(caretPosition);
            }
        }

        void DeleteText(size_t start, size_t length) {
            if (start < rawText.length()) {
                size_t actualLength = std::min(length, rawText.length() - start);
                rawText.erase(start, actualLength);

                // Adjust caret position
                if (caretPosition > start) {
                    caretPosition = std::max(start, caretPosition - actualLength);
                }

                needsReflow = true;

                // Update text blocks
                if (!textBlocks.empty()) {
                    textBlocks[0].text = rawText;
                    textBlocks[0].needsLayout = true;
                }

                if (onTextChanged) onTextChanged();
                if (onCaretMoved) onCaretMoved(caretPosition);
            }
        }

        void Clear() {
            rawText.clear();
            textBlocks.clear();
            textBlocks.emplace_back("", ExtendedTextStyle::Default());
            caretPosition = 0;
            selection = TextSelection();
            hasSelection = false;
            needsReflow = true;

            if (onTextChanged) onTextChanged();
        }

        // ===== TEXT BLOCKS =====
        void AddTextBlock(const TextBlock& block) {
            textBlocks.push_back(block);
            rawText += block.text;
            needsReflow = true;
        }

        void SetTextBlocks(const std::vector<TextBlock>& blocks) {
            textBlocks = blocks;

            // Rebuild raw text
            rawText.clear();
            for (const auto& block : textBlocks) {
                rawText += block.text;
            }

            needsReflow = true;
            if (onTextChanged) onTextChanged();
        }

        const std::vector<TextBlock>& GetTextBlocks() const {
            return textBlocks;
        }

        // ===== FORMATTING =====
        void SetDefaultStyle(const ExtendedTextStyle& style) {
            if (!textBlocks.empty()) {
                textBlocks[0].style = style;
                textBlocks[0].needsLayout = true;
                needsReflow = true;
            }
        }

        ExtendedTextStyle GetDefaultStyle() const {
            return !textBlocks.empty() ? textBlocks[0].style : ExtendedTextStyle::Default();
        }

        void ApplyStyleToRange(size_t start, size_t length, const ExtendedTextStyle& style) {
            // Simplified implementation - would need proper range handling
            // For now, just update the first block if the range overlaps
            if (!textBlocks.empty() && start < rawText.length()) {
                textBlocks[0].style = style;
                textBlocks[0].needsLayout = true;
                needsReflow = true;
            }

            // Suppress unused parameter warning
            (void)length;
        }

        // ===== EDITING PROPERTIES =====
        void SetEditable(bool enable) {
            editable = enable;
        }

        bool IsEditable() const {
            return editable;
        }

        void SetSelectionEnabled(bool enable) {
            selectionEnabled = enable;
            if (!enable) {
                ClearSelection();
            }
        }

        bool IsSelectionEnabled() const {
            return selectionEnabled;
        }

        // ===== SELECTION =====
        void SetSelection(size_t start, size_t end) {
            if (selectionEnabled) {
                selection.startPos = std::min(start, rawText.length());
                selection.endPos = std::min(end, rawText.length());
                hasSelection = (selection.startPos != selection.endPos);

                if (onSelectionChanged) onSelectionChanged();
            }
        }

        void SelectAll() {
            SetSelection(0, rawText.length());
        }

        void ClearSelection() {
            selection = TextSelection();
            hasSelection = false;
            if (onSelectionChanged) onSelectionChanged();
        }

        bool HasSelection() const {
            return hasSelection && selection.IsValid();
        }

        std::string GetSelectedText() const {
            if (HasSelection()) {
                size_t start = selection.GetStart();
                size_t length = selection.GetLength();
                return rawText.substr(start, length);
            }
            return "";
        }

        void DeleteSelection() {
            if (HasSelection()) {
                size_t start = selection.GetStart();
                size_t length = selection.GetLength();
                DeleteText(start, length);
                caretPosition = start;
                ClearSelection();
            }
        }

        // ===== CARET =====
        void SetCaretPosition(size_t position) {
            caretPosition = std::min(position, rawText.length());
            caretVisible = true;
            lastCaretBlink = std::chrono::steady_clock::now();

            if (autoScroll) {
                ScrollToMakeVisible(caretPosition);
            }

            if (onCaretMoved) onCaretMoved(caretPosition);
        }

        size_t GetCaretPosition() const {
            return caretPosition;
        }

        void SetCaretBlinkRate(float rate) {
            caretBlinkRate = rate;
        }

        // ===== SCROLLING =====
        void SetScrollEnabled(bool enable) {
            scrollEnabled = enable;
        }

        void SetScrollOffset(const Point2D& offset) {
            scrollOffset.x = std::clamp(offset.x, 0.0f, maxScrollOffset.x);
            scrollOffset.y = std::clamp(offset.y, 0.0f, maxScrollOffset.y);
        }

        Point2D GetScrollOffset() const {
            return scrollOffset;
        }

        void ScrollBy(float deltaX, float deltaY) {
            SetScrollOffset(Point2D(scrollOffset.x + deltaX, scrollOffset.y + deltaY));
        }

        void ScrollTo(float x, float y) {
            SetScrollOffset(Point2D(x, y));
        }

        void ScrollToMakeVisible(size_t textPos) {
            if (textPos >= lineRects.size()) return;

            const Rect2D& rect = lineRects[textPos];
            Rect2D bounds = GetBounds();

            // Scroll vertically if needed
            if (rect.y < bounds.y + scrollOffset.y) {
                ScrollTo(scrollOffset.x, rect.y - bounds.y);
            } else if (rect.y + rect.height > bounds.y + bounds.height + scrollOffset.y) {
                ScrollTo(scrollOffset.x, rect.y + rect.height - bounds.height - bounds.y);
            }
        }

        // ===== SEARCH =====
        std::vector<SearchResult> Search(const std::string& searchTerm, bool caseSensitive = false) {
            searchResults.clear();
            lastSearchTerm = searchTerm;
            currentSearchIndex = 0;

            if (searchTerm.empty()) return searchResults;

            std::string searchText = caseSensitive ? rawText : ToLowerCase(rawText);
            std::string term = caseSensitive ? searchTerm : ToLowerCase(searchTerm);

            size_t pos = 0;
            while ((pos = searchText.find(term, pos)) != std::string::npos) {
                searchResults.emplace_back(pos, term.length());
                pos += term.length();
            }

            return searchResults;
        }

        void FindNext() {
            if (!searchResults.empty()) {
                currentSearchIndex = (currentSearchIndex + 1) % searchResults.size();
                ScrollToSearchResult(currentSearchIndex);
            }
        }

        void FindPrevious() {
            if (!searchResults.empty()) {
                currentSearchIndex = (currentSearchIndex == 0) ? searchResults.size() - 1 : currentSearchIndex - 1;
                ScrollToSearchResult(currentSearchIndex);
            }
        }

        void ClearSearch() {
            searchResults.clear();
            lastSearchTerm.clear();
            currentSearchIndex = 0;
        }

        // ===== FILE OPERATIONS =====
        bool LoadFromFile(const std::string& filePath) {
            std::ifstream file(filePath);
            if (!file.is_open()) return false;

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            SetText(content, TextFormat::PlainText);
            return true;
        }

        bool SaveToFile(const std::string& filePath) const {
            std::ofstream file(filePath);
            if (!file.is_open()) return false;

            file << rawText;
            file.close();
            return true;
        }

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override {
            if (!IsVisible()) return;

            ctx->PushState();

            // Reflow text if needed
            if (needsReflow) {
                ReflowText();
                needsReflow = false;
            }

            // Update caret blinking
            UpdateCaretBlinking();

            Rect2D bounds = GetBounds();

            // Draw background
            ctx->PaintWidthColorColors::White);
            ctx->DrawRectangle(bounds);

            // Set clipping to content area
            PushRenderState();
            ctx->ClipRect(bounds);

            // Apply scroll offset
            Translate(-scrollOffset.x, -scrollOffset.y);

            // Render text blocks
            RenderTextBlocks();

            // Render selection
            if (hasSelection) {
                RenderSelection();
            }

            // Render search highlights
            if (!searchResults.empty()) {
                RenderSearchHighlights();
            }

            // Render caret
            if (editable && caretVisible) {
                RenderCaret();
            }

            // Restore transforms
            PopRenderState();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (IsDisabled() || !IsVisible()) return false;;

            switch (event.type) {
                case UCEventType::MouseDown:
                    HandleMouseDown(event);
                    break;

                case UCEventType::MouseMove:
                    HandleMouseMove(event);
                    break;

                case UCEventType::MouseUp:
                    HandleMouseUp(event);
                    break;

                case UCEventType::KeyDown:
                    HandleKeyDown(event);
                    break;

                case UCEventType::KeyChar:
                    HandleKeyChar(event);
                    break;

                case UCEventType::MouseWheel:
                    HandleMouseWheel(event);
                    break;

                default:
                    break;
            }
            return false;
        }

        // ===== EVENT CALLBACKS =====
        void SetOnTextChanged(std::function<void()> callback) { onTextChanged = callback; }
        void SetOnSelectionChanged(std::function<void()> callback) { onSelectionChanged = callback; }
        void SetOnCaretMoved(std::function<void(size_t)> callback) { onCaretMoved = callback; }
        void SetOnModified(std::function<void()> callback) { onModified = callback; }

    private:
        // ===== LAYOUT =====
        void ReflowText() {
            lineRects.clear();

            Rect2D bounds = GetBounds();
            float currentY = bounds.y + textBlocks[0].style.paddingTop;
            float maxWidth = 0;

            for (auto& block : textBlocks) {
                if (block.needsLayout) {
                    LayoutTextBlock(block, bounds, currentY);
                    block.needsLayout = false;
                }

                // Update line rects
                for (const auto& lineRect : block.lineRects) {
                    lineRects.push_back(lineRect);
                    maxWidth = std::max(maxWidth, lineRect.width);
                }

                currentY += block.bounds.height + block.style.marginBottom;
            }

            // Update scroll limits
            maxScrollOffset.x = std::max(0.0f, maxWidth - bounds.width);
            maxScrollOffset.y = std::max(0.0f, currentY - bounds.y - bounds.height);
        }

        void LayoutTextBlock(TextBlock& block, const Rect2D& containerBounds, float startY) {
            block.lineRects.clear();

            if (block.text.empty()) {
                block.bounds = Rect2D(containerBounds.x, startY, containerBounds.width, block.style.fontSize * block.style.lineHeight);
                return;
            }

            // Simplified line breaking - split by words
            std::vector<std::string> words = SplitIntoWords(block.text);

            float lineY = startY;
            float lineHeight = block.style.fontSize * block.style.lineHeight;
            float availableWidth = containerBounds.width - block.style.paddingLeft - block.style.paddingRight;

            std::string currentLine;
            float currentLineWidth = 0;

            for (const auto& word : words) {
                float wordWidth = GetTextWidth(word.c_str());

                if (!currentLine.empty() && currentLineWidth + wordWidth > availableWidth) {
                    // Line is full, create line rect
                    Rect2D lineRect(
                            containerBounds.x + block.style.paddingLeft,
                            lineY,
                            currentLineWidth,
                            lineHeight
                    );
                    block.lineRects.push_back(lineRect);

                    // Start new line
                    currentLine = word;
                    currentLineWidth = wordWidth;
                    lineY += lineHeight;
                } else {
                    if (!currentLine.empty()) {
                        currentLine += " ";
                        currentLineWidth += GetTextWidth(" ");
                    }
                    currentLine += word;
                    currentLineWidth += wordWidth;
                }
            }

            // Add final line
            if (!currentLine.empty()) {
                Rect2D lineRect(
                        containerBounds.x + block.style.paddingLeft,
                        lineY,
                        currentLineWidth,
                        lineHeight
                );
                block.lineRects.push_back(lineRect);
                lineY += lineHeight;
            }

            block.bounds = Rect2D(
                    containerBounds.x,
                    startY,
                    containerBounds.width,
                    lineY - startY + block.style.paddingBottom
            );
        }

        // ===== RENDERING HELPERS =====
        void RenderTextBlocks() {
            for (const auto& block : textBlocks) {
                RenderTextBlock(block);
            }
        }

        void RenderTextBlock(const TextBlock& block) {
            // Set font and color using framework functions
            ctx->SetFont(block.style.fontFamily, block.style.fontSize);
            ctx->PaintWidthColorblock.style.textColor);

            // Draw background if specified
            if (block.style.backgroundColor.a > 0) {
                ctx->PaintWidthColorblock.style.backgroundColor);
                ctx->DrawRectangle(block.bounds);
            }

            // Draw text for each line
            std::vector<std::string> lines = SplitIntoLines(block.text);
            for (size_t i = 0; i < lines.size() && i < block.lineRects.size(); ++i) {
                const Rect2D& lineRect = block.lineRects[i];

                float textX = lineRect.x;
                float textY = lineRect.y + block.style.fontSize; // Baseline

                // Apply alignment
                if (block.style.alignment == TextAlignment::Center) {
                    textX = lineRect.x + (lineRect.width - GetTextWidth(lines[i].c_str())) / 2;
                } else if (block.style.alignment == TextAlignment::Right) {
                    textX = lineRect.x + lineRect.width - GetTextWidth(lines[i].c_str());
                }

                ctx->DrawText(lines[i], Point2D(textX, textY));

                // Draw decorations
                if (block.style.textDecoration & static_cast<int>(TextDecoration::Underline)) {
                    float underlineY = textY + 2;
                    ctx->PaintWidthColorblock.style.textColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine(Point2D(textX, underlineY), Point2D(textX + GetTextWidth(lines[i].c_str()), underlineY));
                }
            }
        }

        void RenderSelection() {
            if (!HasSelection()) return;

            ctx->PaintWidthColorselection.selectionColor);

            // Simple selection rendering - would need proper text position calculations
            size_t start = selection.GetStart();
            size_t end = selection.GetEnd();

            for (size_t i = start; i < end && i < lineRects.size(); ++i) {
                ctx->DrawRectangle(lineRects[i]);
            }
        }

        void RenderSearchHighlights() {
            ctx->PaintWidthColorColor(255, 255, 0, 100)); // Yellow highlight

            for (const auto& result : searchResults) {
                // Simple highlighting - would need proper text position calculations
                if (result.position < lineRects.size()) {
                    ctx->DrawRectangle(lineRects[result.position]);
                }
            }
        }

        void RenderCaret() {
            if (caretPosition >= lineRects.size()) return;

            const Rect2D& lineRect = lineRects[caretPosition];
            float caretX = lineRect.x; // Would need proper position calculation
            float caretY = lineRect.y;
            float caretHeight = lineRect.height;

            ctx->PaintWidthColorColors::Black);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2D(caretX, caretY), Point2D(caretX, caretY + caretHeight));
        }

        // ===== EVENT HANDLERS =====
        void HandleMouseDown(const UCEvent& event) {
            Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));

            if (Contains(mousePos)) {
                size_t textPos = GetTextPositionFromPoint(mousePos);

                if (selectionEnabled) {
                    if (event.shift) {
                        // Extend selection
                        SetSelection(selection.startPos, textPos);
                    } else {
                        // Start new selection
                        SetSelection(textPos, textPos);
                        hasSelection = false;
                    }
                }

                SetCaretPosition(textPos);
            }
        }

        void HandleMouseMove(const UCEvent& event) {
            // Handle selection dragging
            if (selectionEnabled && event.button == UCMouseButton::Left) {
                Point2D mousePos(static_cast<float>(event.x), static_cast<float>(event.y));
                size_t textPos = GetTextPositionFromPoint(mousePos);

                if (selection.startPos != textPos) {
                    SetSelection(selection.startPos, textPos);
                }
            }
        }

        void HandleMouseUp(const UCEvent& event) {
            // Selection handling is done in MouseMove
            (void)event; // Suppress unused parameter warning
        }

        void HandleKeyDown(const UCEvent& event) {
            if (!editable) return;

            switch (event.virtualKey) {
                case UCKeys::Left:
                    if (caretPosition > 0) {
                        SetCaretPosition(caretPosition - 1);
                    }
                    break;

                case UCKeys::Right:
                    if (caretPosition < rawText.length()) {
                        SetCaretPosition(caretPosition + 1);
                    }
                    break;

                case UCKeys::Up:
                    // Move up one line - simplified
                    if (caretPosition > 0) {
                        SetCaretPosition(std::max(0, static_cast<int>(caretPosition) - 40));
                    }
                    break;

                case UCKeys::Down:
                    // Move down one line - simplified
                    if (caretPosition < rawText.length()) {
                        SetCaretPosition(std::min(rawText.length(), caretPosition + 40));
                    }
                    break;

                case UCKeys::Home:
                    SetCaretPosition(0);
                    break;

                case UCKeys::End:
                    SetCaretPosition(rawText.length());
                    break;

                case UCKeys::Backspace:
                    if (HasSelection()) {
                        DeleteSelection();
                    } else if (caretPosition > 0) {
                        DeleteText(caretPosition - 1, 1);
                    }
                    break;

                case UCKeys::Delete:
                    if (HasSelection()) {
                        DeleteSelection();
                    } else if (caretPosition < rawText.length()) {
                        DeleteText(caretPosition, 1);
                    }
                    break;

                default:
                    break;
            }
        }

        void HandleKeyChar(const UCEvent& event) {
            if (!editable || event.character == 0) return;

            std::string inputText(1, event.character);

            // Handle special characters
            if (event.character == '\r' || event.character == '\n') {
                inputText = "\n";
            }

            if (HasSelection()) {
                DeleteSelection();
            }

            if (editable && !inputText.empty()) {
                InsertText(inputText);
            }
        }

        void HandleMouseWheel(const UCEvent& event) {
            if (scrollEnabled) {
                // FIX: Use correct property name from UCEvent
                ScrollBy(0, -event.wheelDelta * 20);
            }
        }

        // ===== UTILITY METHODS =====
        size_t GetTextPositionFromPoint(const Point2D& point) {
            // Simplified text position calculation
            for (size_t i = 0; i < lineRects.size(); ++i) {
                if (lineRects[i].Contains(point)) {
                    return i;
                }
            }
            return lineRects.size();
        }

        void ScrollToSearchResult(size_t index) {
            if (index < searchResults.size()) {
                // Suppress unused variable warning for now
                // const SearchResult& result = searchResults[index];
                // Calculate position and scroll to make it visible
                // This is simplified - would need full text layout
            }
        }

        void UpdateCaretBlinking() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCaretBlink);

            if (elapsed.count() >= caretBlinkRate * 1000) {
                caretVisible = !caretVisible;
                lastCaretBlink = now;
            }
        }

        std::vector<std::string> SplitIntoWords(const std::string& text) {
            std::vector<std::string> words;
            std::string currentWord;

            for (char c : text) {
                if (c == ' ' || c == '\t' || c == '\n') {
                    if (!currentWord.empty()) {
                        words.push_back(currentWord);
                        currentWord.clear();
                    }
                } else {
                    currentWord += c;
                }
            }

            if (!currentWord.empty()) {
                words.push_back(currentWord);
            }

            return words;
        }

        std::vector<std::string> SplitIntoLines(const std::string& text) {
            std::vector<std::string> lines;
            std::string currentLine;

            for (char c : text) {
                if (c == '\n') {
                    lines.push_back(currentLine);
                    currentLine.clear();
                } else {
                    currentLine += c;
                }
            }

            if (!currentLine.empty()) {
                lines.push_back(currentLine);
            }

            return lines;
        }

        std::string ToLowerCase(const std::string& str) {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            return result;
        }

        float std::clamp(float value, float min, float max) {
            return std::max(min, std::min(max, value));
        }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasStyledText> CreateStyledText(
            const std::string& identifier, long id, long x, long y, long w, long h) {
        return UltraCanvasUIElementFactory::CreateWithID<UltraCanvasStyledText>(
                id, identifier, id, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasStyledText> CreateStyledTextFromFile(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& filePath) {
        auto element = CreateStyledText(identifier, id, x, y, w, h);
        element->LoadFromFile(filePath);
        return element;
    }

    inline std::shared_ptr<UltraCanvasStyledText> CreateStyledTextEditor(
            const std::string& identifier, long id, long x, long y, long w, long h) {
        auto element = CreateStyledText(identifier, id, x, y, w, h);
        element->SetEditable(true);
        element->SetSelectionEnabled(true);
        return element;
    }

// ===== BUILDER PATTERN =====
    class StyledTextBuilder {
    private:
        std::string identifier = "StyledText";
        long id = 0;
        long x = 0, y = 0, w = 300, h = 200;
        std::string content;
        TextFormat format = TextFormat::PlainText;
        ExtendedTextStyle style = ExtendedTextStyle::Default();
        bool editable = false;
        bool wordWrap = true;
        bool autoResize = false;

    public:
        StyledTextBuilder& SetIdentifier(const std::string& elementId) { identifier = elementId; return *this; }
        StyledTextBuilder& SetID(long elementId) { id = elementId; return *this; }
        StyledTextBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
        StyledTextBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
        StyledTextBuilder& SetContent(const std::string& text, TextFormat fmt = TextFormat::PlainText) {
            content = text; format = fmt; return *this;
        }
        StyledTextBuilder& SetStyle(const ExtendedTextStyle& textStyle) { style = textStyle; return *this; }
        StyledTextBuilder& SetEditable(bool enable) { editable = enable; return *this; }
        StyledTextBuilder& SetWordWrap(bool enable) { wordWrap = enable; return *this; }
        StyledTextBuilder& SetAutoResize(bool enable) { autoResize = enable; return *this; }

        std::shared_ptr<UltraCanvasStyledText> Build() {
            auto element = CreateStyledText(identifier, id, x, y, w, h);
            element->SetText(content, format);
            element->SetDefaultStyle(style);
            element->SetEditable(editable);
            return element;
        }
    };
    
} // namespace UltraCanvas