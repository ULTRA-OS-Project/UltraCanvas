// UltraCanvasTooltipManager.cpp
// Implementation of tooltip system for UltraCanvas
// Version: 2.0.1
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "../include/UltraCanvasTooltipManager.h"
#include <string>
#include <algorithm>
#include <iostream>

namespace UltraCanvas {
// ===== STATIC MEMBER DEFINITIONS =====
    UltraCanvasElement* UltraCanvasTooltipManager::hoveredElement = nullptr;
    UltraCanvasElement* UltraCanvasTooltipManager::tooltipSource = nullptr;
    std::string UltraCanvasTooltipManager::currentText;
    Point2Di UltraCanvasTooltipManager::tooltipPosition;
    Point2Di UltraCanvasTooltipManager::cursorPosition;
    bool UltraCanvasTooltipManager::visible = false;
    bool UltraCanvasTooltipManager::pendingShow = false;

    std::chrono::steady_clock::time_point UltraCanvasTooltipManager::hoverStartTime;
    std::chrono::steady_clock::time_point UltraCanvasTooltipManager::hideStartTime;
    float UltraCanvasTooltipManager::showDelay = 0.8f;
    float UltraCanvasTooltipManager::hideDelay = 0.1f;

    TooltipStyle UltraCanvasTooltipManager::style;
    Point2Di UltraCanvasTooltipManager::tooltipSize;
    std::vector<std::string> UltraCanvasTooltipManager::wrappedLines;

    bool UltraCanvasTooltipManager::enabled = true;
    bool UltraCanvasTooltipManager::debugMode = false;
    Rect2Di UltraCanvasTooltipManager::screenBounds = Rect2Di(0, 0, 1920, 1080);

    void UltraCanvasTooltipManager::ShowTooltip(UltraCanvasElement *element, const std::string &text,
                                                const Point2Di &position) {
        if (!enabled || !element || text.empty()) return;

        // If already showing for this element, just update text
        if (tooltipSource == element && visible) {
            if (currentText != text) {
                currentText = text;
                CalculateTooltipLayout();
            }
            return;
        }

        // Hide current tooltip if showing for different element
        if (tooltipSource != element) {
            HideTooltip();
        }

        tooltipSource = element;
        currentText = text;

        // Set position
        if (position.x >= 0 && position.y >= 0) {
            cursorPosition = position;
        }
        UpdateTooltipPosition();

        // Calculate layout
        CalculateTooltipLayout();

        // Start show timer
        hoverStartTime = std::chrono::steady_clock::now();
        pendingShow = true;
        showDelay = style.showDelay;

        if (debugMode) {
            std::cout << "Tooltip requested for: " << element->GetIdentifier()
                      << " - Text: " << text << std::endl;
        }
    }

    void UltraCanvasTooltipManager::Update(float deltaTime) {
        if (!enabled) return;

        auto now = std::chrono::steady_clock::now();

        // Handle pending show
        if (pendingShow && tooltipSource) {
            float elapsed = std::chrono::duration<float>(now - hoverStartTime).count();
            if (elapsed >= showDelay) {
                visible = true;
                pendingShow = false;

                if (debugMode) {
                    std::cout << "Tooltip shown for: " << tooltipSource->GetIdentifier() << std::endl;
                }
            }
        }

        // Handle pending hide
        if (!pendingShow && !tooltipSource) {
            float elapsed = std::chrono::duration<float>(now - hideStartTime).count();
            if (elapsed >= hideDelay && visible) {
                visible = false;

                if (debugMode) {
                    std::cout << "Tooltip hidden" << std::endl;
                }
            }
        }

        // Update tooltip position if following cursor
        if (visible && style.followCursor) {
            UpdateTooltipPosition();
        }
    }

    void UltraCanvasTooltipManager::HideTooltip() {
        if (!tooltipSource && !visible && !pendingShow) return;

        tooltipSource = nullptr;
        pendingShow = false;

        if (visible) {
            hideStartTime = std::chrono::steady_clock::now();
            hideDelay = style.hideDelay;
        } else {
            visible = false;
        }

        if (debugMode) {
            std::cout << "Tooltip hide requested" << std::endl;
        }
    }

    void UltraCanvasTooltipManager::ShowImmediately(UltraCanvasElement *element, const std::string &text,
                                                    const Point2Di &position) {
        ShowTooltip(element, text, position);
        visible = true;
        pendingShow = false;
    }

    void UltraCanvasTooltipManager::HideImmediately() {
        tooltipSource = nullptr;
        pendingShow = false;
        visible = false;
    }

    void UltraCanvasTooltipManager::Render() {
        IRenderContext *ctx = GetRenderContext();
        if (!visible || currentText.empty()) return;

        ctx->PushState();

        // Draw shadow first
        if (style.hasShadow) {
            DrawShadow();
        }

        // Draw tooltip background
        DrawBackground();

        // Draw border
        if (style.borderWidth > 0) {
            DrawBorder();
        }

        // Draw text
        ctx->DrawText();
    }

    void UltraCanvasTooltipManager::SetStyle(const TooltipStyle &newStyle) {
        style = newStyle;
        if (visible) {
            CalculateTooltipLayout(); // Recalculate if currently visible
        }
    }

    void UltraCanvasTooltipManager::UpdateCursorPosition(const Point2Di &position) {
        cursorPosition = position;

        if (visible && style.followCursor) {
            UpdateTooltipPosition();
        }
    }

    void UltraCanvasTooltipManager::OnElementHover(UltraCanvasElement *element, const std::string &tooltipText,
                                                   const Point2Di &mousePosition) {
        UpdateCursorPosition(mousePosition);

        if (hoveredElement != element) {
            hoveredElement = element;

            if (!tooltipText.empty()) {
                ShowTooltip(element, tooltipText, mousePosition);
            } else {
                HideTooltip();
            }
        }
    }

    void UltraCanvasTooltipManager::OnElementLeave(UltraCanvasElement *element) {
        if (hoveredElement == element) {
            hoveredElement = nullptr;
            HideTooltip();
        }
    }

    void UltraCanvasTooltipManager::CalculateTooltipLayout() {
        if (currentText.empty()) return;

        // Set up text style for measurement
        ctx->PushState();
        ctx->SetFont(style.fontFamily, style.fontSize);

        // Word wrap the text
        wrappedLines = WrapText(currentText, style.maxWidth - style.paddingLeft - style.paddingRight);

        // Calculate size
        float maxLineWidth = 0;
        float totalHeight = 0;

        for (const std::string& line : wrappedLines) {
            float lineWidth = GetRenderContext()->GetTextWidth(line);
            maxLineWidth = std::max(maxLineWidth, lineWidth);
            totalHeight += style.fontSize * 1.2f; // Line height
        }

        tooltipSize.x = maxLineWidth + style.paddingLeft + style.paddingRight;
        tooltipSize.y = totalHeight + style.paddingTop + style.paddingBottom;

        // Ensure minimum size
        tooltipSize.x = std::max(tooltipSize.x, 20);
        tooltipSize.y = std::max(tooltipSize.y, 15);
    }

    void UltraCanvasTooltipManager::UpdateTooltipPosition() {
        // Basic positioning relative to cursor
        tooltipPosition.x = cursorPosition.x + style.offsetX;
        tooltipPosition.y = cursorPosition.y + style.offsetY;

        // Keep tooltip on screen
        if (screenBounds.width > 0 && screenBounds.height > 0) {
            // Adjust horizontal position
            if (tooltipPosition.x + tooltipSize.x > screenBounds.x + screenBounds.width) {
                tooltipPosition.x = cursorPosition.x - style.offsetX - tooltipSize.x;
            }

            // Adjust vertical position
            if (tooltipPosition.y + tooltipSize.y > screenBounds.y + screenBounds.height) {
                tooltipPosition.y = cursorPosition.y - style.offsetY - tooltipSize.y;
            }

            // Ensure tooltip is not off-screen
            tooltipPosition.x = std::max(tooltipPosition.x, screenBounds.x);
            tooltipPosition.y = std::max(tooltipPosition.y, screenBounds.y);
        }
    }

    std::vector <std::string> UltraCanvasTooltipManager::WrapText(const std::string &text, float maxWidth) {
        std::vector<std::string> lines;
        std::vector<std::string> words = SplitWords(text);

        if (words.empty()) {
            lines.push_back("");
            return lines;
        }

        std::string currentLine;

        for (const std::string& word : words) {
            std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
            float lineWidth = GetRenderContext()->GetTextWidth(testLine);

            if (lineWidth <= maxWidth || currentLine.empty()) {
                currentLine = testLine;
            } else {
                lines.push_back(currentLine);
                currentLine = word;
            }
        }

        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }

        return lines;
    }

    std::vector <std::string> UltraCanvasTooltipManager::SplitWords(const std::string &text) {
        std::vector<std::string> words;
        std::string word;

        for (char c : text) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
                if (c == '\n') {
                    words.push_back("\n"); // Preserve line breaks
                }
            } else {
                word += c;
            }
        }

        if (!word.empty()) {
            words.push_back(word);
        }

        return words;
    }

    void UltraCanvasTooltipManager::DrawShadow() {
        Rect2Di shadowRect(
                tooltipPosition.x + style.shadowOffset.x,
                tooltipPosition.y + style.shadowOffset.y,
                tooltipSize.x,
                tooltipSize.y
        );

        if (style.cornerRadius > 0) {
            ctx->SetFillColor(style.shadowColor);
            DrawingStyle shadowStyle = GetRenderContext()->GetDrawingStyle();
            shadowStyle.fillMode = FillMode::Solid;
            shadowStyle.fillColor = style.shadowColor;
            shadowStyle.hasStroke = false;
            SetDrawingStyle(shadowStyle);
            ctx->DrawRoundedRectangle(shadowRect, style.cornerRadius);
        } else {
            ctx->DrawFilledRectangle(shadowRect, style.shadowColor);
        }
    }

    void UltraCanvasTooltipManager::DrawBackground() {
        Rect2Di bgRect(tooltipPosition.x, tooltipPosition.y, tooltipSize.x, tooltipSize.y);

        if (style.cornerRadius > 0) {
           ctx->SetFillColor(style.backgroundColor);
            DrawingStyle bgStyle = GetRenderContext()->GetDrawingStyle();
            bgStyle.fillMode = FillMode::Solid;
            bgStyle.fillColor = style.backgroundColor;
            bgStyle.hasStroke = false;
            SetDrawingStyle(bgStyle);
            ctx->DrawRoundedRectangle(bgRect, style.cornerRadius);
        } else {
            ctx->DrawFilledRectangle(bgRect, style.backgroundColor);
        }
    }

    void UltraCanvasTooltipManager::DrawBorder() {
        Rect2Di borderRect(tooltipPosition.x, tooltipPosition.y, tooltipSize.x, tooltipSize.y);

        if (style.cornerRadius > 0) {
            ctx->SetStrokeColor(style.borderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            DrawingStyle borderStyle = GetRenderContext()->GetDrawingStyle();
            borderStyle.fillMode = FillMode::NoneFill;
            borderStyle.hasStroke = true;
            borderStyle.strokeColor = style.borderColor;
            borderStyle.strokeWidth = style.borderWidth;
            SetDrawingStyle(borderStyle);
            ctx->DrawRoundedRectangle(borderRect, style.cornerRadius);
        } else {
            ctx->DrawFilledRectangle(borderRect, Colors::Transparent, style.borderColor, style.borderWidth);
        }
    }

    void UltraCanvasTooltipManager::DrawText() {
        ctx->SetTextColor(style.textColor);
        ctx->SetFont(style.fontFamily, style.fontSize);

        float textX = tooltipPosition.x + style.paddingLeft;
        float textY = tooltipPosition.y + style.paddingTop + style.fontSize;

        for (const std::string& line : wrappedLines) {
            if (line == "\n") {
                textY += style.fontSize * 1.2f;
                continue;
            }

            UltraCanvas::DrawText(line, Point2Di(textX, textY));
            textY += style.fontSize * 1.2f;
        }
    }

} // namespace UltraCanvas