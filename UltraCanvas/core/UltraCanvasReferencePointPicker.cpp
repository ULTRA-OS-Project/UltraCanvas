// core/UltraCanvasReferencePointPicker.cpp
// The 3 x 3 reference point picker.
// Version: 1.0.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasReferencePointPicker.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    Point2Dd ReferencePointFraction(ReferencePoint point) {
        const int index = static_cast<int>(point);
        return Point2Dd((index % 3) * 0.5, (index / 3) * 0.5);
    }

    Point2Dd ReferencePointIn(ReferencePoint point, const Rect2Dd& box) {
        const Point2Dd f = ReferencePointFraction(point);
        return Point2Dd(box.x + box.width * f.x, box.y + box.height * f.y);
    }

    const char* ReferencePointName(ReferencePoint point) {
        static const char* names[] = {
            "Top left", "Top centre", "Top right",
            "Middle left", "Centre", "Middle right",
            "Bottom left", "Bottom centre", "Bottom right"
        };
        return names[static_cast<int>(point)];
    }

    UltraCanvasReferencePointPicker::UltraCanvasReferencePointPicker(const std::string& identifier,
                                                                     float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Hand;
    }

    void UltraCanvasReferencePointPicker::SetPoint(ReferencePoint p, bool runCallback) {
        if (p == point) return;
        point = p;
        if (runCallback && onPointChanged) onPointChanged(point);
        NotifyAccessibility(AccessibilityEventType::ValueChanged);
        RequestRedraw();
    }

// ===================================================================
// GEOMETRY
// ===================================================================

    Rect2Df UltraCanvasReferencePointPicker::GridRect() const {
        const Rect2Df b = GetLocalBounds();
        const float side = std::min(b.width, b.height);
        return Rect2Df(b.x + (b.width - side) / 2.0f, b.y + (b.height - side) / 2.0f, side, side);
    }

    // The points sit on the frame's corners and edge midpoints, inset by
    // half a selected point so the largest one stays inside the frame.
    Point2Df UltraCanvasReferencePointPicker::PointCentre(int index) const {
        const Rect2Df g = GridRect();
        const float inset = style.selectedPointSize / 2.0f + style.frameWidth + 1.0f;
        const float span = std::max(0.0f, g.width - 2.0f * inset);
        return Point2Df(g.x + inset + span * 0.5f * static_cast<float>(index % 3),
                        g.y + inset + span * 0.5f * static_cast<float>(index / 3));
    }

    // The nearest point; any click in the element picks one.
    int UltraCanvasReferencePointPicker::PointAt(const Point2Df& local) const {
        int best = -1;
        float bestDistance = 0.0f;
        for (int i = 0; i < 9; ++i) {
            const Point2Df c = PointCentre(i);
            const float d = (c.x - local.x) * (c.x - local.x) + (c.y - local.y) * (c.y - local.y);
            if (best < 0 || d < bestDistance) { best = i; bestDistance = d; }
        }
        return best;
    }

// ===================================================================
// RENDERING
// ===================================================================

    void UltraCanvasReferencePointPicker::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        const Rect2Df g = GridRect();
        const bool disabled = IsDisabled();
        const Color frame = disabled ? style.disabledColor : (IsFocused() ? style.focusColor : style.frameColor);
        ctx->DrawFilledRectangle(Rect2Dd(g.x, g.y, g.width, g.height), style.backgroundColor,
                                 style.frameWidth, frame, style.cornerRadius);

        // Thin lines joining the points: the box the points stand for.
        const Point2Df tl = PointCentre(0), br = PointCentre(8);
        ctx->DrawFilledRectangle(Rect2Dd(tl.x, tl.y, br.x - tl.x, br.y - tl.y), Colors::Transparent,
                                 1.0f, disabled ? style.disabledColor : style.pointColor, 0.0f);

        const int selected = static_cast<int>(point);
        for (int i = 0; i < 9; ++i) {
            const Point2Df c = PointCentre(i);
            const bool isSelected = i == selected;
            const float size = isSelected ? style.selectedPointSize : style.pointSize;
            Color colour = style.pointColor;
            if (disabled) colour = style.disabledColor;
            else if (isSelected) colour = style.selectedColor;
            else if (i == hovered) colour = style.hoverColor;
            // Whole-pixel squares, so a point is as sharp as the frame.
            const double x = std::round(c.x - size / 2.0f), y = std::round(c.y - size / 2.0f);
            ctx->DrawFilledRectangle(Rect2Dd(x, y, std::round(size), std::round(size)), colour,
                                     isSelected ? 0.0f : 1.0f,
                                     isSelected ? Colors::Transparent : colour, 0.0f);
        }
    }

// ===================================================================
// EVENTS
// ===================================================================

    bool UltraCanvasReferencePointPicker::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseMove: {
                const int index = PointAt(Point2Df(static_cast<float>(event.pointer.x),
                                                   static_cast<float>(event.pointer.y)));
                if (index != hovered) { hovered = index; RequestRedraw(); }
                return true;
            }
            case UCEventType::MouseLeave:
                if (hovered >= 0) { hovered = -1; RequestRedraw(); }
                return true;
            case UCEventType::MouseDown: {
                SetFocus(true);
                const int index = PointAt(Point2Df(static_cast<float>(event.pointer.x),
                                                   static_cast<float>(event.pointer.y)));
                if (index >= 0) SetPoint(static_cast<ReferencePoint>(index), true);
                return true;
            }
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasReferencePointPicker::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return false;
        int col = static_cast<int>(point) % 3, row = static_cast<int>(point) / 3;
        switch (event.virtualKey) {
            case UCKeys::Left:  col = std::max(0, col - 1); break;
            case UCKeys::Right: col = std::min(2, col + 1); break;
            case UCKeys::Up:    row = std::max(0, row - 1); break;
            case UCKeys::Down:  row = std::min(2, row + 1); break;
            case UCKeys::Home:  col = 1; row = 1; break;
            default: return false;
        }
        SetPoint(static_cast<ReferencePoint>(row * 3 + col), true);
        return true;
    }

} // namespace UltraCanvas
