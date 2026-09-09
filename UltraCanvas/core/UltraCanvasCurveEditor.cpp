// core/UltraCanvasCurveEditor.cpp
// The interactive curve editor element. The curve model it edits lives in
// core/UltraCanvasToneCurve.cpp.
// See include/UltraCanvasCurveEditor.h for the API and the interaction rules.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasCurveEditor.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {
    inline float Clamp01(float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
}

// ===========================================================================
// STYLE
// ===========================================================================

CurveEditorStyle CurveEditorStyle::Dark() {
    CurveEditorStyle s;
    s.backgroundColor    = Color(30, 30, 36, 255);
    s.borderColor        = Color(80, 80, 90, 255);
    s.gridColor          = Color(58, 58, 68, 255);
    s.diagonalColor      = Color(190, 90, 90, 200);
    s.curveColor         = Color(235, 235, 240, 255);
    s.pointColor         = Color(245, 245, 250, 255);
    s.pointBorderColor   = Color(20, 20, 24, 255);
    s.selectedPointColor = Color(64, 160, 255, 255);
    s.histogramColor     = Color(110, 110, 122, 150);
    return s;
}

// ===========================================================================
// CURVE EDITOR ELEMENT
// ===========================================================================

UltraCanvasCurveEditor::UltraCanvasCurveEditor(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {
    SetMouseCursor(UCMouseCursor::Cross);
}

void UltraCanvasCurveEditor::SetCurves(const ToneCurveSet& set) {
    curves = set;
    selectedPoint = -1;
    draggingPoint = -1;
    RequestRedraw();
}

void UltraCanvasCurveEditor::SetActiveChannel(ToneCurveChannel c) {
    if (activeChannel == c) return;
    activeChannel = c;
    SelectPoint(-1);
    draggingPoint = -1;
    RequestRedraw();
}

void UltraCanvasCurveEditor::ResetActiveChannel() {
    curves.Channel(activeChannel).Reset();
    SelectPoint(-1);
    NotifyChanged();
    NotifyEditFinished();
}

void UltraCanvasCurveEditor::ResetAllChannels() {
    curves.Reset();
    SelectPoint(-1);
    NotifyChanged();
    NotifyEditFinished();
}

void UltraCanvasCurveEditor::SetHistogram(ToneCurveChannel channel,
                                          const std::vector<uint32_t>& bins) {
    auto& dst = histograms[static_cast<int>(channel)];
    if (bins.empty()) {
        dst.clear();
    } else if (bins.size() == 256) {
        dst = bins;
    } else {
        // Resample whatever resolution the caller has into the 256 levels drawn.
        dst.assign(256, 0);
        for (size_t i = 0; i < bins.size(); ++i) {
            size_t target = i * 256 / bins.size();
            if (target > 255) target = 255;
            dst[target] += bins[i];
        }
    }
    RequestRedraw();
}

void UltraCanvasCurveEditor::ClearHistograms() {
    for (auto& h : histograms) h.clear();
    RequestRedraw();
}

void UltraCanvasCurveEditor::GetSelectedPointValues(int& inputLevel, int& outputLevel) const {
    const auto& pts = GetActiveCurve().GetPoints();
    if (selectedPoint < 0 || selectedPoint >= static_cast<int>(pts.size())) {
        inputLevel = outputLevel = -1;
        return;
    }
    inputLevel  = static_cast<int>(std::lround(pts[selectedPoint].input * 255.0f));
    outputLevel = static_cast<int>(std::lround(pts[selectedPoint].output * 255.0f));
}

// ----- geometry -----

Rect2Df UltraCanvasCurveEditor::PlotRect() const {
    Rect2Df b = GetLocalBounds();
    // Half a control point plus the border, so points on the edges stay whole.
    float inset = style.pointSize * 0.5f + 2.0f;
    float w = std::max(1.0f, b.width  - 2.0f * inset);
    float h = std::max(1.0f, b.height - 2.0f * inset);
    return Rect2Df(b.x + inset, b.y + inset, w, h);
}

Point2Df UltraCanvasCurveEditor::CurveToPixel(float in, float out) const {
    Rect2Df p = PlotRect();
    return Point2Df(p.x + in * p.width,
                    p.y + (1.0f - out) * p.height);   // output grows upwards
}

void UltraCanvasCurveEditor::PixelToCurve(float px, float py, float& in, float& out) const {
    Rect2Df p = PlotRect();
    in  = Clamp01(p.width  > 0.0f ? (px - p.x) / p.width  : 0.0f);
    out = Clamp01(p.height > 0.0f ? 1.0f - (py - p.y) / p.height : 0.0f);
}

// ----- rendering -----

void UltraCanvasCurveEditor::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    Rect2Df bounds = GetLocalBounds();
    if (bounds.width <= 0 || bounds.height <= 0) return;

    ctx->PushState();

    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(bounds);

    Rect2Df plot = PlotRect();
    if (showHistogram) DrawHistogram(ctx, plot);
    DrawGrid(ctx, plot);
    DrawCurve(ctx, plot);
    DrawPoints(ctx, plot);

    ctx->SetStrokePaint(IsFocused() ? style.focusColor : style.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRectangle(bounds);

    ctx->PopState();
}

void UltraCanvasCurveEditor::DrawGrid(IRenderContext* ctx, const Rect2Df& plot) const {
    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(1.0);
    int div = std::max(1, style.gridDivisions);
    for (int i = 0; i <= div; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(div);
        float x = plot.x + t * plot.width;
        float y = plot.y + t * plot.height;
        ctx->DrawLine(Point2Dd(x, plot.y), Point2Dd(x, plot.y + plot.height));
        ctx->DrawLine(Point2Dd(plot.x, y), Point2Dd(plot.x + plot.width, y));
    }

    // The identity diagonal: the reference the edited curve departs from.
    ctx->SetStrokePaint(style.diagonalColor);
    UCDashPattern dash;
    dash.dashes = { 3.0, 3.0 };
    ctx->SetLineDash(dash);
    ctx->DrawLine(Point2Dd(plot.x, plot.y + plot.height),
                  Point2Dd(plot.x + plot.width, plot.y));
    ctx->SetLineDash(UCDashPattern());
}

void UltraCanvasCurveEditor::DrawHistogram(IRenderContext* ctx, const Rect2Df& plot) const {
    const auto& bins = histograms[static_cast<int>(activeChannel)];
    if (bins.size() != 256) return;

    uint32_t peak = 0;
    for (uint32_t v : bins) peak = std::max(peak, v);
    if (peak == 0) return;

    // A square-root scale keeps a dominant background tone from flattening the
    // rest of the distribution into an invisible line.
    double peakScale = std::sqrt(static_cast<double>(peak));
    ctx->SetFillPaint(style.histogramColor);
    float barW = plot.width / 256.0f;
    for (int i = 0; i < 256; ++i) {
        if (bins[i] == 0) continue;
        double h = std::sqrt(static_cast<double>(bins[i])) / peakScale * plot.height;
        if (h < 1.0) h = 1.0;
        ctx->FillRectangle(Rect2Dd(plot.x + i * barW,
                                   plot.y + plot.height - h,
                                   std::max(1.0f, barW), h));
    }
}

void UltraCanvasCurveEditor::DrawCurve(IRenderContext* ctx, const Rect2Df& plot) const {
    const UltraCanvasToneCurve& curve = GetActiveCurve();

    // One sample per horizontal pixel keeps the spline smooth at any size.
    int samples = std::max(2, static_cast<int>(plot.width));
    std::vector<Point2Dd> path;
    path.reserve(samples + 1);
    for (int i = 0; i <= samples; ++i) {
        float x = static_cast<float>(i) / static_cast<float>(samples);
        Point2Df p = CurveToPixel(x, curve.Evaluate(x));
        path.push_back(Point2Dd(p.x, p.y));
    }

    Color line = style.curveColor;
    switch (activeChannel) {
        case ToneCurveChannel::Red:   line = Color(210, 60, 60, 255);  break;
        case ToneCurveChannel::Green: line = Color(50, 160, 70, 255);  break;
        case ToneCurveChannel::Blue:  line = Color(60, 110, 220, 255); break;
        default: break;
    }
    ctx->SetStrokePaint(line);
    ctx->SetStrokeWidth(style.curveWidth);
    ctx->DrawLinePath(path, false);
}

void UltraCanvasCurveEditor::DrawPoints(IRenderContext* ctx, const Rect2Df& plot) const {
    const auto& pts = GetActiveCurve().GetPoints();
    float s = style.pointSize;
    for (size_t i = 0; i < pts.size(); ++i) {
        Point2Df c = CurveToPixel(pts[i].input, pts[i].output);
        Rect2Dd r(c.x - s * 0.5, c.y - s * 0.5, s, s);
        bool selected = (static_cast<int>(i) == selectedPoint);
        ctx->SetFillPaint(selected ? style.selectedPointColor : style.pointColor);
        ctx->FillRectangle(r);
        ctx->SetStrokePaint(style.pointBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(r);
    }
}

// ----- interaction -----

void UltraCanvasCurveEditor::SelectPoint(int index) {
    if (selectedPoint == index) return;
    selectedPoint = index;
    if (onSelectionChanged) onSelectionChanged(selectedPoint);
    RequestRedraw();
}

void UltraCanvasCurveEditor::NotifyChanged() {
    RequestRedraw();
    if (onCurveChanged) onCurveChanged(curves);
}

void UltraCanvasCurveEditor::NotifyEditFinished() {
    if (onEditFinished) onEditFinished(curves);
}

bool UltraCanvasCurveEditor::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    UltraCanvasToneCurve& curve = curves.Channel(activeChannel);
    Rect2Df plot = PlotRect();
    // Grab radius is given in pixels; the search runs in curve space.
    float radius = plot.width > 0.0f ? style.grabRadius / plot.width : 0.05f;

    switch (event.type) {
        case UCEventType::MouseDown: {
            Point2Df pos(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
            if (!Contains(pos)) return false;
            SetFocus(true);

            float in = 0.0f, out = 0.0f;
            PixelToCurve(pos.x, pos.y, in, out);
            int hit = curve.FindPointNear(in, out, radius);

            if (event.button == UCMouseButton::Right) {
                if (hit >= 0 && curve.RemovePoint(hit)) {
                    SelectPoint(-1);
                    NotifyChanged();
                    NotifyEditFinished();
                }
                return true;
            }
            if (event.button != UCMouseButton::Left) return false;

            if (hit < 0) hit = curve.AddPoint(in, out);   // click on empty space adds one
            if (hit < 0) return true;                     // curve full / no room

            draggingPoint = hit;
            SelectPoint(hit);
            draggingPoint = curve.MovePoint(draggingPoint, in, out);
            SelectPoint(draggingPoint);
            NotifyChanged();
            if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
            return true;
        }

        case UCEventType::MouseMove: {
            if (draggingPoint < 0) return false;
            float in = 0.0f, out = 0.0f;
            PixelToCurve(static_cast<float>(event.pointer.x),
                         static_cast<float>(event.pointer.y), in, out);
            draggingPoint = curve.MovePoint(draggingPoint, in, out);
            SelectPoint(draggingPoint);
            NotifyChanged();
            return true;
        }

        case UCEventType::MouseUp: {
            if (draggingPoint < 0) return false;
            draggingPoint = -1;
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            NotifyEditFinished();
            return true;
        }

        case UCEventType::MouseDoubleClick: {
            Point2Df pos(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
            if (!Contains(pos)) return false;
            float in = 0.0f, out = 0.0f;
            PixelToCurve(pos.x, pos.y, in, out);
            int hit = curve.FindPointNear(in, out, radius);
            if (hit >= 0 && curve.RemovePoint(hit)) {
                draggingPoint = -1;
                SelectPoint(-1);
                NotifyChanged();
                NotifyEditFinished();
            }
            return true;
        }

        case UCEventType::MouseLeave:
            SetHovered(false);
            return false;

        case UCEventType::KeyDown: {
            if (!IsFocused()) return false;
            const auto& pts = curve.GetPoints();
            if (selectedPoint < 0 || selectedPoint >= static_cast<int>(pts.size())) return false;

            if (event.virtualKey == UCKeys::Delete || event.virtualKey == UCKeys::Backspace) {
                if (curve.RemovePoint(selectedPoint)) {
                    SelectPoint(-1);
                    NotifyChanged();
                    NotifyEditFinished();
                }
                return true;
            }

            float stepValue = (event.shift ? 10.0f : 1.0f) / 255.0f;
            float in  = pts[selectedPoint].input;
            float out = pts[selectedPoint].output;
            switch (event.virtualKey) {
                case UCKeys::Left:  in  -= stepValue; break;
                case UCKeys::Right: in  += stepValue; break;
                case UCKeys::Up:    out += stepValue; break;
                case UCKeys::Down:  out -= stepValue; break;
                default: return false;
            }
            int moved = curve.MovePoint(selectedPoint, in, out);
            SelectPoint(moved);
            NotifyChanged();
            NotifyEditFinished();
            return true;
        }

        default:
            break;
    }
    return false;
}

} // namespace UltraCanvas
