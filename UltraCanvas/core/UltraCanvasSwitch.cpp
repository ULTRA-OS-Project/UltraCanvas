// UltraCanvasSwitch.cpp
// Toggle switch rendering with optional orientation, thumb icons, and state labels.
// Version: 1.3.0
// Last Modified: 2026-08-04
// Author: UltraCanvas Framework

#include "UltraCanvasSwitch.h"
#include <algorithm>

namespace UltraCanvas {

    UltraCanvasSwitch::UltraCanvasSwitch(const std::string& identifier,
                                         float x, float y, float w, float h,
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

    UltraCanvasSwitch::SideLabelMetrics UltraCanvasSwitch::MeasureSideLabels() const {
        SideLabelMetrics metrics;
        metrics.height = visualStyle.sideLabelFontSize + 2.0f;

        IRenderContext* ctx = GetRenderContext();
        if (!ctx) {
            // No surface yet — fall back to the same conservative estimate the
            // outside-track label uses.
            const float estChar = visualStyle.sideLabelFontSize * 0.65f;
            metrics.offWidth = estChar * static_cast<float>(visualStyle.offText.size());
            metrics.onWidth  = estChar * static_cast<float>(visualStyle.onText.size());
            return metrics;
        }

        ctx->PushState();
        // Measure at both weights so toggling never shifts the track sideways.
        const FontWeight weights[2] = { visualStyle.sideLabelActiveWeight,
                                        visualStyle.sideLabelInactiveWeight };
        auto measure = [&](const std::string& text) -> float {
            if (text.empty()) return 0.0f;
            float widest = 0.0f;
            for (FontWeight weight : weights) {
                ctx->SetFontFace(visualStyle.sideLabelFontFamily, weight, FontSlant::Normal);
                ctx->SetFontSize(visualStyle.sideLabelFontSize);
                const Size2Di dims = ctx->GetTextLineDimensions(text);
                widest = std::max(widest, static_cast<float>(dims.width));
                metrics.height = std::max(metrics.height, static_cast<float>(dims.height));
            }
            return widest;
        };
        metrics.offWidth = measure(visualStyle.offText);
        metrics.onWidth  = measure(visualStyle.onText);
        ctx->PopState();

        return metrics;
    }

    Size2Df UltraCanvasSwitch::GetIndicatorSize() const {
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;
        Size2Df size = horizontal
                ? Size2Df{visualStyle.trackWidth, visualStyle.trackHeight}
                : Size2Df{visualStyle.trackHeight, visualStyle.trackWidth};

        if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::BothSides) {
            const SideLabelMetrics labels = MeasureSideLabels();
            const float pad = visualStyle.sideLabelPadding;
            if (horizontal) {
                if (labels.offWidth > 0.0f) size.width += labels.offWidth + pad;
                if (labels.onWidth > 0.0f)  size.width += labels.onWidth + pad;
                size.height = std::max(size.height, labels.height);
            } else {
                if (labels.onWidth > 0.0f)  size.height += labels.height + pad;
                if (labels.offWidth > 0.0f) size.height += labels.height + pad;
                size.width = std::max(size.width,
                                      std::max(labels.offWidth, labels.onWidth));
            }
            return size;
        }

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

        if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::BothSides) {
            // The leading label occupies the start of indicatorRect; centre the
            // track on the cross axis so taller text stays vertically aligned.
            const SideLabelMetrics labels = MeasureSideLabels();
            const float pad = visualStyle.sideLabelPadding;
            if (horizontal) {
                if (labels.offWidth > 0.0f) track.x += labels.offWidth + pad;
                track.y = indicatorRect.y + (indicatorRect.height - track.height) / 2.0f;
            } else {
                if (labels.onWidth > 0.0f) track.y += labels.height + pad;
                track.x = indicatorRect.x + (indicatorRect.width - track.width) / 2.0f;
            }
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
        } else if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::BothSides) {
            DrawSideLabels(ctx, track);
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
                pos.x = track.x + track.width - (textSize.width + pad);
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

    void UltraCanvasSwitch::DrawSideLabels(IRenderContext* ctx, const Rect2Df& track) {
        const bool horizontal = visualStyle.orientation == SwitchOrientation::Horizontal;
        const float pad = visualStyle.sideLabelPadding;
        const bool disabled = IsDisabled();

        // leading = left (horizontal) or above (vertical).
        auto drawLabel = [&](const std::string& text, bool active, bool leading) {
            if (text.empty()) return;

            ctx->SetFontFace(visualStyle.sideLabelFontFamily,
                             active ? visualStyle.sideLabelActiveWeight
                                    : visualStyle.sideLabelInactiveWeight,
                             FontSlant::Normal);
            ctx->SetFontSize(visualStyle.sideLabelFontSize);
            ctx->SetTextPaint(disabled
                    ? visualStyle.sideLabelDisabledColor
                    : (active ? visualStyle.sideLabelActiveColor
                              : visualStyle.sideLabelInactiveColor));

            const Size2Di textSize = ctx->GetTextLineDimensions(text);
            Point2Df pos;
            if (horizontal) {
                pos.x = leading ? (track.x - pad - textSize.width)
                                : (track.x + track.width + pad);
                pos.y = track.y + (track.height - textSize.height) / 2.0f;
            } else {
                pos.x = track.x + (track.width - textSize.width) / 2.0f;
                pos.y = leading ? (track.y - pad - textSize.height)
                                : (track.y + track.height + pad);
            }
            ctx->DrawText(text, pos);
        };

        if (horizontal) {
            drawLabel(visualStyle.offText, !IsChecked(), true);
            drawLabel(visualStyle.onText, IsChecked(), false);
        } else {
            // Vertical: ON is at the top, so the ON text leads.
            drawLabel(visualStyle.onText, IsChecked(), true);
            drawLabel(visualStyle.offText, !IsChecked(), false);
        }
    }

    void UltraCanvasSwitch::OnActivate() {
        // In BothSides mode a click on either text selects that side directly;
        // clicks on the track (and keyboard activation) toggle as usual.
        if (visualStyle.stateLabelPosition == SwitchStateLabelPosition::BothSides
            && visualStyle.sideLabelClickSelects && lastPointerValid) {
            const Rect2Df track = GetTrackRect();
            if (visualStyle.orientation == SwitchOrientation::Horizontal) {
                if (lastPointer.x < track.x) { SetChecked(false); return; }
                if (lastPointer.x > track.x + track.width) { SetChecked(true); return; }
            } else {
                if (lastPointer.y < track.y) { SetChecked(true); return; }
                if (lastPointer.y > track.y + track.height) { SetChecked(false); return; }
            }
        }
        Toggle();
    }

    bool UltraCanvasSwitch::OnEvent(const UCEvent& event) {
        // Remember where the pointer was so OnActivate() can tell a side-label
        // click from a track click; keyboard activation clears it.
        switch (event.type) {
            case UCEventType::MouseDown:
            case UCEventType::MouseDoubleClick:
            case UCEventType::MouseUp:
                lastPointer = event.pointer;
                lastPointerValid = true;
                break;
            case UCEventType::KeyDown:
                lastPointerValid = false;
                break;
            default:
                break;
        }
        return UltraCanvasLabeledToggleBase::OnEvent(event);
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
            float x, float y,
            const std::string& text, bool checked) {
        auto sw = std::make_shared<UltraCanvasSwitch>(identifier, x, y, 200, 40, text);
        sw->SetChecked(checked);
        // Content-size to track + label: clear the ctor-stamped pixel size.
        sw->size.width  = CSSLayout::Dimension::Auto();
        sw->size.height = CSSLayout::Dimension::Auto();
        return sw;
    }

    std::shared_ptr<UltraCanvasSwitch> UltraCanvasSwitch::CreateWithSideLabels(
            const std::string& identifier,
            float x, float y,
            const std::string& offLabel,
            const std::string& onLabel,
            bool checked) {
        auto sw = Create(identifier, x, y, "", checked);
        sw->SetSideLabels(offLabel, onLabel);
        return sw;
    }

} // namespace UltraCanvas
