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
    std::unique_ptr<IRenderContext> UltraCanvasTooltipManager::renderCtx;
    Rect2Di UltraCanvasTooltipManager::tooltipRect;
    UltraCanvasWindowBase* UltraCanvasTooltipManager::targetWindow = nullptr;
//    Point2Di UltraCanvasTooltipManager::cursorPosition;
    bool UltraCanvasTooltipManager::visible = false;
    bool UltraCanvasTooltipManager::pendingShow = false;
    bool UltraCanvasTooltipManager::pendingHide = false;

    TimerId UltraCanvasTooltipManager::showTimerId = InvalidTimerId;
    TimerId UltraCanvasTooltipManager::hideTimerId = InvalidTimerId;

    TooltipStyle UltraCanvasTooltipManager::style;
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
        if (targetWindow != win || currentText != text || style != newStyle) {
            renderCtx.reset();
        }
        style = newStyle;
        currentText = text;
        targetWindow = win;
        CalculateTooltipLayout();
        UpdateTooltipPosition(position);

        // A pending hide for the same target is stale now
        CancelHideTimer();
        pendingHide = false;

        if (!visible) {
            CancelShowTimer();
            pendingShow = true;
            showTimerId = UltraCanvasApplication::GetInstance()->StartTimer(style.showDelay, false,
                [](TimerId) {
                    showTimerId = InvalidTimerId;
                    if (!pendingShow) return;
                    pendingShow = false;
                    visible = true;
                    targetWindow->RequestWindowComposition();
                    debugOutput << "Tooltip shown" << std::endl;
                });
        } else {
            targetWindow->RequestWindowComposition();
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
            hideTimerId = UltraCanvasApplication::GetInstance()->StartTimer(style.hideDelay, false,
                [](TimerId) {
                    hideTimerId = InvalidTimerId;
                    if (!pendingHide) return;
                    pendingHide = false;
                    visible = false;
                    if (targetWindow) {
                        targetWindow->RequestWindowComposition();
                    }
                    renderCtx.reset();
                    debugOutput << "Tooltip hidden" << std::endl;
                });
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
    }

    void UltraCanvasTooltipManager::HideTooltipImmediately() {
        CancelShowTimer();
        CancelHideTimer();
        pendingHide = false;
        pendingShow = false;
        visible = false;
        if (targetWindow) {
            targetWindow->RequestWindowComposition();
        }
        renderCtx.reset();
    }

    IRenderContext* UltraCanvasTooltipManager::Render(UltraCanvasWindowBase* win) {
        if (!visible || currentText.empty() || win != targetWindow) return nullptr;

        if (renderCtx) {
            return renderCtx.get();
        }

        renderCtx = CreateRenderContext({tooltipRect.width + style.shadowOffset.x,
                                         tooltipRect.height + style.shadowOffset.y}, win->GetNativeSurface());
        // Draw shadow first
        if (style.hasShadow) {
            Rect2Di shadowRect(
                    style.shadowOffset.x,
                    style.shadowOffset.y,
                    tooltipRect.width,
                    tooltipRect.height
            );

            renderCtx->DrawFilledRectangle(shadowRect, style.shadowColor, 0, Colors::Transparent, style.cornerRadius);
        }

        // Draw tooltip background
        renderCtx->DrawFilledRectangle(Rect2Di(0, 0, tooltipRect.width, tooltipRect.height), style.backgroundColor, style.borderWidth, style.borderColor, style.cornerRadius);

        // Draw text
        if (textLayout) {
            renderCtx->SetCurrentPaint(style.textColor);
            renderCtx->DrawTextLayout(*textLayout,
                Point2Df(style.paddingLeft,
                         style.paddingTop));
        }

        return renderCtx.get();
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
        tooltipRect.width = std::max(static_cast<int>(measured.width) + style.paddingLeft + style.paddingRight, 20);
        tooltipRect.height = std::max(static_cast<int>(measured.height) + style.paddingTop + style.paddingBottom, 15);
    }

    void UltraCanvasTooltipManager::UpdateTooltipPosition(const Point2Di &cursorPosition) {
        // Basic positioning relative to cursor
        int windowWidth = targetWindow->GetWidth();
        int windowHeight = targetWindow->GetHeight();

        tooltipRect.x = cursorPosition.x + style.offsetX;
        tooltipRect.y = cursorPosition.y + style.offsetY;

        // Keep tooltip on screen
        if (windowWidth > 0 && windowHeight > 0) {
            // Adjust horizontal position
            if (tooltipRect.x + tooltipRect.width > windowWidth) {
                tooltipRect.x = cursorPosition.x - style.offsetX - tooltipRect.width;
            }

            // Adjust vertical position
            if (tooltipRect.y + tooltipRect.height > windowHeight) {
                tooltipRect.y = cursorPosition.y - style.offsetY - tooltipRect.height;
            }

            // Ensure tooltip is not off-screen
            tooltipRect.x = std::max(tooltipRect.x, 0);
            tooltipRect.y = std::max(tooltipRect.y, 0);
        }
    }

} // namespace UltraCanvas