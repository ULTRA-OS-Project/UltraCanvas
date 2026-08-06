// UltraCanvasTooltipManager.cpp
// Implementation of tooltip system for UltraCanvas
// Version: 2.2.0
// Last Modified: 2026-08-06
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

    bool UltraCanvasTooltipManager::enabled = true;
//    Rect2Di UltraCanvasTooltipManager::screenBounds = Rect2Di(0, 0, 1920, 1080);

// ===== INTERNAL LAYOUT STATE =====
    // One laid-out block of the current tooltip content, positioned in
    // content-local coordinates (0,0 = inside the padding).
    struct TooltipBlockLayout {
        TooltipBlockType type = TooltipBlockType::Text;
        std::unique_ptr<ITextLayout> textLayout;   // Title/Text/Bullet text; Row label
        std::unique_ptr<ITextLayout> valueLayout;  // Row value
        Color swatch = Color(0, 0, 0, 0);
        int y = 0;
        int height = 0;
        int valueWidth = 0;                        // drawn width of the value column entry
    };

    static TooltipContent currentContent;
    static std::vector<TooltipBlockLayout> blockLayouts;
    static std::unique_ptr<ITextLayout> bulletGlyphLayout;
    static int rowLabelIndent = 0;   // label x-offset when any row carries a swatch

    // Row swatch and bullet metrics
    static constexpr int kSwatchSize = 9;
    static constexpr int kSwatchGap = 6;
    static constexpr int kBulletIndent = 13;
    static constexpr int kSeparatorMargin = 3;   // extra space above/below separators

    static std::string EscapeMarkup(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out += c;
            }
        }
        return out;
    }

    static std::string SpanMarkup(const TooltipStyle& style, float fontSize, bool bold,
                                  const std::string& inner) {
        return fmt::format("<span size=\"{}pt\" face=\"{}\"{}>{}</span>",
                           fontSize, style.fontFamily, bold ? " weight=\"bold\"" : "", inner);
    }

    // Plain-text projection of the content, kept for GetCurrentText()
    static std::string PlainTextOf(const TooltipContent& content) {
        std::string out;
        for (const auto& block : content.blocks) {
            if (block.type == TooltipBlockType::Separator) continue;
            if (!out.empty()) out += "\n";
            out += block.text;
            if (block.type == TooltipBlockType::Row) {
                out += ": " + block.value;
            }
        }
        return out;
    }

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

    void UltraCanvasTooltipManager::UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content,
                                                         const Point2Di& position, const TooltipStyle& newStyle) {
        if (!enabled) return;

        // If already showing for this element, just update content
        if (visible) {
            if (targetWindow != win) {
                HideTooltipImmediately();
            } else {
                if (content.Empty()) {
                    HideTooltipImmediately();
                    return;
                }
            }
        }
        if (content.Empty()) return;

        if (targetWindow != win || currentContent != content || style != newStyle) {
            renderCtx.reset();
        }
        style = newStyle;
        currentContent = content;
        currentText = PlainTextOf(content);
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

        debugOutput << "Tooltip requested. Text: " << currentText << std::endl;
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string &text,
                                                const Point2Di &position, const TooltipStyle& newStyle) {
        TooltipContent content;
        if (!text.empty()) {
            content.AddText(text);   // Pango markup allowed, matches legacy behavior
        }
        UpdateAndShowTooltip(win, content, position, newStyle);
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

    void UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const TooltipContent& content,
                                                                    const Point2Di& position, const TooltipStyle& newStyle) {
        UpdateAndShowTooltip(win, content, position, newStyle);
        CancelShowTimer();
        CancelHideTimer();
        pendingShow = false;
        pendingHide = false;
        visible = true;
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
        if (!visible || currentContent.Empty() || win != targetWindow) return nullptr;

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

        // Draw content blocks
        const int contentWidth = tooltipRect.width - style.paddingLeft - style.paddingRight;
        const double baseX = contentRect.x + style.paddingLeft;
        const double baseY = contentRect.y + style.paddingTop;

        for (const auto& bl : blockLayouts) {
            switch (bl.type) {
                case TooltipBlockType::Title:
                case TooltipBlockType::Text:
                    if (bl.textLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bl.textLayout, Point2Dd(baseX, baseY + bl.y));
                    }
                    break;

                case TooltipBlockType::Bullet:
                    if (bulletGlyphLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bulletGlyphLayout, Point2Dd(baseX, baseY + bl.y));
                    }
                    if (bl.textLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bl.textLayout, Point2Dd(baseX + kBulletIndent, baseY + bl.y));
                    }
                    break;

                case TooltipBlockType::Row: {
                    if (bl.swatch.a > 0) {
                        Rect2Di swatchRect(static_cast<int>(baseX),
                                           static_cast<int>(baseY) + bl.y + (bl.height - kSwatchSize) / 2,
                                           kSwatchSize, kSwatchSize);
                        renderCtx->DrawFilledRectangle(swatchRect, bl.swatch, 0, Colors::Transparent, 2.0f);
                    }
                    if (bl.textLayout) {
                        renderCtx->SetTextPaint(style.secondaryTextColor);
                        renderCtx->DrawTextLayout(*bl.textLayout, Point2Dd(baseX + rowLabelIndent, baseY + bl.y));
                    }
                    if (bl.valueLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bl.valueLayout,
                            Point2Dd(baseX + contentWidth - bl.valueWidth, baseY + bl.y));
                    }
                    break;
                }

                case TooltipBlockType::Separator: {
                    Rect2Di sepRect(static_cast<int>(baseX), static_cast<int>(baseY) + bl.y, contentWidth, 1);
                    renderCtx->DrawFilledRectangle(sepRect, style.separatorColor, 0, Colors::Transparent, 0);
                    break;
                }
            }
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
        blockLayouts.clear();
        bulletGlyphLayout.reset();
        rowLabelIndent = 0;
        if (currentContent.Empty() || !targetWindow) return;

        auto ctx = targetWindow->GetRenderContext();
        const int innerMax = std::max(20, style.maxWidth - style.paddingLeft - style.paddingRight);

        // Rows with a color swatch shift every label so the columns stay aligned
        bool anySwatch = false;
        bool anyBullet = false;
        for (const auto& block : currentContent.blocks) {
            if (block.type == TooltipBlockType::Row && block.swatch.a > 0) anySwatch = true;
            if (block.type == TooltipBlockType::Bullet) anyBullet = true;
        }
        rowLabelIndent = anySwatch ? kSwatchSize + kSwatchGap : 0;

        if (anyBullet) {
            bulletGlyphLayout = ctx->CreateTextLayout(SpanMarkup(style, style.fontSize, false, "•"), true);
        }

        // Pass 1: create layouts and measure natural widths
        int maxLabelWidth = 0, maxValueWidth = 0, maxBlockWidth = 0;
        for (const auto& block : currentContent.blocks) {
            TooltipBlockLayout bl;
            bl.type = block.type;
            bl.swatch = block.swatch;

            switch (block.type) {
                case TooltipBlockType::Title:
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.titleFontSize, true, EscapeMarkup(block.text)), true);
                    break;
                case TooltipBlockType::Text:
                    // Free text: Pango markup is interpreted, not escaped
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, block.text), true);
                    break;
                case TooltipBlockType::Bullet:
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, EscapeMarkup(block.text)), true);
                    break;
                case TooltipBlockType::Row:
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, EscapeMarkup(block.text)), true);
                    bl.valueLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, EscapeMarkup(block.value)), true);
                    maxLabelWidth = std::max(maxLabelWidth,
                        static_cast<int>(bl.textLayout->GetLayoutSize().width));
                    maxValueWidth = std::max(maxValueWidth,
                        static_cast<int>(bl.valueLayout->GetLayoutSize().width));
                    break;
                case TooltipBlockType::Separator:
                    break;
            }
            blockLayouts.push_back(std::move(bl));
        }

        // Column widths: labels keep their natural width up to 55% of the
        // available space; values get the rest
        const int rowSpace = innerMax - rowLabelIndent - style.columnGap;
        const int labelColWidth = std::min(maxLabelWidth, rowSpace * 55 / 100);
        const int valueColWidth = std::min(maxValueWidth, rowSpace - labelColWidth);
        if (maxLabelWidth > 0) {
            maxBlockWidth = std::max(maxBlockWidth,
                rowLabelIndent + labelColWidth + style.columnGap + valueColWidth);
        }

        // Pass 2: wrap over-wide blocks and take final measurements
        for (auto& bl : blockLayouts) {
            switch (bl.type) {
                case TooltipBlockType::Title:
                case TooltipBlockType::Text: {
                    int w = static_cast<int>(bl.textLayout->GetLayoutSize().width);
                    if (w > innerMax) {
                        bl.textLayout->SetExplicitWidth(innerMax);
                        w = innerMax;
                    }
                    maxBlockWidth = std::max(maxBlockWidth, w);
                    break;
                }
                case TooltipBlockType::Bullet: {
                    int w = static_cast<int>(bl.textLayout->GetLayoutSize().width);
                    if (w > innerMax - kBulletIndent) {
                        bl.textLayout->SetExplicitWidth(innerMax - kBulletIndent);
                        w = innerMax - kBulletIndent;
                    }
                    maxBlockWidth = std::max(maxBlockWidth, kBulletIndent + w);
                    break;
                }
                case TooltipBlockType::Row: {
                    if (static_cast<int>(bl.textLayout->GetLayoutSize().width) > labelColWidth) {
                        bl.textLayout->SetExplicitWidth(labelColWidth);
                    }
                    int vw = static_cast<int>(bl.valueLayout->GetLayoutSize().width);
                    if (vw > valueColWidth) {
                        bl.valueLayout->SetExplicitWidth(valueColWidth);
                        vw = valueColWidth;
                    }
                    bl.valueWidth = vw;
                    break;
                }
                case TooltipBlockType::Separator:
                    break;
            }
        }

        // Pass 3: stack the blocks vertically
        int y = 0;
        bool first = true;
        for (auto& bl : blockLayouts) {
            if (!first) y += style.rowSpacing;
            first = false;

            switch (bl.type) {
                case TooltipBlockType::Title:
                case TooltipBlockType::Text:
                case TooltipBlockType::Bullet:
                    bl.height = static_cast<int>(bl.textLayout->GetLayoutSize().height);
                    break;
                case TooltipBlockType::Row: {
                    int lh = static_cast<int>(bl.textLayout->GetLayoutSize().height);
                    int vh = static_cast<int>(bl.valueLayout->GetLayoutSize().height);
                    bl.height = std::max({lh, vh, bl.swatch.a > 0 ? kSwatchSize : 0});
                    break;
                }
                case TooltipBlockType::Separator:
                    y += kSeparatorMargin;
                    bl.height = 1;
                    break;
            }
            bl.y = y;
            y += bl.height;
            if (bl.type == TooltipBlockType::Separator) {
                y += kSeparatorMargin;
            }
        }

        tooltipRect.width = std::max(maxBlockWidth + style.paddingLeft + style.paddingRight, 20);
        tooltipRect.height = std::max(y + style.paddingTop + style.paddingBottom, 15);
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
