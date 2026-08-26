// include/UltraCanvasCurveEditor.h
// The interactive curve editing element ("Curves" in image editors): the grid
// the user drags control points in, drawn over an optional histogram of the
// image being edited.
//
// The curves themselves live in UltraCanvasToneCurve.h — this element edits a
// ToneCurveSet and hands it back through its callbacks; pixel work stays with
// the caller (PixelFX::Colour::MapLut applies the tables the model produces).
// The element is framework-wide, not viewer-specific: the media viewer's Curves
// dialog is one caller, a paint tool or a colour ramp editor is the next.
//
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasToneCurve.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== CURVE EDITOR APPEARANCE =====
struct CurveEditorStyle {
    Color backgroundColor  = Color(250, 250, 250, 255);
    Color borderColor      = Color(120, 120, 128, 255);
    Color gridColor        = Color(205, 205, 210, 255);
    Color diagonalColor    = Color(190, 60, 60, 200);   // the identity reference line
    Color curveColor       = Color(30, 30, 35, 255);
    Color pointColor       = Color(255, 255, 255, 255);
    Color pointBorderColor = Color(30, 30, 35, 255);
    Color selectedPointColor = Color(0, 120, 215, 255);
    Color histogramColor   = Color(150, 150, 158, 140);
    Color focusColor       = Color(0, 120, 215, 255);

    int   gridDivisions = 4;     // cells per axis
    float pointSize     = 7.0f;  // control point square, in pixels
    float grabRadius    = 10.0f; // how close the pointer must be to grab a point
    float curveWidth    = 1.6f;

    static CurveEditorStyle Default() { return CurveEditorStyle(); }
    static CurveEditorStyle Dark();
};

// ===== THE CURVE EDITING ELEMENT =====
// A square-ish plot of input (x, left to right) against output (y, bottom to
// top) holding the four curves of a ToneCurveSet; the active channel is the one
// being edited and drawn. Interaction follows the convention of every image
// editor's curves box:
//
//   left click on empty space  - add a point there and start dragging it
//   left drag on a point       - move it (endpoints slide along their edge)
//   right click / double click - remove the point under the pointer
//   Delete / Backspace         - remove the selected point
//   arrow keys                 - nudge the selected point by one 8-bit step
//                                (Shift = ten steps)
//
// An optional histogram of the image being edited is drawn behind the curve —
// SetHistogram() takes the raw bin counts and the element scales them.
class UltraCanvasCurveEditor : public UltraCanvasUIElement {
public:
    UltraCanvasCurveEditor(const std::string& identifier,
                           float x, float y, float w, float h);
    UltraCanvasCurveEditor(const std::string& identifier, float w, float h)
        : UltraCanvasCurveEditor(identifier, 0, 0, w, h) {}
    explicit UltraCanvasCurveEditor(const std::string& identifier)
        : UltraCanvasCurveEditor(identifier, 0, 0, 0, 0) {}

    // ===== CURVES =====
    const ToneCurveSet& GetCurves() const { return curves; }
    ToneCurveSet& GetCurves() { return curves; }
    void SetCurves(const ToneCurveSet& set);

    const UltraCanvasToneCurve& GetActiveCurve() const { return curves.Channel(activeChannel); }
    void SetActiveChannel(ToneCurveChannel c);
    ToneCurveChannel GetActiveChannel() const { return activeChannel; }

    void ResetActiveChannel();   // identity for the channel being edited
    void ResetAllChannels();     // identity for all four curves

    // ===== HISTOGRAM BACKDROP =====
    // `bins` is one count per 8-bit level (256 entries; other sizes are
    // resampled). Pass an empty vector to remove the histogram of that channel.
    void SetHistogram(ToneCurveChannel channel, const std::vector<uint32_t>& bins);
    void ClearHistograms();
    void SetShowHistogram(bool show) { showHistogram = show; RequestRedraw(); }
    bool GetShowHistogram() const { return showHistogram; }

    // ===== SELECTION =====
    // Index of the point the user last touched, or -1. The dialog uses it to
    // show the input/output readout of that point.
    int GetSelectedPointIndex() const { return selectedPoint; }
    // Selected point in 8-bit units; both are -1 when nothing is selected.
    void GetSelectedPointValues(int& inputLevel, int& outputLevel) const;

    // ===== STYLE =====
    const CurveEditorStyle& GetStyle() const { return style; }
    void SetStyle(const CurveEditorStyle& s) { style = s; RequestRedraw(); }

    // ===== CALLBACKS =====
    // Fired for every change while editing (live preview), including each step
    // of a drag.
    std::function<void(const ToneCurveSet&)> onCurveChanged;
    // Fired once when a drag or a point add/remove finishes — the moment to
    // commit an expensive re-render.
    std::function<void(const ToneCurveSet&)> onEditFinished;
    // Fired when the selected point changes (index, or -1 when cleared).
    std::function<void(int)> onSelectionChanged;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

private:
    // Plot area: the bounds minus the border inset the points are drawn in.
    Rect2Df PlotRect() const;
    // Curve space (0..1, y up) <-> element-local pixels.
    Point2Df CurveToPixel(float in, float out) const;
    void     PixelToCurve(float px, float py, float& in, float& out) const;

    void DrawGrid(IRenderContext* ctx, const Rect2Df& plot) const;
    void DrawHistogram(IRenderContext* ctx, const Rect2Df& plot) const;
    void DrawCurve(IRenderContext* ctx, const Rect2Df& plot) const;
    void DrawPoints(IRenderContext* ctx, const Rect2Df& plot) const;

    void SelectPoint(int index);
    void NotifyChanged();
    void NotifyEditFinished();

    ToneCurveSet curves;
    ToneCurveChannel activeChannel = ToneCurveChannel::RGB;
    CurveEditorStyle style;

    // Histogram bins per channel, indexed by ToneCurveChannel.
    std::array<std::vector<uint32_t>, 4> histograms;
    bool showHistogram = true;

    int  selectedPoint = -1;
    int  draggingPoint = -1;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasCurveEditor> CreateCurveEditor(
        const std::string& identifier, float x, float y, float w, float h) {
    return std::make_shared<UltraCanvasCurveEditor>(identifier, x, y, w, h);
}

} // namespace UltraCanvas
