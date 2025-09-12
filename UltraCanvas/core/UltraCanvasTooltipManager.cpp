// UltraCanvasTooltipManager.cpp
// Implementation of tooltip system for UltraCanvas
// Version: 2.0.1
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasWindow.h"
#include <string>
#include <algorithm>
#include <iostream>

namespace UltraCanvas {
// ===== STATIC MEMBER DEFINITIONS =====
    std::string UltraCanvasTooltipManager::currentText;
    Point2Di UltraCanvasTooltipManager::tooltipPosition;
    UltraCanvasBaseWindow* UltraCanvasTooltipManager::targetWindow = nullptr;
//    Point2Di UltraCanvasTooltipManager::cursorPosition;
    bool UltraCanvasTooltipManager::visible = false;
    bool UltraCanvasTooltipManager::pendingShow = false;
    bool UltraCanvasTooltipManager::pendingHide = false;

    std::chrono::steady_clock::time_point UltraCanvasTooltipManager::hoverStartTime;
    std::chrono::steady_clock::time_point UltraCanvasTooltipManager::hideStartTime;
    float UltraCanvasTooltipManager::showDelay = 0.8f;
    float UltraCanvasTooltipManager::hideDelay = 0.5f;

    TooltipStyle UltraCanvasTooltipManager::style;
    Point2Di UltraCanvasTooltipManager::tooltipSize;
    std::vector<std::string> UltraCanvasTooltipManager::wrappedLines;

    bool UltraCanvasTooltipManager::enabled = true;
//    Rect2Di UltraCanvasTooltipManager::screenBounds = Rect2Di(0, 0, 1920, 1080);

    void UltraCanvasTooltipManager::UpdateAndShowTooltip(UltraCanvasWindow* win, const std::string &text,
                                                const Point2Di &position, const TooltipStyle& newStyle) {
        if (!enabled) return;

        // If already showing for this element, just update text
        if (visible) {
            if (targetWindow != win) {
                HideTooltipImmediately();
            } else {
                if (text.empty()) {
                    HideTooltipImmediately();
                    return;
                }
            }
        }
        style = newStyle;

        // Hide current tooltip if showing for different element
//        if (tooltipSource != element) {
//            HideTooltip();
//        }

//        tooltipSource = element;
        currentText = text;
        targetWindow = win;
        // Set position
//        cursorPosition = position;
        UpdateTooltipPosition(position);

        // Calculate layout
        CalculateTooltipLayout();

        // Start show timer
        if (!visible) {
            hoverStartTime = std::chrono::steady_clock::now();
            pendingShow = true;
            showDelay = style.showDelay;
        } else {
            targetWindow->RequestRedraw();
        }

        pendingHide = false;
        std::cout << "Tooltip requested. Text: " << text << std::endl;
    }

    void UltraCanvasTooltipManager::Update() {
        if (!enabled) return;

        auto now = std::chrono::steady_clock::now();

        // Handle pending show
        if (pendingShow) {
            float elapsed = std::chrono::duration<float>(now - hoverStartTime).count();
            if (elapsed >= showDelay) {
                visible = true;
                pendingShow = false;
                std::cout << "Tooltip shown" << std::endl;
                targetWindow->RequestRedraw();
                return;
            }
        }

        // Handle pending hide
        if (pendingHide) {
            float elapsed = std::chrono::duration<float>(now - hideStartTime).count();
            if (elapsed >= hideDelay && visible) {
                visible = false;
                pendingHide = false;
                targetWindow->RequestRedraw();
                std::cout << "Tooltip hidden" << std::endl;
                return;
            }
        }

        // Update tooltip position if following cursor
//        if (visible && style.followCursor) {
//            UpdateTooltipPosition();
//        }
    }

    void UltraCanvasTooltipManager::HideTooltip() {
        if (!visible && !pendingShow) return;

        pendingShow = false;
        pendingHide = true;

        if (visible) {
            hideStartTime = std::chrono::steady_clock::now();
            hideDelay = style.hideDelay;
        } else {
            visible = false;
        }

        std::cout << "Tooltip hide requested" << std::endl;
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(UltraCanvasWindow* win, const std::string &text,
                                                                    const Point2Di &position, const TooltipStyle& newStyle) {
        UpdateAndShowTooltip(win, text, position, newStyle);
        visible = true;
        pendingShow = false;
        targetWindow->RequestRedraw();
    }

    void UltraCanvasTooltipManager::HideTooltipImmediately() {
        pendingHide = false;
        pendingShow = false;
        visible = false;
        targetWindow->RequestRedraw();
    }

    void UltraCanvasTooltipManager::Render(const UltraCanvasBaseWindow* win) {
        if (!visible || currentText.empty() || win != targetWindow) return;
        IRenderContext *ctx = targetWindow->GetRenderContext();

        ctx->PushState();

        // Draw shadow first
        if (style.hasShadow) {
            DrawShadow(ctx);
        }

        // Draw tooltip background
        DrawBackground(ctx);

        // Draw border
        if (style.borderWidth > 0) {
            DrawBorder(ctx);
        }

        // Draw text
        DrawText(ctx);

        ctx->PopState();
    }

    void UltraCanvasTooltipManager::SetStyle(const TooltipStyle &newStyle) {
        style = newStyle;
        if (visible) {
            CalculateTooltipLayout(); // Recalculate if currently visible
            targetWindow->RequestRedraw();
        }
    }

//    void UltraCanvasTooltipManager::UpdateCursorPosition(const Point2Di &position) {
//        cursorPosition = position;
//
//        if (visible && style.followCursor) {
//            UpdateTooltipPosition();
//        }
//    }

//    void UltraCanvasTooltipManager::OnElementHover(UltraCanvasElement *element, const std::string &tooltipText,
//                                                   const Point2Di &mousePosition) {
//        UpdateCursorPosition(mousePosition);
//
//        if (hoveredElement != element) {
//            hoveredElement = element;
//
//            if (!tooltipText.empty()) {
//                ShowTooltip(element, tooltipText, mousePosition);
//            } else {
//                HideTooltip();
//            }
//        }
//    }
//
//    void UltraCanvasTooltipManager::OnElementLeave(UltraCanvasElement *element) {
//        if (hoveredElement == element) {
//            hoveredElement = nullptr;
//            HideTooltip();
//        }
//    }

    void UltraCanvasTooltipManager::CalculateTooltipLayout() {
        if (currentText.empty() || !targetWindow) return;

        // Set up text style for measurement
        // Word wrap the text
        wrappedLines = WrapText(currentText, style.maxWidth - style.paddingLeft - style.paddingRight);

        // Calculate size
        float maxLineWidth = 0;
        float totalHeight = 0;
        int linesCount = 0;
        for (const std::string& line : wrappedLines) {
            int txtW, txtH;
            targetWindow->GetRenderContext()->MeasureText(line, txtW, txtH);
            maxLineWidth = std::max(maxLineWidth, (float )txtW);
            totalHeight += txtH; // Line height
            linesCount++;
        }

        if (linesCount > 1) {
            totalHeight = totalHeight * 1.2;
        }

        tooltipSize.x = maxLineWidth + style.paddingLeft + style.paddingRight;
        tooltipSize.y = totalHeight + style.paddingTop + style.paddingBottom;

        // Ensure minimum size
        tooltipSize.x = std::max(tooltipSize.x, 20);
        tooltipSize.y = std::max(tooltipSize.y, 15);
    }

    void UltraCanvasTooltipManager::UpdateTooltipPosition(const Point2Di &cursorPosition) {
        // Basic positioning relative to cursor
        int windowWidth = targetWindow->GetWidth();
        int windowHeight = targetWindow->GetHeight();

        tooltipPosition.x = cursorPosition.x + style.offsetX;
        tooltipPosition.y = cursorPosition.y + style.offsetY;

        // Keep tooltip on screen
        if (windowWidth > 0 && windowHeight > 0) {
            // Adjust horizontal position
            if (tooltipPosition.x + tooltipSize.x > windowWidth) {
                tooltipPosition.x = cursorPosition.x - style.offsetX - tooltipSize.x;
            }

            // Adjust vertical position
            if (tooltipPosition.y + tooltipSize.y > windowHeight) {
                tooltipPosition.y = cursorPosition.y - style.offsetY - tooltipSize.y;
            }

            // Ensure tooltip is not off-screen
            tooltipPosition.x = std::max(tooltipPosition.x, 0);
            tooltipPosition.y = std::max(tooltipPosition.y, 0);
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
        auto ctx = targetWindow->GetRenderContext();
        for (const std::string& word : words) {
            std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
            float lineWidth = ctx->GetTextWidth(testLine);

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

    void UltraCanvasTooltipManager::DrawShadow(IRenderContext* ctx) {
        Rect2Di shadowRect(
                tooltipPosition.x + style.shadowOffset.x,
                tooltipPosition.y + style.shadowOffset.y,
                tooltipSize.x,
                tooltipSize.y
        );

        ctx->SetFillColor(style.shadowColor);
        if (style.cornerRadius > 0) {
            ctx->FillRoundedRectangle(shadowRect, style.cornerRadius);
        } else {
            ctx->FillRectangle(shadowRect);
        }
    }

    void UltraCanvasTooltipManager::DrawBackground(IRenderContext* ctx) {
        Rect2Di bgRect(tooltipPosition.x, tooltipPosition.y, tooltipSize.x, tooltipSize.y);

        ctx->SetFillColor(style.backgroundColor);
        if (style.cornerRadius > 0) {
            ctx->FillRoundedRectangle(bgRect, style.cornerRadius);
        } else {
            ctx->FillRectangle(bgRect);
        }
    }

    void UltraCanvasTooltipManager::DrawBorder(IRenderContext* ctx) {
        Rect2Di borderRect(tooltipPosition.x, tooltipPosition.y, tooltipSize.x, tooltipSize.y);

        ctx->SetStrokeColor(style.borderColor);
        ctx->SetStrokeWidth(style.borderWidth);
        if (style.cornerRadius > 0) {
            ctx->DrawRoundedRectangle(borderRect, style.cornerRadius);
        } else {
            ctx->DrawRectangle(borderRect);
        }
    }

    void UltraCanvasTooltipManager::DrawText(IRenderContext* ctx) {
        ctx->SetTextColor(style.textColor);
        ctx->SetFont(style.fontFamily, style.fontSize);

        float textX = tooltipPosition.x + style.paddingLeft;
        float textY = tooltipPosition.y + style.paddingTop;
        float lineHeight = (tooltipSize.y - style.paddingTop - style.paddingBottom) / wrappedLines.size();
        for (const std::string& line : wrappedLines) {
            if (line == "\n") {
                textY += lineHeight;
                continue;
            }

            ctx->DrawText(line, textX, textY);
            textY += lineHeight;
        }
    }

} // namespace UltraCanvas