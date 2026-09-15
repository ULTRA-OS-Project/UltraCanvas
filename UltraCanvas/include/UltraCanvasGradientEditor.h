// include/UltraCanvasGradientEditor.h
// A gradient ramp editor: the stops of a gradient on a horizontal strip.
// Click a stop to select it, drag it to move it, double-click the strip to
// add a stop where the ramp's colour is, drag a stop off the strip (or
// press Delete) to remove it. The selected stop's colour and position are
// set through the API - the host binds them to an UltraCanvasColorPicker
// and a spinner - and every change fires onStopsChanged. The stops are
// the render context's GradientStop, so the result feeds
// CreateLinearGradientPattern & co. and VectorStorage's gradient data
// directly.
//
// The tone curve editor is the precedent for "drag control points on a
// strip"; this is its colour-ramp sibling.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"

#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {

class UltraCanvasGradientEditor : public UltraCanvasUIElement {
public:
    explicit UltraCanvasGradientEditor(const std::string& elemId = "GradientEditor",
                                       int x = 0, int y = 0, int width = 240, int height = 44);

    // ===== STOPS =====
    // Stops are kept sorted by position; the first and last may not be
    // removed (a gradient needs two).
    void SetStops(const std::vector<GradientStop>& stops);
    const std::vector<GradientStop>& GetStops() const { return stops; }
    int AddStop(double position, const Color& color);        // returns the new index
    // Adds a stop at `position` with the ramp's colour there.
    int AddStopAt(double position);
    bool RemoveStop(int index);
    void SetStopColor(int index, const Color& color);
    void SetStopPosition(int index, double position);       // re-sorts; the index may change
    Color ColorAt(double position) const;                    // the ramp's colour, interpolated

    // ===== SELECTION =====
    int GetSelectedStop() const { return selected; }
    void SelectStop(int index);
    Color GetSelectedColor() const;

    // ===== OPTIONS =====
    void SetShowAlphaChecker(bool show) { showChecker = show; RequestRedraw(); }
    void SetMinimumStops(int n) { minimumStops = n < 2 ? 2 : n; }
    // Flips the ramp end for end.
    void Reverse();

    // ===== CALLBACKS =====
    std::function<void()> onStopsChanged;                    // any edit (position, colour, add, remove)
    std::function<void(int)> onSelectionChanged;             // selected stop index, -1 for none
    // Fires while a stop is being dragged (for a live preview); onStopsChanged
    // fires once on release as well.
    std::function<void()> onStopsChanging;

    // ===== ELEMENT =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

private:
    Rect2Dd StripRect() const;                 // the ramp, element-local
    double PositionAtX(double x) const;
    double XAtPosition(double position) const;
    int StopAt(const Point2Di& p) const;       // -1 when none
    void SortStops(int* trackIndex);
    void Changed();

    std::vector<GradientStop> stops;
    int selected = -1;
    int minimumStops = 2;
    bool showChecker = true;
    // interaction
    bool dragging = false;
    int dragIndex = -1;
    bool dragRemovePending = false;
    double markerSize = 12.0;
    double stripInset = 8.0;
};

inline std::shared_ptr<UltraCanvasGradientEditor> CreateGradientEditor(const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasGradientEditor>(id, x, y, w, h);
}

} // namespace UltraCanvas
