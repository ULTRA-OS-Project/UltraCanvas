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
#include <cmath>
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

    // Margins the soft shadow needs around the tooltip body on each side
    static void GetShadowMargins(const TooltipStyle& style, int& left, int& top, int& right, int& bottom) {
        left = top = right = bottom = 0;
        if (!style.hasShadow || style.shadowColor.a == 0) return;
        int blur = std::max(0, style.shadowBlur);
        left = std::max(0, blur - style.shadowOffset.x);
        top = std::max(0, blur - style.shadowOffset.y);
        right = std::max(0, blur + style.shadowOffset.x);
        bottom = std::max(0, blur + style.shadowOffset.y);
    }

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

        int marginLeft, marginTop, marginRight, marginBottom;
        GetShadowMargins(style, marginLeft, marginTop, marginRight, marginBottom);

        renderCtx = CreateRenderContext({tooltipRect.width + marginLeft + marginRight,
                                         tooltipRect.height + marginTop + marginBottom}, win->GetNativeSurface());

        Rect2Di contentRect(marginLeft, marginTop, tooltipRect.width, tooltipRect.height);

        // Draw shadow first
        if (style.hasShadow && style.shadowColor.a > 0) {
            Rect2Di shadowRect(
                    contentRect.x + style.shadowOffset.x,
                    contentRect.y + style.shadowOffset.y,
                    contentRect.width,
                    contentRect.height
            );

            if (style.shadowBlur <= 0) {
                renderCtx->DrawFilledRectangle(shadowRect, style.shadowColor, 0, Colors::Transparent, style.cornerRadius);
            } else {
                // No blur primitive in IRenderContext, so approximate a
                // Gaussian falloff by stacking translucent rounded rectangles,
                // each inflated by one more pixel. The per-layer alpha is
                // chosen so the fully overlapped core reaches shadowColor.a.
                int layers = style.shadowBlur;
                float coreAlpha = style.shadowColor.a / 255.0f;
                float layerAlpha = 1.0f - std::pow(1.0f - coreAlpha, 1.0f / static_cast<float>(layers));
                Color layerColor = style.shadowColor;
                layerColor.a = static_cast<uint8_t>(std::clamp(layerAlpha * 255.0f + 0.5f, 1.0f, 255.0f));
                for (int i = layers; i >= 1; --i) {
                    Rect2Di inflated(shadowRect.x - i, shadowRect.y - i,
                                     shadowRect.width + 2 * i, shadowRect.height + 2 * i);
                    renderCtx->DrawFilledRectangle(inflated, layerColor, 0, Colors::Transparent,
                                                   style.cornerRadius + static_cast<float>(i));
                }
            }
        }

        // Draw tooltip background
        renderCtx->DrawFilledRectangle(contentRect, style.backgroundColor, style.borderWidth, style.borderColor, style.cornerRadius);

        // Draw text
        if (textLayout) {
            renderCtx->SetTextPaint(style.textColor);
            renderCtx->DrawTextLayout(*textLayout,
                Point2Dd(contentRect.x + style.paddingLeft,
                         contentRect.y + style.paddingTop));
        }

        return renderCtx.get();
    }

    Point2Di UltraCanvasTooltipManager::GetCompositePosition() {
        int marginLeft, marginTop, marginRight, marginBottom;
        GetShadowMargins(style, marginLeft, marginTop, marginRight, marginBottom);
        return Point2Di(tooltipRect.x - marginLeft, tooltipRect.y - marginTop);
    }

    void UltraCanvasTooltipManager::SetStyle(const TooltipStyle &newStyle) {
        if (style != newStyle) {
            renderCtx.reset();
        }
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