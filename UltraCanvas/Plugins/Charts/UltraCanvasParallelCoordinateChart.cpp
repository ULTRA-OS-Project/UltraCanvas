// Plugins/Charts/UltraCanvasParallelCoordinateChart.cpp
// Parallel coordinate chart element - first native client of the chart engine
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasParallelCoordinateChart.h"
#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

namespace {

// Fallback categorical palette for groups without an explicit colour.
const Color kGroupPalette[] = {
    Color(68, 119, 170, 255),  Color(238, 102, 119, 255), Color(34, 136, 51, 255),
    Color(204, 187, 68, 255),  Color(102, 204, 238, 255), Color(170, 51, 119, 255),
    Color(187, 187, 187, 255), Color(153, 68, 85, 255),
};
constexpr size_t kGroupPaletteSize = sizeof(kGroupPalette) / sizeof(kGroupPalette[0]);

Color WithAlpha(Color c, uint8_t alpha) {
    c.a = alpha;
    return c;
}

// WCAG relative luminance, 0 (black) .. 1 (white).
double RelativeLuminance(const Color& c) {
    auto linear = [](double u) {
        u /= 255.0;
        return (u <= 0.03928) ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

// A highlighted line must contrast with the ordinary lines of the same colour
// around it, whatever that colour is: the halo comes from the opposite end of
// the luminance scale, so a pale line gets a dark casing and vice versa.
Color HighlightHaloColor(const Color& base) {
    return (RelativeLuminance(base) > 0.4) ? Color(25, 25, 30, 235)
                                           : Color(255, 255, 255, 235);
}

} // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

UltraCanvasParallelCoordinateChartElement::UltraCanvasParallelCoordinateChartElement(
        const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    // Axis header chips sit inside the plot band, so in-plot labels must
    // steer around them (chart engine proposal §5.8).
    SetLabelPolicy(ChartLabelPolicy::ParallelCoordinates());

    Properties().Define("lineAlpha", 0.0);
    Properties().Define("lineWidth", 1.5);
    Properties().Define("curveTension", 0.5);
    Properties().Define("lineMode", std::string("straight"));
    Properties().Define("showEndpointLabels", true);
}

// =============================================================================
// DATA AND CONFIGURATION
// =============================================================================

void UltraCanvasParallelCoordinateChartElement::AddDimension(
        const std::string& name, const std::vector<double>& column) {
    model.AddDimensionColumn(name, column);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasParallelCoordinateChartElement::SetRecords(std::vector<PCPRecord> records) {
    model.SetRecords(std::move(records));
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasParallelCoordinateChartElement::SetRecordGroups(
        const std::vector<std::string>& groups) {
    model.SetRecordGroups(groups);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasParallelCoordinateChartElement::ClearData() {
    model.Clear();
    pinnedRecords.clear();
    hoveredRecord = -1;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasParallelCoordinateChartElement::SetNormalizationMode(PCPNormalization mode) {
    model.SetNormalization(mode);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasParallelCoordinateChartElement::SetStandardize(PCPStandardize mode) {
    model.SetStandardize(mode);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasParallelCoordinateChartElement::SetAxisInverted(
        const std::string& dimension, bool inverted) {
    for (size_t d = 0; d < model.DimensionCount(); ++d) {
        if (model.DimensionName(d) == dimension) {
            model.SetAxisInverted(d, inverted);
            MarkEngineDirty(ChartDirty::Data);
            return;
        }
    }
}

void UltraCanvasParallelCoordinateChartElement::SetLineMode(PCPLineMode mode) {
    lineMode = mode;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetCurveTension(float tension) {
    curveTension = std::clamp(tension, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetLineWidth(float width) {
    lineWidth = std::max(0.1f, width);
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetLineAlpha(float alpha) {
    lineAlpha = std::clamp(alpha, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetShowVertexMarkers(bool show, float radius) {
    showVertexMarkers = show;
    vertexRadius = radius;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetContextStyle(const Color& color) {
    contextColor = color;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetShowContext(bool show) {
    showContext = show;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetColorMode(PCPColorMode mode) {
    colorMode = mode;
    RebuildLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasParallelCoordinateChartElement::SetSingleColor(const Color& color) {
    singleColor = color;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetGroupColors(
        const std::map<std::string, Color>& colors) {
    groupColors = colors;
    RebuildLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasParallelCoordinateChartElement::SetColorDimension(const std::string& dimension) {
    colorDimensionName = dimension;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetColormap(HeatmapColormap map, bool reverse) {
    colormap = map;
    colormapReversed = reverse;
    RequestRedraw();
}

void UltraCanvasParallelCoordinateChartElement::SetShowAxisEndpointLabels(bool show) {
    endpointLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasParallelCoordinateChartElement::AddBrush(
        const std::string& dimension, double low, double high) {
    for (size_t d = 0; d < model.DimensionCount(); ++d) {
        if (model.DimensionName(d) == dimension) {
            model.AddBrush(d, low, high);
            if (onBrushChanged) onBrushChanged(model.PassingCount());
            MarkEngineDirty(ChartDirty::Selection);
            return;
        }
    }
}

void UltraCanvasParallelCoordinateChartElement::ClearBrushes() {
    model.ClearBrushes();
    if (onBrushChanged) onBrushChanged(model.PassingCount());
    MarkEngineDirty(ChartDirty::Selection);
}

void UltraCanvasParallelCoordinateChartElement::PinRecord(size_t recordIndex, bool pinned) {
    if (pinned) pinnedRecords.insert(recordIndex);
    else pinnedRecords.erase(recordIndex);
    MarkEngineDirty(ChartDirty::Selection);
}

// =============================================================================
// COLOURS AND LEGEND
// =============================================================================

uint8_t UltraCanvasParallelCoordinateChartElement::EffectiveAlpha() const {
    if (lineAlpha > 0.0f) return static_cast<uint8_t>(lineAlpha * 255.0f);
    // Automatic: fade with record count so dense plots stay readable.
    const size_t n = std::max<size_t>(1, model.RecordCount());
    const double alpha = std::clamp(2000.0 / static_cast<double>(n), 18.0, 255.0);
    return static_cast<uint8_t>(alpha);
}

Color UltraCanvasParallelCoordinateChartElement::FallbackGroupColor(size_t groupIndex) const {
    return kGroupPalette[groupIndex % kGroupPaletteSize];
}

Color UltraCanvasParallelCoordinateChartElement::RecordColor(size_t record) const {
    const PCPRecord& r = model.Record(record);
    switch (colorMode) {
        case PCPColorMode::PerRecord:
            if (r.colorOverride.a != 0) return r.colorOverride;
            return singleColor;
        case PCPColorMode::ByValue: {
            // Colour by the chosen dimension through the colormap.
            for (size_t d = 0; d < model.DimensionCount(); ++d) {
                if (model.DimensionName(d) != colorDimensionName) continue;
                double lo = 0.0, hi = 1.0;
                bool first = true;
                for (size_t i = 0; i < model.RecordCount(); ++i) {
                    const double v = model.Record(i).values[d];
                    if (!std::isfinite(v)) continue;
                    if (first) { lo = hi = v; first = false; }
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                const double v = r.values[d];
                if (!std::isfinite(v) || !(hi > lo)) break;
                return SampleColormap(colormap, {}, (v - lo) / (hi - lo), colormapReversed);
            }
            return singleColor;
        }
        case PCPColorMode::Single:
            return singleColor;
        case PCPColorMode::ByGroup:
        default: {
            if (r.group.empty()) return singleColor;
            auto it = groupColors.find(r.group);
            if (it != groupColors.end()) return it->second;
            const std::vector<std::string> groups = model.Groups();
            for (size_t g = 0; g < groups.size(); ++g) {
                if (groups[g] == r.group) return FallbackGroupColor(g);
            }
            return singleColor;
        }
    }
}

void UltraCanvasParallelCoordinateChartElement::RebuildLegend() {
    if (colorMode != PCPColorMode::ByGroup) {
        SetShowLegend(false);
        return;
    }
    const std::vector<std::string> groups = model.Groups();
    std::vector<ChartLegendEntry> entries;
    for (size_t g = 0; g < groups.size(); ++g) {
        auto it = groupColors.find(groups[g]);
        entries.push_back({groups[g],
                           (it != groupColors.end()) ? it->second
                                                     : FallbackGroupColor(g)});
    }
    SetLegendEntries(entries);
    SetShowLegend(!entries.empty());
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasParallelCoordinateChartElement::DescribeAxes(ChartAxisSet& axes) {
    displayOrder = model.DisplayOrder();

    model.ConfigureAxes(axes);
    // The model decides which labelling system each axis uses (integrated
    // ticks or endpoint min/max); the element's flag can only switch the
    // endpoint labels off, never force them onto an integrated axis.
    if (!endpointLabels) {
        for (ChartAxis& axis : axes) axis.showEndpointLabels = false;
    }

    // Common scale gains the shared value axis on the left, with gridlines
    // derived from its ticks - the Iris look. In-plot axes must stay first so
    // display slot k lines up with axis index k for the cache.
    if (model.Normalization() == PCPNormalization::CommonScale) {
        double lo = 0.0, hi = 1.0;
        model.SharedRange(lo, hi);
        ChartAxis shared("value");
        // No title: a left-axis title lands below the plot at the same spot
        // as the first in-plot axis's dimension title and would overprint it.
        shared.side = ChartAxisSide::Left;
        shared.SetRange(lo, hi);
        const size_t sharedIndex = axes.Add(shared);
        SetGridAxis(sharedIndex);
    } else {
        SetGridAxis(static_cast<size_t>(-1));
    }

    model.BuildCache(axes);
    RebuildLegend();
}

void UltraCanvasParallelCoordinateChartElement::MeasureContent(
        IRenderContext* ctx, ChartLayoutRequest& request) {
    // Endpoint labels above/below the axes plus the dimension titles below.
    ctx->SetFontSize(axisFontSize);
    const double lineHeight = ctx->GetTextLineHeight("Ag");
    request.Reserve(ChartAxisEdge::Top, lineHeight + 10.0);
    request.Reserve(ChartAxisEdge::Bottom, lineHeight * 2.0 + 16.0);

    // The outer axes' titles and endpoint labels are centred on the axis, so
    // half of the widest of them hangs outside the plot - measure, don't guess.
    const std::vector<size_t> display = model.DisplayOrder();
    if (!display.empty()) {
        request.Reserve(ChartAxisEdge::Left,
                        ctx->GetTextLineWidth(model.DimensionName(display.front())) * 0.5 + 8.0);
        request.Reserve(ChartAxisEdge::Right,
                        ctx->GetTextLineWidth(model.DimensionName(display.back())) * 0.5 + 8.0);
    } else {
        request.Reserve(ChartAxisEdge::Left, 30.0);
        request.Reserve(ChartAxisEdge::Right, 30.0);
    }
}

void UltraCanvasParallelCoordinateChartElement::BuildRecordPath(
        size_t record, const ChartEngineFrame& frame,
        std::vector<Point2Dd>& path) const {
    path.clear();
    for (size_t k = 0; k < displayOrder.size(); ++k) {
        const double v = model.NormalizedValue(record, k);
        if (!std::isfinite(v)) continue;   // runs are split by the caller
        path.push_back(frame.projection->ToScreen(
            ChartNormalizedPoint(model.AxisU(k), v)));
    }
}

void UltraCanvasParallelCoordinateChartElement::StrokeRecord(
        IRenderContext* ctx, const ChartEngineFrame& frame,
        size_t record, const Color& color, float width) const {
    // Collect contiguous runs of finite values; a missing value breaks the line.
    std::vector<Point2Dd> run;
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(width);

    auto flush = [&]() {
        if (run.size() >= 2) {
            if (lineMode == PCPLineMode::Straight) {
                ctx->DrawLinePath(run, false);
            } else {
                // Catmull-Rom through the vertices, one cubic per segment.
                const double k = curveTension / 6.0;
                for (size_t i = 0; i + 1 < run.size(); ++i) {
                    const Point2Dd& p1 = run[i];
                    const Point2Dd& p2 = run[i + 1];
                    const Point2Dd& p0 = (i > 0) ? run[i - 1] : p1;
                    const Point2Dd& p3 = (i + 2 < run.size()) ? run[i + 2] : p2;
                    const Point2Dd c1(p1.x + (p2.x - p0.x) * k, p1.y + (p2.y - p0.y) * k);
                    const Point2Dd c2(p2.x - (p3.x - p1.x) * k, p2.y - (p3.y - p1.y) * k);
                    ctx->DrawBezierCurve(p1, c1, c2, p2);
                }
            }
        }
        run.clear();
    };

    for (size_t k = 0; k < displayOrder.size(); ++k) {
        const double v = model.NormalizedValue(record, k);
        if (!std::isfinite(v)) { flush(); continue; }
        run.push_back(frame.projection->ToScreen(
            ChartNormalizedPoint(model.AxisU(k), v)));
    }
    flush();

    if (showVertexMarkers) {
        ctx->SetFillPaint(color);
        for (size_t k = 0; k < displayOrder.size(); ++k) {
            const double v = model.NormalizedValue(record, k);
            if (!std::isfinite(v)) continue;
            ctx->FillCircle(frame.projection->ToScreen(
                                ChartNormalizedPoint(model.AxisU(k), v)),
                            vertexRadius);
        }
    }
}

void UltraCanvasParallelCoordinateChartElement::RenderChartContent(
        IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (model.RecordCount() == 0 || displayOrder.empty()) return;

    const uint8_t alpha = EffectiveAlpha();
    const bool filtering = model.HasBrushes();

    // Layer order inside the content phase: dimmed context first, passing
    // records next, emphasised records (hovered / pinned) on top.
    if (filtering && showContext) {
        for (size_t r = 0; r < model.RecordCount(); ++r) {
            if (model.RecordPasses(r)) continue;
            StrokeRecord(ctx, frame, r, contextColor, lineWidth);
        }
    }

    for (size_t r = 0; r < model.RecordCount(); ++r) {
        if (filtering && !model.RecordPasses(r)) continue;
        const bool emphasised = (static_cast<int>(r) == hoveredRecord) ||
                                pinnedRecords.count(r);
        if (emphasised) continue;
        StrokeRecord(ctx, frame, r, WithAlpha(RecordColor(r), alpha), lineWidth);
    }

    for (size_t r = 0; r < model.RecordCount(); ++r) {
        const bool emphasised = (static_cast<int>(r) == hoveredRecord) ||
                                pinnedRecords.count(r);
        if (!emphasised) continue;
        // Luminance-opposed casing first, full-strength colour on top: the
        // pair stays visible against neighbours of the very same colour.
        const Color base = RecordColor(r);
        StrokeRecord(ctx, frame, r, HighlightHaloColor(base),
                     lineWidth * 2.0f + 3.0f);
        StrokeRecord(ctx, frame, r, WithAlpha(base, 255), lineWidth * 2.0f);
    }
}

void UltraCanvasParallelCoordinateChartElement::RenderInteractionOverlay(
        IRenderContext* ctx, const ChartEngineFrame& frame) {
    const double bandHalfWidth = 6.0;

    auto drawBand = [&](size_t slot, double vLow, double vHigh) {
        const Point2Dd a = frame.projection->ToScreen(
            ChartNormalizedPoint(model.AxisU(slot), std::max(vLow, vHigh)));
        const Point2Dd b = frame.projection->ToScreen(
            ChartNormalizedPoint(model.AxisU(slot), std::min(vLow, vHigh)));
        const Rect2Dd band(a.x - bandHalfWidth, std::min(a.y, b.y),
                           bandHalfWidth * 2.0, std::abs(b.y - a.y));
        ctx->SetFillPaint(Color(110, 110, 130, 55));
        ctx->FillRectangle(band);
        ctx->SetStrokePaint(Color(90, 90, 110, 160));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(band);
    };

    // Committed brushes, at their dimension's current display slot.
    for (const PCPBrush& brush : model.Brushes()) {
        for (size_t k = 0; k < displayOrder.size(); ++k) {
            if (displayOrder[k] != brush.dimension) continue;
            if (k >= frame.axes->Count()) break;
            const ChartAxis& axis = frame.axes->At(k);
            drawBand(k, axis.Normalize(brush.low), axis.Normalize(brush.high));
            break;
        }
    }

    // The drag in progress.
    if (brushDragSlot >= 0) {
        drawBand(static_cast<size_t>(brushDragSlot), brushDragStartV, brushDragCurrentV);
    }

    // Axis reorder ghost: a vertical rule tracking the pointer.
    if (headerDragSlot >= 0) {
        const Point2Dd top = frame.projection->ToScreen(ChartNormalizedPoint(headerDragU, 1.0));
        const Point2Dd bottom = frame.projection->ToScreen(ChartNormalizedPoint(headerDragU, 0.0));
        ctx->SetStrokePaint(Color(60, 60, 200, 150));
        ctx->SetStrokeWidth(2.0f);
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
        ctx->DrawLine(top, bottom);
        ctx->SetLineDash(UCDashPattern());
    }
}

void UltraCanvasParallelCoordinateChartElement::OnEnginePropertyChanged(
        const std::string& key, const UCPropertyValue& value) {
    double number = 0.0;
    std::string text;
    if (key == "lineAlpha" && UCPropertyAsDouble(value, number)) {
        SetLineAlpha(static_cast<float>(number));
    } else if (key == "lineWidth" && UCPropertyAsDouble(value, number)) {
        SetLineWidth(static_cast<float>(number));
    } else if (key == "curveTension" && UCPropertyAsDouble(value, number)) {
        SetCurveTension(static_cast<float>(number));
    } else if (key == "lineMode" && UCPropertyAsString(value, text)) {
        SetLineMode(text == "curved" ? PCPLineMode::Curved : PCPLineMode::Straight);
    } else if (key == "showEndpointLabels") {
        if (const bool* flag = std::get_if<bool>(&value)) SetShowAxisEndpointLabels(*flag);
    }
}

// =============================================================================
// INTERACTION
// =============================================================================

bool UltraCanvasParallelCoordinateChartElement::ToNormalized(
        const Point2Di& mousePos, double& u, double& v) const {
    const ChartEngineFrame& f = Frame();
    if (!f.projection) return false;
    const ChartNormalizedPoint p = f.projection->ToNormalized(
        Point2Dd(mousePos.x, mousePos.y));
    u = p.u;
    v = p.v;
    return true;
}

int UltraCanvasParallelCoordinateChartElement::SlotNearU(double u, double tolerance) const {
    int best = -1;
    double bestDistance = tolerance;
    for (size_t k = 0; k < displayOrder.size(); ++k) {
        const double d = std::abs(u - model.AxisU(k));
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(k);
        }
    }
    return best;
}

bool UltraCanvasParallelCoordinateChartElement::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;

    const Point2Di mousePos(event.pointer.x, event.pointer.y);
    double u = 0.0, v = 0.0;

    switch (event.type) {
        case UCEventType::MouseMove: {
            if (!ToNormalized(mousePos, u, v)) break;

            if (brushDragSlot >= 0) {
                brushDragCurrentV = std::clamp(v, 0.0, 1.0);
                RequestRedraw();
                return true;
            }
            if (headerDragSlot >= 0) {
                headerDragU = std::clamp(u, 0.0, 1.0);
                RequestRedraw();
                return true;
            }

            // Hover: emphasise the nearest line and describe the record.
            const double tolerance =
                10.0 / std::max(1.0, Frame().plotArea.height);
            const int hit = (u >= -0.02 && u <= 1.02 && v >= -0.02 && v <= 1.02)
                                ? model.NearestRecord(u, v, tolerance)
                                : -1;
            if (hit != hoveredRecord) {
                hoveredRecord = hit;
                if (hit >= 0 && enableTooltips) {
                    const PCPRecord& record = model.Record(static_cast<size_t>(hit));
                    std::ostringstream text;
                    if (!record.label.empty()) text << record.label << "\n";
                    else if (!record.group.empty()) text << record.group << "\n";
                    for (size_t k = 0; k < displayOrder.size(); ++k) {
                        const size_t dim = displayOrder[k];
                        if (k) text << "\n";
                        text << model.DimensionName(dim) << ": ";
                        const double value = record.values[dim];
                        if (std::isfinite(value)) text << value;
                        else text << "-";
                    }
                    const Point2Di windowPos = MapFromLocal(mousePos, nullptr);
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        this->window, text.str(), windowPos);
                } else {
                    HideTooltip();
                }
                RequestRedraw();
            }
            return hit >= 0;
        }

        case UCEventType::MouseDown: {
            if (!ToNormalized(mousePos, u, v)) break;
            const int slot = SlotNearU(u, 0.03);

            if (slot >= 0 && v > 1.0 && v < 1.15) {
                // Above the axis top: grab the header to reorder.
                headerDragSlot = slot;
                headerDragU = model.AxisU(static_cast<size_t>(slot));
                return true;
            }
            if (slot >= 0 && brushingEnabled && v >= -0.02 && v <= 1.02) {
                brushDragSlot = slot;
                brushDragStartV = brushDragCurrentV = std::clamp(v, 0.0, 1.0);
                return true;
            }
            if (hoveredRecord >= 0) {
                const size_t record = static_cast<size_t>(hoveredRecord);
                PinRecord(record, !pinnedRecords.count(record));
                if (onRecordClick) onRecordClick(hoveredRecord);
                return true;
            }
            break;
        }

        case UCEventType::MouseUp: {
            if (brushDragSlot >= 0) {
                const size_t slot = static_cast<size_t>(brushDragSlot);
                brushDragSlot = -1;
                const ChartEngineFrame& f = Frame();
                if (slot < displayOrder.size() && slot < f.axes->Count()) {
                    const size_t dim = displayOrder[slot];
                    if (std::abs(brushDragCurrentV - brushDragStartV) < 0.01) {
                        // A click on the axis clears its brushes.
                        model.ClearBrushes(dim);
                    } else {
                        const ChartAxis& axis = f.axes->At(slot);
                        const double a = axis.Denormalize(brushDragStartV);
                        const double b = axis.Denormalize(brushDragCurrentV);
                        model.AddBrush(dim, std::min(a, b), std::max(a, b));
                    }
                    if (onBrushChanged) onBrushChanged(model.PassingCount());
                }
                MarkEngineDirty(ChartDirty::Selection);
                return true;
            }
            if (headerDragSlot >= 0) {
                const size_t from = static_cast<size_t>(headerDragSlot);
                headerDragSlot = -1;
                const int target = SlotNearU(headerDragU, 1.0);
                if (target >= 0 && static_cast<size_t>(target) != from) {
                    model.MoveAxis(from, static_cast<size_t>(target));
                    MarkEngineDirty(ChartDirty::Data);
                    EmitAxisOrderChanged();
                } else {
                    RequestRedraw();
                }
                return true;
            }
            break;
        }

        case UCEventType::MouseDoubleClick: {
            if (!ToNormalized(mousePos, u, v)) break;
            const int slot = SlotNearU(u, 0.03);
            if (slot >= 0 && static_cast<size_t>(slot) < displayOrder.size()) {
                const size_t dim = displayOrder[static_cast<size_t>(slot)];
                model.SetAxisInverted(dim, !model.IsAxisInverted(dim));
                MarkEngineDirty(ChartDirty::Data);
                return true;
            }
            break;
        }

        case UCEventType::MouseLeave: {
            if (hoveredRecord != -1) {
                hoveredRecord = -1;
                HideTooltip();
                RequestRedraw();
            }
            break;
        }

        default:
            break;
    }
    return UltraCanvasChartElementBase::OnEvent(event);
}

void UltraCanvasParallelCoordinateChartElement::EmitAxisOrderChanged() {
    if (!onAxisOrderChanged) return;
    std::vector<std::string> names;
    for (size_t dim : model.DisplayOrder()) names.push_back(model.DimensionName(dim));
    onAxisOrderChanged(names);
}

// =============================================================================
// FACTORY AND REGISTRATION
// =============================================================================

std::shared_ptr<UltraCanvasParallelCoordinateChartElement>
CreateParallelCoordinateChartElement(const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasParallelCoordinateChartElement>(id, x, y, w, h);
}

UCElementDescriptor MakeParallelCoordinateChartDescriptor() {
    UCElementDescriptor descriptor;
    descriptor.typeName = "parallel-coordinate-chart";
    descriptor.category = UCPluginCategory::Chart;
    descriptor.displayName = "Parallel Coordinates";
    descriptor.description = "Multi-dimensional records as polylines across parallel axes";
    descriptor.version = "1.0.0";
    descriptor.docPath = "Docs/UltraCanvas/UltraCanvasParallelCoordinateChart.md";
    descriptor.propertyKeys = {"title", "lineAlpha", "lineWidth", "curveTension",
                               "lineMode", "showEndpointLabels"};
    descriptor.create = [](const std::string& id, int x, int y, int w, int h) {
        return std::static_pointer_cast<UltraCanvasUIElement>(
            CreateParallelCoordinateChartElement(id, x, y, w, h));
    };
    return descriptor;
}

void RegisterParallelCoordinateChartElement() {
    UltraCanvasElementRegistry::Register(MakeParallelCoordinateChartDescriptor());
}

} // namespace UltraCanvas
