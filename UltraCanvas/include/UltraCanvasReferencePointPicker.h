// include/UltraCanvasReferencePointPicker.h
// Reference point picker: a 3 x 3 grid of points standing for the corners,
// edge midpoints and centre of a box. A drawing program's transform bar uses
// it to say which point of the selection stays put when it is moved, scaled,
// rotated or sheared, and which point the X / Y fields show.
//
// Click a point, or move between them with the arrow keys; Home picks the
// centre.
//
// Version: 1.0.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// The nine points, row by row from the top left.
    enum class ReferencePoint {
        TopLeft, TopCentre, TopRight,
        MiddleLeft, Centre, MiddleRight,
        BottomLeft, BottomCentre, BottomRight
    };

    // Where the point lies across a box, as fractions of its width and
    // height: 0, 0.5 or 1 (TopLeft is 0, 0; BottomRight 1, 1).
    Point2Dd ReferencePointFraction(ReferencePoint point);
    // The point itself in `box`.
    Point2Dd ReferencePointIn(ReferencePoint point, const Rect2Dd& box);
    // "Top left", "Centre", ...
    const char* ReferencePointName(ReferencePoint point);

    struct ReferencePointPickerStyle {
        Color frameColor        = Color(150, 150, 156);
        Color backgroundColor   = Color(255, 255, 255);
        Color pointColor        = Color(150, 150, 156);   // the eight points not picked
        Color hoverColor        = Color(90, 90, 96);
        Color selectedColor     = Color(0, 120, 215);
        Color focusColor        = Color(0, 120, 215);
        Color disabledColor     = Color(200, 200, 204);
        float frameWidth        = 1.0f;
        float cornerRadius      = 2.0f;
        float pointSize         = 5.0f;    // side of a point's square
        float selectedPointSize = 7.0f;
    };

    class UltraCanvasReferencePointPicker : public UltraCanvasUIElement {
    private:
        ReferencePoint point = ReferencePoint::Centre;
        int hovered = -1;   // index of the point under the pointer, -1 none
        ReferencePointPickerStyle style;

    public:
        UltraCanvasReferencePointPicker(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasReferencePointPicker(const std::string& identifier, float w, float h)
            : UltraCanvasReferencePointPicker(identifier, -1, -1, w, h) {}
        explicit UltraCanvasReferencePointPicker(const std::string& identifier)
            : UltraCanvasReferencePointPicker(identifier, -1, -1, -1, -1) {}

        // Picks `p`; `runCallback` fires onPointChanged when it changes.
        void SetPoint(ReferencePoint p, bool runCallback = false);
        ReferencePoint GetPoint() const { return point; }

        ReferencePointPickerStyle& GetStyle() { return style; }
        const ReferencePointPickerStyle& GetStyle() const { return style; }
        void SetStyle(const ReferencePointPickerStyle& s) { style = s; RequestRedraw(); }

        // ===== ACCESSIBILITY =====
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Group; }
        std::string GetAccessibleValueText() const override { return ReferencePointName(point); }

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // The point the user picked.
        std::function<void(ReferencePoint)> onPointChanged;

    private:
        Rect2Df GridRect() const;             // element-local square holding the points
        Point2Df PointCentre(int index) const;
        int PointAt(const Point2Df& local) const;
        bool HandleKeyDown(const UCEvent& event);
    };

    inline std::shared_ptr<UltraCanvasReferencePointPicker> CreateReferencePointPicker(
            const std::string& identifier, float x, float y, float w, float h,
            ReferencePoint point = ReferencePoint::Centre) {
        auto picker = std::make_shared<UltraCanvasReferencePointPicker>(identifier, x, y, w, h);
        picker->SetPoint(point);
        return picker;
    }

} // namespace UltraCanvas
