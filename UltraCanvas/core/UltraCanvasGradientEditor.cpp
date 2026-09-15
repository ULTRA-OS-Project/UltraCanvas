// core/UltraCanvasGradientEditor.cpp
// The gradient ramp editor: see the header.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasGradientEditor.h"
#include "UltraCanvasApplication.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

UltraCanvasGradientEditor::UltraCanvasGradientEditor(const std::string& elemId, int x, int y, int width, int height)
    : UltraCanvasUIElement(elemId, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(width), static_cast<float>(height)) {
    stops = {{0.0, Color(0, 0, 0, 255)}, {1.0, Color(255, 255, 255, 255)}};
    SetMouseCursor(UCMouseCursor::Arrow);
}

// ===========================================================================
// STOPS
// ===========================================================================

void UltraCanvasGradientEditor::SortStops(int* trackIndex) {
    GradientStop tracked;
    const bool track = trackIndex && *trackIndex >= 0 && *trackIndex < static_cast<int>(stops.size());
    if (track) tracked = stops[*trackIndex];
    std::stable_sort(stops.begin(), stops.end(), [](const GradientStop& a, const GradientStop& b) {
        return a.position < b.position;
    });
    if (track) {
        for (size_t i = 0; i < stops.size(); ++i)
            if (stops[i].position == tracked.position && stops[i].color.ToARGB() == tracked.color.ToARGB()) {
                *trackIndex = static_cast<int>(i);
                break;
            }
    }
}

void UltraCanvasGradientEditor::Changed() {
    if (onStopsChanged) onStopsChanged();
    RequestRedraw();
}

void UltraCanvasGradientEditor::SetStops(const std::vector<GradientStop>& list) {
    stops = list;
    for (auto& s : stops) s.position = std::clamp(s.position, 0.0, 1.0);
    if (stops.empty()) stops = {{0.0, Color(0, 0, 0, 255)}, {1.0, Color(255, 255, 255, 255)}};
    if (stops.size() == 1) stops.push_back({1.0, stops[0].color});
    SortStops(nullptr);
    selected = std::min(selected, static_cast<int>(stops.size()) - 1);
    RequestRedraw();
}

Color UltraCanvasGradientEditor::ColorAt(double t) const {
    if (stops.empty()) return Colors::Black;
    if (t <= stops.front().position) return stops.front().color;
    if (t >= stops.back().position) return stops.back().color;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].position) {
            const GradientStop& a = stops[i - 1];
            const GradientStop& b = stops[i];
            const double span = b.position - a.position;
            const double k = span > 0 ? (t - a.position) / span : 0.0;
            auto mix = [k](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * k)); };
            return Color(mix(a.color.r, b.color.r), mix(a.color.g, b.color.g), mix(a.color.b, b.color.b), mix(a.color.a, b.color.a));
        }
    }
    return stops.back().color;
}

int UltraCanvasGradientEditor::AddStop(double position, const Color& color) {
    position = std::clamp(position, 0.0, 1.0);
    stops.push_back({position, color});
    int index = static_cast<int>(stops.size()) - 1;
    SortStops(&index);
    selected = index;
    if (onSelectionChanged) onSelectionChanged(selected);
    Changed();
    return index;
}

int UltraCanvasGradientEditor::AddStopAt(double position) {
    return AddStop(position, ColorAt(std::clamp(position, 0.0, 1.0)));
}

bool UltraCanvasGradientEditor::RemoveStop(int index) {
    if (index < 0 || index >= static_cast<int>(stops.size())) return false;
    if (static_cast<int>(stops.size()) <= minimumStops) return false;
    stops.erase(stops.begin() + index);
    if (selected >= static_cast<int>(stops.size())) selected = static_cast<int>(stops.size()) - 1;
    if (onSelectionChanged) onSelectionChanged(selected);
    Changed();
    return true;
}

void UltraCanvasGradientEditor::SetStopColor(int index, const Color& color) {
    if (index < 0 || index >= static_cast<int>(stops.size())) return;
    stops[index].color = color;
    Changed();
}

void UltraCanvasGradientEditor::SetStopPosition(int index, double position) {
    if (index < 0 || index >= static_cast<int>(stops.size())) return;
    stops[index].position = std::clamp(position, 0.0, 1.0);
    int track = index;
    SortStops(&track);
    if (selected == index) selected = track;
    Changed();
}

void UltraCanvasGradientEditor::Reverse() {
    for (auto& s : stops) s.position = 1.0 - s.position;
    int track = selected;
    SortStops(&track);
    selected = track;
    Changed();
}

// ===========================================================================
// SELECTION
// ===========================================================================

void UltraCanvasGradientEditor::SelectStop(int index) {
    if (index < -1 || index >= static_cast<int>(stops.size())) index = -1;
    if (selected == index) return;
    selected = index;
    if (onSelectionChanged) onSelectionChanged(selected);
    RequestRedraw();
}

Color UltraCanvasGradientEditor::GetSelectedColor() const {
    if (selected < 0 || selected >= static_cast<int>(stops.size())) return Colors::Black;
    return stops[selected].color;
}

// ===========================================================================
// GEOMETRY
// ===========================================================================

Rect2Dd UltraCanvasGradientEditor::StripRect() const {
    const Rect2Df b = GetLocalBounds();
    const double h = std::max(8.0, b.height - markerSize - 6.0);
    return Rect2Dd(stripInset, 2.0, std::max(1.0, b.width - 2 * stripInset), h);
}

double UltraCanvasGradientEditor::PositionAtX(double x) const {
    const Rect2Dd s = StripRect();
    return std::clamp((x - s.x) / s.width, 0.0, 1.0);
}

double UltraCanvasGradientEditor::XAtPosition(double position) const {
    const Rect2Dd s = StripRect();
    return s.x + position * s.width;
}

int UltraCanvasGradientEditor::StopAt(const Point2Di& p) const {
    const Rect2Dd s = StripRect();
    const double markerTop = s.y + s.height;
    int best = -1;
    double bestD = markerSize;
    for (size_t i = 0; i < stops.size(); ++i) {
        const double x = XAtPosition(stops[i].position);
        const double d = std::fabs(p.x - x);
        const bool inMarker = p.y >= markerTop - 2 && p.y <= markerTop + markerSize + 2;
        if (inMarker && d <= bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}

// ===========================================================================
// RENDER
// ===========================================================================

void UltraCanvasGradientEditor::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!IsVisible()) return;
    const Rect2Dd s = StripRect();
    ctx->PushState();
    // Checkerboard behind the ramp so alpha shows.
    if (showChecker) {
        const double cell = 6.0;
        ctx->PushState();
        ctx->ClipRect(s);
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(s);
        ctx->SetFillPaint(Color(204, 204, 204, 255));
        int row = 0;
        for (double y = s.y; y < s.y + s.height; y += cell, ++row)
            for (double x = s.x + ((row % 2) ? cell : 0); x < s.x + s.width; x += 2 * cell)
                ctx->FillRectangle(Rect2Dd(x, y, cell, cell));
        ctx->PopState();
    }
    auto ramp = ctx->CreateLinearGradientPattern(s.x, s.y, s.x + s.width, s.y, stops);
    if (ramp) {
        ctx->SetFillPaint(ramp);
        ctx->FillRectangle(s);
    }
    ctx->SetStrokePaint(Color(120, 120, 125, 255));
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    ctx->DrawRectangle(Rect2Dd(std::round(s.x) + 0.5, std::round(s.y) + 0.5, std::round(s.width), std::round(s.height)));

    // Stop markers: a small house shape pointing at the ramp, filled with
    // the stop's colour, the selected one outlined in blue.
    const double top = s.y + s.height;
    for (size_t i = 0; i < stops.size(); ++i) {
        const double x = XAtPosition(stops[i].position);
        const double w = markerSize;
        ctx->ClearPath();
        ctx->MoveTo(x, top + 1);
        ctx->LineTo(x + w / 2, top + 1 + w * 0.45);
        ctx->LineTo(x + w / 2, top + 1 + w);
        ctx->LineTo(x - w / 2, top + 1 + w);
        ctx->LineTo(x - w / 2, top + 1 + w * 0.45);
        ctx->ClosePath();
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillPathPreserve();
        Color c = stops[i].color; c.a = 255;
        ctx->SetFillPaint(c);
        ctx->FillPathPreserve();
        ctx->SetStrokePaint(static_cast<int>(i) == selected ? Color(30, 100, 220, 255) : Color(70, 70, 75, 255));
        ctx->SetStrokeWidth(static_cast<int>(i) == selected ? 2.0 : 1.0);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
    }
    ctx->PopState();
}

// ===========================================================================
// EVENTS
// ===========================================================================

bool UltraCanvasGradientEditor::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown: {
            if (!Contains(event.pointer) || event.button != UCMouseButton::Left) return false;
            SetFocus(true);
            const int hit = StopAt(event.pointer);
            if (hit >= 0) {
                SelectStop(hit);
                dragging = true;
                dragIndex = hit;
                dragRemovePending = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
                return true;
            }
            const Rect2Dd s = StripRect();
            if (event.pointer.y >= s.y && event.pointer.y <= s.y + s.height) {
                SelectStop(-1);
                return true;
            }
            return false;
        }
        case UCEventType::MouseDoubleClick: {
            if (!Contains(event.pointer)) return false;
            if (StopAt(event.pointer) >= 0) return true;
            const Rect2Dd s = StripRect();
            if (event.pointer.y >= s.y - 2 && event.pointer.y <= s.y + s.height + markerSize) {
                AddStopAt(PositionAtX(event.pointer.x));
                return true;
            }
            return false;
        }
        case UCEventType::MouseMove: {
            if (!dragging || dragIndex < 0) return false;
            const Rect2Dd s = StripRect();
            // Pulled well below the markers: the stop will be removed on release.
            const bool away = event.pointer.y > s.y + s.height + markerSize * 2.5 || event.pointer.y < s.y - markerSize * 2;
            const bool removable = static_cast<int>(stops.size()) > minimumStops &&
                                   dragIndex != 0 && dragIndex != static_cast<int>(stops.size()) - 1;
            dragRemovePending = away && removable;
            if (!away) {
                stops[dragIndex].position = PositionAtX(event.pointer.x);
                int track = dragIndex;
                SortStops(&track);
                dragIndex = track;
                selected = track;
                if (onStopsChanging) onStopsChanging();
            }
            RequestRedraw();
            return true;
        }
        case UCEventType::MouseUp: {
            if (!dragging) return false;
            dragging = false;
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            if (dragRemovePending) RemoveStop(dragIndex);
            else Changed();
            dragIndex = -1;
            dragRemovePending = false;
            return true;
        }
        case UCEventType::KeyDown: {
            if (selected < 0) return false;
            if (event.virtualKey == UCKeys::Delete) { RemoveStop(selected); return true; }
            if (event.virtualKey == UCKeys::Left || event.virtualKey == UCKeys::Right) {
                const double step = event.shift ? 0.1 : 0.01;
                SetStopPosition(selected, stops[selected].position + (event.virtualKey == UCKeys::Left ? -step : step));
                return true;
            }
            return false;
        }
        default:
            break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

} // namespace UltraCanvas
