// UltraCanvasSwitch.cpp
// Toggle switch rendering with optional orientation, thumb icons, and state labels.
// Version: 1.1.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework

#include "UltraCanvasSwitch.h"
#include <algorithm>

namespace UltraCanvas {

    UltraCanvasSwitch::UltraCanvasSwitch(const std::string& identifier,
                                         long x, long y, long w, long h,
                                         const std::string& labelText)
            : UltraCanvasLabeledToggleBase(identifier, x, y, w, h, labelText) {}

    void UltraCanvasSwitch::SetCheckState(CheckedState state) {
        if (state == CheckedState::Indeterminate) state = CheckedState::Unchecked;
        UltraCanvasLabeledToggleBase::SetCheckState(state);
    }

    Color UltraCanvasSwitch::GetCurrentTrackColor() const {
        if (IsDisabled()) return visualStyle.trackDisabledColor;
        return IsChecked() ? visualStyle.trackOnColor : visualStyle.trackOffColor;
    }

    Color UltraCanvasSwitch::GetCurrentThumbColor() const {
        return IsDisabled() ? visualStyle.thumbDisabledColor : visualStyle.thumbColor;
    }

    float UltraCanvasSwitch::EstimateStateLabelExtent() const {
        // Conservative pre-render estimate; exact metrics are used at draw time.
        const float estChar = visualStyle.stateLabelFontSize * 0.65f;
        const size_t maxLen = std::max(visualStyle.onText.size(), visualStyle.offText.size());
        return estChar * static_cast<float>(maxLen);
    }

    Size2Df UltraCanvasSwitch::GetIndicatorSize() const {
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;
        Size2Df size = horizontal
                ? Size2Df{visualStyle.trackWidth, visualStyle.trackHeight}
                : Size2Df{visualStyle.trackHeight, visualStyle.trackWidth};

        if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::OutsideTrack) {
            const float labelExtent = EstimateStateLabelExtent();
            if (horizontal) {
                size.width += visualStyle.stateLabelOutsidePadding + labelExtent;
            } else {
                size.height += visualStyle.stateLabelOutsidePadding
                              + visualStyle.stateLabelFontSize + 4.0f;
            }
        }
        return size;
    }

    Rect2Df UltraCanvasSwitch::GetTrackRect() const {
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;
        const float trackLong  = visualStyle.trackWidth;
        const float trackShort = visualStyle.trackHeight;

        Rect2Df track = indicatorRect;
        if (horizontal) {
            track.width = trackLong;
            track.height = trackShort;
        } else {
            track.width = trackShort;
            track.height = trackLong;
        }
        return track;
    }

    void UltraCanvasSwitch::DrawIndicator(IRenderContext* ctx) {
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;
        const Rect2Df track = GetTrackRect();

        const float pillRadius = std::min(track.width, track.height) / 2.0f;
        const float trackRadius = (visualStyle.trackCornerRadius < 0.0f)
                ? pillRadius
                : visualStyle.trackCornerRadius;

        // Track
        ctx->DrawFilledRectangle(track,
                                 GetCurrentTrackColor(),
                                 visualStyle.borderWidth,
                                 visualStyle.trackBorderColor,
                                 trackRadius);

        // Inside-track state label (drawn before the thumb so the thumb sits on top of any overlap)
        if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::InsideTrack) {
            DrawStateLabelInsideTrack(ctx, track);
        }

        // Thumb position
        const float thumbRadius = (horizontal ? track.height : track.width) / 2.0f
                                  - visualStyle.thumbInset;
        Point2Df thumbCenter;
        if (horizontal) {
            thumbCenter.y = track.y + track.height / 2.0f;
            thumbCenter.x = IsChecked()
                    ? (track.x + track.width - thumbRadius - visualStyle.thumbInset)
                    : (track.x + thumbRadius + visualStyle.thumbInset);
        } else {
            // Vertical: ON at top, OFF at bottom
            thumbCenter.x = track.x + track.width / 2.0f;
            thumbCenter.y = IsChecked()
                    ? (track.y + thumbRadius + visualStyle.thumbInset)
                    : (track.y + track.height - thumbRadius - visualStyle.thumbInset);
        }

        ctx->DrawFilledCircle(thumbCenter, thumbRadius,
                              GetCurrentThumbColor(),
                              visualStyle.thumbBorderColor,
                              visualStyle.borderWidth);

        DrawThumbIcon(ctx, thumbCenter, thumbRadius);

        if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::OutsideTrack) {
            DrawStateLabelOutsideTrack(ctx, track);
        }
    }

    void UltraCanvasSwitch::DrawThumbIcon(IRenderContext* ctx,
                                          const Point2Df& thumbCenter,
                                          float thumbRadius) {
        if (visualStyle.thumbIconStyle == SwitchThumbIconStyle::Plain) return;

        const float inset = visualStyle.thumbIconInset;
        const float drawRadius = thumbRadius - inset;
        if (drawRadius <= 0.0f) return;

        const Rect2Df iconRect(thumbCenter.x - drawRadius,
                               thumbCenter.y - drawRadius,
                               drawRadius * 2.0f,
                               drawRadius * 2.0f);

        if (visualStyle.thumbIconStyle == SwitchThumbIconStyle::Check) {
            // Built-in checkmark: drawn only when ON; OFF stays as plain circle.
            if (!IsChecked()) return;

            const float cx = thumbCenter.x;
            const float cy = thumbCenter.y;
            const float size = drawRadius * 1.4f;  // checkmark spans ~70% of thumb diameter

            const float x1 = cx - size * 0.35f;
            const float y1 = cy;
            const float x2 = cx - size * 0.1f;
            const float y2 = cy + size * 0.25f;
            const float x3 = cx + size * 0.35f;
            const float y3 = cy - size * 0.25f;

            ctx->SetStrokeWidth(visualStyle.thumbIconStrokeWidth);
            ctx->SetStrokePaint(visualStyle.thumbIconOnColor);
            ctx->ClearPath();
            ctx->MoveTo(x1, y1);
            ctx->LineTo(x2, y2);
            ctx->LineTo(x3, y3);
            ctx->Stroke();
            return;
        }

        // CustomImage: pick the matching icon; null icon falls back to plain circle.
        UCImagePtr icon = IsChecked() ? visualStyle.thumbIconOn : visualStyle.thumbIconOff;
        if (!icon) return;

        if (visualStyle.thumbIconUseAsMask) {
            const Color tint = IsChecked()
                    ? visualStyle.thumbIconOnColor
                    : visualStyle.thumbIconOffColor;
            ctx->DrawMask(tint, *icon, iconRect, ImageFitMode::Contain);
        } else {
            ctx->DrawImage(*icon, iconRect, ImageFitMode::Contain);
        }
    }

    void UltraCanvasSwitch::DrawStateLabelInsideTrack(IRenderContext* ctx, const Rect2Df& track) {
        const std::string& text = IsChecked() ? visualStyle.onText : visualStyle.offText;
        if (text.empty()) return;

        const Color color = IsChecked() ? visualStyle.onTextColor : visualStyle.offTextColor;

        ctx->SetFontFace(visualStyle.stateLabelFontFamily,
                         visualStyle.stateLabelFontWeight,
                         FontSlant::Normal);
        ctx->SetFontSize(visualStyle.stateLabelFontSize);
        ctx->SetTextPaint(color);

        const Size2Di textSize = ctx->GetTextLineDimensions(text);
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;
        const float pad = visualStyle.stateLabelTrackPadding;

        Point2Df pos;
        if (horizontal) {
            if (IsChecked()) {
                pos.x = track.x + pad;
            } else {
                pos.x = track.width - (textSize.width + pad);
            }
            pos.y = track.y + (track.height - textSize.height) / 2.0f;
        } else {
            // Vertical: ON at top, label sits in bottom half; OFF at bottom, label sits in top half.
            const float halfHeight = track.height / 2.0f;
            const float regionY = IsChecked() ? (track.y + halfHeight) : track.y;
            pos.x = track.x + (track.width - textSize.width) / 2.0f;
            pos.y = regionY + (halfHeight - textSize.height) / 2.0f;
            pos.y = std::max<double>(pos.y, regionY + pad);
        }
        ctx->DrawText(text, pos);
    }

    void UltraCanvasSwitch::DrawStateLabelOutsideTrack(IRenderContext* ctx, const Rect2Df& track) {
        const std::string& text = IsChecked() ? visualStyle.onText : visualStyle.offText;
        if (text.empty()) return;

        const Color color = IsChecked() ? visualStyle.onTextColor : visualStyle.offTextColor;

        ctx->SetFontFace(visualStyle.stateLabelFontFamily,
                         visualStyle.stateLabelFontWeight,
                         FontSlant::Normal);
        ctx->SetFontSize(visualStyle.stateLabelFontSize);
        ctx->SetTextPaint(color);

        const Size2Di textSize = ctx->GetTextLineDimensions(text);
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;

        Point2Df pos;
        if (horizontal) {
            pos.x = track.x + track.width + visualStyle.stateLabelOutsidePadding;
            pos.y = track.y + (track.height - textSize.height) / 2.0f;
        } else {
            pos.x = track.x + (track.width - textSize.width) / 2.0f;
            pos.y = track.y + track.height + visualStyle.stateLabelOutsidePadding;
        }
        ctx->DrawText(text, pos);
    }

    void UltraCanvasSwitch::DrawFocusRingShape(IRenderContext* ctx) {
        const auto& base = visualStyle.base;
        const Rect2Df track = GetTrackRect();

        const float pillRadius = std::min(track.width, track.height) / 2.0f;
        const float baseRadius = (visualStyle.trackCornerRadius < 0.0f)
                ? pillRadius
                : visualStyle.trackCornerRadius;

        Rect2Df focusRect(
                track.x - base.focusRingWidth,
                track.y - base.focusRingWidth,
                track.width + 2 * base.focusRingWidth,
                track.height + 2 * base.focusRingWidth);
        ctx->DrawFilledRectangle(focusRect, base.focusRingColor,
                                 base.focusRingWidth, base.focusRingColor,
                                 baseRadius + base.focusRingWidth);
    }

    std::shared_ptr<UltraCanvasSwitch> UltraCanvasSwitch::Create(
            const std::string& identifier,
            long x, long y,
            const std::string& text, bool checked) {
        auto sw = std::make_shared<UltraCanvasSwitch>(identifier, x, y, 200, 30, text);
        sw->SetChecked(checked);
        sw->SetAutoSize(true);
        return sw;
    }

} // namespace UltraCanvas
