// UltraCanvasTooltipManager.cpp
// Implementation of tooltip system for UltraCanvas
// Version: 2.1.0
// Last Modified: 2026-04-23
// Author: UltraCanvas Framework

#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
#include <string>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <fmt/os.h>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
// ===== STATIC MEMBER DEFINITIONS =====
    std::string UltraCanvasTooltipManager::currentText;
    Point2Di UltraCanvasTooltipManager::tooltipPosition;
    UltraCanvasWindowBase* UltraCanvasTooltipManager::targetWindow = nullptr;
//    Point2Di UltraCanvasTooltipManager::cursorPosition;
    bool UltraCanvasTooltipManager::visible = false;
    bool UltraCanvasTooltipManager::pendingShow = false;
    bool UltraCanvasTooltipManager::pendingHide = false;

    TimerId UltraCanvasTooltipManager::showTimerId = InvalidTimerId;
    TimerId UltraCanvasTooltipManager::hideTimerId = InvalidTimerId;

    TooltipStyle UltraCanvasTooltipManager::style;
    Point2Di UltraCanvasTooltipManager::tooltipSize;
    std::unique_ptr<ITextLayout> UltraCanvasTooltipManager::textLayout;

    bool UltraCanvasTooltipManager::enabled = true;
//    Rect2Di UltraCanvasTooltipManager::screenBounds = Rect2Di(0, 0, 1920, 1080);

    void UltraCanvasTooltipManager::CancelShowTimer() {
        if (showTimerId != InvalidTimerId) {
            UltraCanvasApplication::GetInstance()->StopTimer(showTimerId);
            showTimerId = InvalidTimerId;
        }
    }

    void UltraCanvasTooltipManager::CancelHideTimer() {
        if (hideTimerId != InvalidTimerId) {
            UltraCanvasApplication::GetInstance()->StopTimer(hideTimerId);
            hideTimerId = InvalidTimerId;
        }
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string &text,
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

        currentText = text;
        targetWindow = win;
        UpdateTooltipPosition(position);
        CalculateTooltipLayout();

        // A pending hide for the same target is stale now
        CancelHideTimer();
        pendingHide = false;

        if (!visible) {
            CancelShowTimer();
            pendingShow = true;
            auto ms = std::chrono::milliseconds(static_cast<int>(style.showDelay * 1000.0f));
            showTimerId = UltraCanvasApplication::GetInstance()->StartTimer(ms, false,
                [](TimerId) {
                    showTimerId = InvalidTimerId;
                    if (!pendingShow) return;
                    pendingShow = false;
                    visible = true;
                    if (targetWindow) targetWindow->RequestRedraw();
                    debugOutput << "Tooltip shown" << std::endl;
                });
        } else {
            targetWindow->RequestRedraw();
        }

        debugOutput << "Tooltip requested. Text: " << text << std::endl;
    }

    void UltraCanvasTooltipManager::HideTooltip() {
        if (!visible && !pendingShow) return;

        // Cancel any in-flight show
        CancelShowTimer();
        pendingShow = false;

        if (visible) {
            CancelHideTimer();
            pendingHide = true;
            auto ms = std::chrono::milliseconds(static_cast<int>(style.hideDelay * 1000.0f));
            hideTimerId = UltraCanvasApplication::GetInstance()->StartTimer(ms, false,
                [](TimerId) {
                    hideTimerId = InvalidTimerId;
                    if (!pendingHide) return;
                    pendingHide = false;
                    visible = false;
                    if (targetWindow) targetWindow->RequestRedraw();
                    debugOutput << "Tooltip hidden" << std::endl;
                });
        } else {
            visible = false;
        }

        debugOutput << "Tooltip hide requested" << std::endl;
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string &text,
                                                                    const Point2Di &position, const TooltipStyle& newStyle) {
        UpdateAndShowTooltip(win, text, position, newStyle);
        CancelShowTimer();
        CancelHideTimer();
        pendingShow = false;
        pendingHide = false;
        visible = true;
        if (targetWindow) targetWindow->RequestRedraw();
    }

    void UltraCanvasTooltipManager::HideTooltipImmediately() {
        CancelShowTimer();
        CancelHideTimer();
        pendingHide = false;
        pendingShow = false;
        visible = false;
        if (targetWindow) targetWindow->RequestRedraw();
    }

    void UltraCanvasTooltipManager::Render(IRenderContext* ctx, const UltraCanvasWindowBase* win) {
        if (!visible || currentText.empty() || win != targetWindow) return;

        ctx->PushState();

        // Draw shadow first
        if (style.hasShadow) {
            Rect2Di shadowRect(
                    tooltipPosition.x + style.shadowOffset.x,
                    tooltipPosition.y + style.shadowOffset.y,
                    tooltipSize.x,
                    tooltipSize.y
            );

            ctx->DrawFilledRectangle(shadowRect, style.shadowColor, 0, Colors::Transparent, style.cornerRadius);
        }

        // Draw tooltip background
        Rect2Di bgRect(tooltipPosition.x, tooltipPosition.y, tooltipSize.x, tooltipSize.y);
        ctx->DrawFilledRectangle(bgRect, style.backgroundColor, style.borderWidth, style.borderColor, style.cornerRadius);

        // Draw text
        if (textLayout) {
            ctx->SetCurrentPaint(style.textColor);
            ctx->DrawTextLayout(*textLayout,
                Point2Df(tooltipPosition.x + style.paddingLeft,
                         tooltipPosition.y + style.paddingTop));
        }

        ctx->PopState();
    }

    void UltraCanvasTooltipManager::SetStyle(const TooltipStyle &newStyle) {
        style = newStyle;
        if (visible) {
            CalculateTooltipLayout(); // Recalculate if currently visible
            targetWindow->RequestRedraw();
        }
    }


    void UltraCanvasTooltipManager::CalculateTooltipLayout() {
        if (currentText.empty() || !targetWindow) return;

        auto ctx = targetWindow->GetRenderContext();
        auto markup = fmt::format("<span size=\"{}pt\" face=\"{}\">{}</span>", style.fontSize, style.fontFamily, currentText);
        textLayout = ctx->CreateTextLayout(markup, true);

        //textLayout->SetWrap(TextWrap::WrapWordChar);
        textLayout->SetExplicitWidth(style.maxWidth - style.paddingLeft - style.paddingRight);

        auto measured = textLayout->GetLayoutSize();
        tooltipSize.x = std::max(static_cast<int>(measured.width) + style.paddingLeft + style.paddingRight, 20);
        tooltipSize.y = std::max(static_cast<int>(measured.height) + style.paddingTop + style.paddingBottom, 15);
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

} // namespace UltraCanvas