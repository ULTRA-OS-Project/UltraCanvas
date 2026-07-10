// core/UltraCanvasStepper.cpp
// Platform-independent stepper / wizard-progress control implementation.
// Version: 1.0.1
// Last Modified: 2026-07-10
// Author: UltraCanvas Framework

#include "UltraCanvasStepper.h"
#include "UltraCanvasApplication.h"
#include <string>

namespace UltraCanvas {

    UltraCanvasStepper::UltraCanvasStepper(const std::string& identifier,
                                           float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

// ===================================================================
// STEPS
// ===================================================================

    int UltraCanvasStepper::AddStep(const std::string& title, const std::string& description) {
        steps.emplace_back(title, description);
        RequestRedraw();
        return static_cast<int>(steps.size()) - 1;
    }

    void UltraCanvasStepper::SetSteps(const std::vector<StepItem>& newSteps) {
        steps = newSteps;
        for (auto& s : steps) {
            if (!s.iconPath.empty() && !s.iconImage) s.iconImage = UCImage::Get(s.iconPath);
        }
        currentStep = std::max(0, std::min(currentStep, GetStepCount() - 1));
        RequestRedraw();
    }

    void UltraCanvasStepper::ClearSteps() {
        steps.clear();
        currentStep = 0;
        RequestRedraw();
    }

    StepItem* UltraCanvasStepper::GetStep(int index) {
        if (index < 0 || index >= GetStepCount()) return nullptr;
        return &steps[index];
    }

    void UltraCanvasStepper::SetStepError(int index, bool error) {
        if (auto* s = GetStep(index)) { s->error = error; RequestRedraw(); }
    }

    void UltraCanvasStepper::SetStepDisabled(int index, bool disabled) {
        if (auto* s = GetStep(index)) { s->disabled = disabled; RequestRedraw(); }
    }

    void UltraCanvasStepper::SetStepIcon(int index, const std::string& iconPath) {
        if (auto* s = GetStep(index)) {
            s->iconPath = iconPath;
            s->iconImage = iconPath.empty() ? nullptr : UCImage::Get(iconPath);
            RequestRedraw();
        }
    }

    StepState UltraCanvasStepper::GetStepState(int index) const {
        if (index < 0 || index >= GetStepCount()) return StepState::Upcoming;
        const StepItem& s = steps[index];
        if (s.disabled) return StepState::Disabled;
        if (s.error)    return StepState::Error;
        if (index < currentStep) return StepState::Completed;
        if (index == currentStep) return StepState::Active;
        return StepState::Upcoming;
    }

// ===================================================================
// CURRENT STEP / NAVIGATION
// ===================================================================

    void UltraCanvasStepper::SetCurrentStep(int index, bool runCallback) {
        if (steps.empty()) return;
        int ni = std::max(0, std::min(index, GetStepCount() - 1));
        if (steps[ni].disabled) return;
        if (ni != currentStep) {
            currentStep = ni;
            if (runCallback && onStepChanged) onStepChanged(currentStep);
            RequestRedraw();
        }
    }

    bool UltraCanvasStepper::NextStep(bool runCallback) {
        for (int i = currentStep + 1; i < GetStepCount(); ++i) {
            if (!steps[i].disabled) { SetCurrentStep(i, runCallback); return true; }
        }
        return false;
    }

    bool UltraCanvasStepper::PrevStep(bool runCallback) {
        for (int i = currentStep - 1; i >= 0; --i) {
            if (!steps[i].disabled) { SetCurrentStep(i, runCallback); return true; }
        }
        return false;
    }

    bool UltraCanvasStepper::IsComplete() const {
        return !steps.empty() && currentStep >= GetStepCount() - 1;
    }

    bool UltraCanvasStepper::IsClickable(int index) const {
        if (navigation == StepperNavigation::Display) return false;
        if (index < 0 || index >= GetStepCount()) return false;
        if (steps[index].disabled) return false;
        if (navigation == StepperNavigation::NonLinear) return true;
        return index <= currentStep;   // Linear: completed/active are reachable
    }

// ===================================================================
// LAYOUT
// ===================================================================

    void UltraCanvasStepper::ComputeLayout(std::vector<Rect2Df>& markerRects,
                                           std::vector<Rect2Df>& slotRects) const {
        markerRects.clear();
        slotRects.clear();
        int n = GetStepCount();
        if (n == 0) return;

        Rect2Df b = GetLocalBounds();
        float m = style.markerSize;
        float pad = style.edgePadding;

        // The hover/selection ring is stroked at radius + 2 with a 2px pen, so
        // it reaches 3px past the marker circle; keep that overhang (plus 1px
        // for antialiasing) inside the element bounds, or the parent clip cuts
        // the top of the ring on the active/hovered step.
        const float ringOverhang = 4.0f;

        std::vector<float> centersMain;   // along the primary axis
        centersMain.reserve(n);

        if (orientation == StepperOrientation::Horizontal) {
            float cy = showLabels ? (b.y + ringOverhang + m / 2.0f) : (b.y + b.height / 2.0f);
            float first = b.x + pad + m / 2.0f;
            float last  = b.x + b.width - pad - m / 2.0f;
            float span  = (n > 1) ? (last - first) / (n - 1) : 0.0f;
            for (int i = 0; i < n; ++i) {
                float cx = (n > 1) ? (first + i * span) : (b.x + b.width / 2.0f);
                centersMain.push_back(cx);
                markerRects.emplace_back(cx - m / 2.0f, cy - m / 2.0f, m, m);
            }
            for (int i = 0; i < n; ++i) {
                float x0 = (i == 0) ? b.x : (centersMain[i - 1] + centersMain[i]) / 2.0f;
                float x1 = (i == n - 1) ? (b.x + b.width) : (centersMain[i] + centersMain[i + 1]) / 2.0f;
                slotRects.emplace_back(x0, b.y, x1 - x0, b.height);
            }
        } else {
            float cx = b.x + pad + m / 2.0f;
            float first = b.y + pad + m / 2.0f;
            float last  = b.y + b.height - pad - m / 2.0f;
            float span  = (n > 1) ? (last - first) / (n - 1) : 0.0f;
            for (int i = 0; i < n; ++i) {
                float cyc = (n > 1) ? (first + i * span) : (b.y + b.height / 2.0f);
                centersMain.push_back(cyc);
                markerRects.emplace_back(cx - m / 2.0f, cyc - m / 2.0f, m, m);
            }
            for (int i = 0; i < n; ++i) {
                float y0 = (i == 0) ? b.y : (centersMain[i - 1] + centersMain[i]) / 2.0f;
                float y1 = (i == n - 1) ? (b.y + b.height) : (centersMain[i] + centersMain[i + 1]) / 2.0f;
                slotRects.emplace_back(b.x, y0, b.width, y1 - y0);
            }
        }
    }

// ===================================================================
// RENDERING
// ===================================================================

    void UltraCanvasStepper::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        int n = GetStepCount();
        if (n == 0) return;

        std::vector<Rect2Df> markerRects, slotRects;
        ComputeLayout(markerRects, slotRects);

        // Connectors first (behind markers).
        for (int i = 0; i < n - 1; ++i) {
            bool completed = (GetStepState(i) == StepState::Completed);
            RenderConnector(ctx, markerRects[i], markerRects[i + 1], completed);
        }
        // Markers.
        for (int i = 0; i < n; ++i) RenderMarker(ctx, markerRects[i], i);
        // Labels.
        if (showLabels) {
            for (int i = 0; i < n; ++i) RenderLabel(ctx, markerRects[i], slotRects[i], i);
        }
    }

    void UltraCanvasStepper::RenderConnector(IRenderContext* ctx, const Rect2Df& a,
                                             const Rect2Df& b, bool completed) {
        Color c = completed ? style.connectorCompletedColor : style.connectorColor;
        ctx->SetStrokePaint(c);
        ctx->SetStrokeWidth(style.connectorThickness);
        if (orientation == StepperOrientation::Horizontal) {
            float y = a.y + a.height / 2.0f;
            ctx->DrawLine(Point2Dd(a.x + a.width, y), Point2Dd(b.x, y));
        } else {
            float x = a.x + a.width / 2.0f;
            ctx->DrawLine(Point2Dd(x, a.y + a.height), Point2Dd(x, b.y));
        }
    }

    void UltraCanvasStepper::RenderMarker(IRenderContext* ctx, const Rect2Df& rect, int index) {
        StepState state = GetStepState(index);
        Point2Dd center(rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f);
        float radius = style.markerSize / 2.0f;

        // Custom vector/image marker.
        if (markerStyle == StepMarkerStyle::Custom && steps[index].iconImage) {
            ctx->DrawImage(*steps[index].iconImage,
                           Rect2Dd(rect.x, rect.y, rect.width, rect.height),
                           ImageFitMode::Contain);
            if (state != StepState::Upcoming) {
                Color ring = (state == StepState::Error)    ? style.errorColor
                           : (state == StepState::Completed) ? style.completedColor
                           : (state == StepState::Disabled)  ? style.disabledColor
                                                             : style.activeColor;
                ctx->SetStrokePaint(ring);
                ctx->SetStrokeWidth(2.0f);
                ctx->DrawCircle(center, radius);
            }
            return;
        }

        // Dot style: minimal filled dot, no number.
        if (markerStyle == StepMarkerStyle::Dot) {
            float r = radius * 0.5f;
            switch (state) {
                case StepState::Completed: ctx->SetFillPaint(style.completedColor); ctx->FillCircle(center, r); break;
                case StepState::Active:    ctx->SetFillPaint(style.activeColor);    ctx->FillCircle(center, r * 1.15f); break;
                case StepState::Error:     ctx->SetFillPaint(style.errorColor);     ctx->FillCircle(center, r); break;
                case StepState::Disabled:  ctx->SetFillPaint(style.disabledColor);  ctx->FillCircle(center, r); break;
                default:
                    ctx->SetFillPaint(style.markerBackground); ctx->FillCircle(center, r);
                    ctx->SetStrokePaint(style.upcomingColor);  ctx->SetStrokeWidth(1.5f); ctx->DrawCircle(center, r);
                    break;
            }
            return;
        }

        // NumberedCircle / IconCheck.
        Color fill, border(0, 0, 0, 0), glyphColor;
        std::string glyph;
        std::string number = std::to_string(index + 1);
        bool hasBorder = false;

        switch (state) {
            case StepState::Completed:
                fill = style.completedColor; glyphColor = style.completedGlyphColor;
                glyph = style.completedShowsCheck ? "\xE2\x9C\x93" : number;   // ✓
                break;
            case StepState::Active:
                fill = style.activeColor; glyphColor = style.activeGlyphColor; glyph = number;
                break;
            case StepState::Error:
                fill = style.errorColor; glyphColor = style.errorGlyphColor; glyph = "!";
                break;
            case StepState::Disabled:
                fill = style.disabledColor; glyphColor = style.disabledTextColor; glyph = number;
                break;
            default: // Upcoming
                fill = style.markerBackground; glyphColor = style.upcomingGlyphColor;
                border = style.upcomingColor; hasBorder = true; glyph = number;
                break;
        }

        ctx->SetFillPaint(fill);
        ctx->FillCircle(center, radius);
        if (hasBorder) {
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.6f);
            ctx->DrawCircle(center, radius);
        }
        // Hover ring for a clickable step.
        if (hoveredStep == index && IsClickable(index)) {
            ctx->SetStrokePaint(style.activeColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawCircle(center, radius + 2.0f);
        }

        // Glyph.
        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize = style.markerSize * 0.44;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);
        ctx->SetTextPaint(glyphColor);
        Point2Di ts = ctx->GetTextDimension(glyph);
        ctx->DrawText(glyph, Point2Di(static_cast<int>(center.x) - ts.x / 2,
                                      static_cast<int>(center.y) - ts.y / 2));
    }

    void UltraCanvasStepper::RenderLabel(IRenderContext* ctx, const Rect2Df& markerRect,
                                         const Rect2Df& slotRect, int index) {
        const StepItem& s = steps[index];
        if (s.title.empty() && s.description.empty()) return;
        StepState state = GetStepState(index);

        Color titleCol = (state == StepState::Active)    ? style.activeTitleColor
                       : (state == StepState::Error)     ? style.errorColor
                       : (state == StepState::Disabled)  ? style.disabledTextColor
                                                         : style.titleColor;
        Color descCol = (state == StepState::Disabled) ? style.disabledTextColor
                                                       : style.descriptionColor;

        auto drawText = [&](const std::string& text, float x, float y, const Color& col,
                            float size, bool bold, bool centered) {
            if (text.empty()) return 0;
            FontStyle fs;
            fs.fontFamily = style.fontFamily;
            fs.fontSize = size;
            fs.fontWeight = bold ? FontWeight::Bold : FontWeight::Normal;
            ctx->SetFontStyle(fs);
            ctx->SetTextPaint(col);
            Point2Di ts = ctx->GetTextDimension(text);
            float tx = centered ? (x - ts.x / 2.0f) : x;
            ctx->DrawText(text, Point2Di(static_cast<int>(tx), static_cast<int>(y)));
            return ts.y;
        };

        if (orientation == StepperOrientation::Horizontal) {
            float cx = slotRect.x + slotRect.width / 2.0f;
            float y = markerRect.y + markerRect.height + style.labelGap;
            int th = drawText(s.title, cx, y, titleCol, style.titleFontSize, true, true);
            if (!s.description.empty()) {
                drawText(s.description, cx, y + th + 3.0f, descCol, style.descriptionFontSize, false, true);
            }
        } else {
            float x = markerRect.x + markerRect.width + style.labelGap;
            float y = markerRect.y + (s.description.empty()
                          ? (markerRect.height - style.titleFontSize) / 2.0f - 1.0f
                          : 2.0f);
            int th = drawText(s.title, x, y, titleCol, style.titleFontSize, true, false);
            if (!s.description.empty()) {
                drawText(s.description, x, y + th + 3.0f, descCol, style.descriptionFontSize, false, false);
            }
        }
    }

// ===================================================================
// EVENTS
// ===================================================================

    int UltraCanvasStepper::HitTest(const Point2Di& p) const {
        std::vector<Rect2Df> markerRects, slotRects;
        ComputeLayout(markerRects, slotRects);
        Point2Df pf(static_cast<float>(p.x), static_cast<float>(p.y));
        for (int i = 0; i < static_cast<int>(slotRects.size()); ++i) {
            if (slotRects[i].Contains(pf)) return i;
        }
        return -1;
    }

    bool UltraCanvasStepper::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            // A rapid second click arrives as a double-click instead of a
            // MouseDown; step markers must react to every click.
            case UCEventType::MouseDoubleClick:
            case UCEventType::MouseDown: {
                int i = HitTest(event.pointer);
                if (i < 0) return false;
                if (IsClickable(i)) {
                    SetFocus(true);
                    if (onStepClicked) onStepClicked(i);
                    SetCurrentStep(i, true);
                    return true;
                }
                return false;
            }
            case UCEventType::MouseMove: {
                int i = HitTest(event.pointer);
                int nh = (i >= 0 && IsClickable(i)) ? i : -1;
                mouseCursor = (nh >= 0) ? UCMouseCursor::Hand : UCMouseCursor::Default;
                if (nh != hoveredStep) { hoveredStep = nh; RequestRedraw(); }
                return i >= 0;
            }
            case UCEventType::MouseLeave:
                if (hoveredStep != -1) { hoveredStep = -1; RequestRedraw(); }
                return true;
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasStepper::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused() || navigation == StepperNavigation::Display) return false;
        switch (event.virtualKey) {
            case UCKeys::Left:
            case UCKeys::Up:
                return PrevStep(true);
            case UCKeys::Right:
            case UCKeys::Down:
                return NextStep(true);
            case UCKeys::Home:
                SetCurrentStep(0, true);
                return true;
            case UCKeys::End:
                SetCurrentStep(GetStepCount() - 1, true);
                return true;
            default:
                break;
        }
        return false;
    }

} // namespace UltraCanvas
