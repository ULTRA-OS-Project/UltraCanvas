// Plugins/Charts/Engine/Charts/UltraCanvasHistogramChart.cpp
// An image histogram on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasHistogramChart.h"
#include "UltraCanvasImage.h"
#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace UltraCanvas {

namespace {

const char* ChannelName(HistogramChannel channel) {
    switch (channel) {
        case HistogramChannel::Red: return "Red";
        case HistogramChannel::Green: return "Green";
        case HistogramChannel::Blue: return "Blue";
        default: return "Luminosity";
    }
}

const Color kDefaultColors[4] = {Color(222, 52, 52, 255), Color(46, 168, 72, 255), Color(44, 102, 226, 255),
                                 Color(108, 112, 120, 255)};

// 1234567 -> "1,234,567": a read-out, not a file.
std::string Grouped(uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    const size_t lead = digits.size() % 3;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (i - lead) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string ShareText(double share) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f%%", share * 100.0);   // locale-ok: display text
    return buffer;
}

} // namespace

UltraCanvasHistogramChart::UltraCanvasHistogramChart(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    channelColors.fill(Color(0, 0, 0, 0));      // alpha 0: each channel's default colour
    SetLegendPosition(ChartLegendPosition::TopCenter);
    RefreshLegend();
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasHistogramChart::SetHistogram(const ImageHistogram& h) {
    histogram = h;
    MarkEngineDirty(ChartDirty::Data);
}

bool UltraCanvasHistogramChart::SetImage(const std::shared_ptr<UCImageRaster>& image) {
    histogram = ImageHistogram();
    bool counted = false;
    if (image) {
        // At the picture's own size: every pixel counted once.
        std::shared_ptr<UCPixmapCairo> pixmap = image->GetPixmap(image->GetWidth(), image->GetHeight());
        cairo_surface_t* surface = pixmap ? pixmap->GetSurface() : nullptr;
        if (surface && cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE) {
            cairo_surface_flush(surface);
            const cairo_format_t format = cairo_image_surface_get_format(surface);
            const unsigned char* data = cairo_image_surface_get_data(surface);
            if (data && (format == CAIRO_FORMAT_ARGB32 || format == CAIRO_FORMAT_RGB24)) {
                histogram = ComputeImageHistogram(
                    data, cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface),
                    cairo_image_surface_get_stride(surface),
                    format == CAIRO_FORMAT_RGB24 ? HistogramPixelFormat::XRGB32
                                                 : HistogramPixelFormat::ARGB32Premultiplied);
                counted = !histogram.Empty();
            }
        }
    }
    MarkEngineDirty(ChartDirty::Data);
    return counted;
}

void UltraCanvasHistogramChart::ClearHistogram() {
    histogram = ImageHistogram();
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// PRESENTATION
// =============================================================================

void UltraCanvasHistogramChart::SetDisplay(HistogramDisplay d) {
    display = d;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);      // the count range follows the channels shown
}

void UltraCanvasHistogramChart::SetScale(HistogramScale s) {
    scale = s;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasHistogramChart::SetChannelColor(HistogramChannel channel, const Color& color) {
    channelColors[static_cast<size_t>(channel)] = color;
    RefreshLegend();
    RequestRedraw();
}

Color UltraCanvasHistogramChart::GetChannelColor(HistogramChannel channel) const {
    const Color& c = channelColors[static_cast<size_t>(channel)];
    return c.a > 0 ? c : kDefaultColors[static_cast<size_t>(channel)];
}

void UltraCanvasHistogramChart::SetFillOpacity(float opacity) {
    fillOpacity = std::clamp(opacity, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasHistogramChart::SetAxisTitles(const std::string& level, const std::string& count) {
    levelTitle = level;
    countTitle = count;
    MarkEngineDirty(ChartDirty::Style);
}

std::vector<HistogramChannel> UltraCanvasHistogramChart::ShownChannels() const {
    switch (display) {
        case HistogramDisplay::Red: return {HistogramChannel::Red};
        case HistogramDisplay::Green: return {HistogramChannel::Green};
        case HistogramDisplay::Blue: return {HistogramChannel::Blue};
        case HistogramDisplay::Luminosity: return {HistogramChannel::Luminosity};
        default: break;
    }
    std::vector<HistogramChannel> shown;
    for (HistogramChannel c : {HistogramChannel::Red, HistogramChannel::Green, HistogramChannel::Blue}) {
        if (!hiddenChannels[static_cast<size_t>(c)]) shown.push_back(c);
    }
    return shown;
}

std::string UltraCanvasHistogramChart::DescribeLevel(int level) const {
    if (level < 0 || level > 255) return std::string();
    std::ostringstream text;
    text << "Level " << level;
    const double total = static_cast<double>(histogram.pixelCount);
    for (HistogramChannel c : ShownChannels()) {
        const uint64_t n = histogram.CountAt(c, level);
        text << "\n" << ChannelName(c) << ": " << Grouped(n) << " pixels";
        if (total > 0.0) text << " (" << ShareText(static_cast<double>(n) / total) << ")";
    }
    return text.str();
}

void UltraCanvasHistogramChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    if (display == HistogramDisplay::Combined) {
        for (HistogramChannel c : {HistogramChannel::Red, HistogramChannel::Green, HistogramChannel::Blue}) {
            ChartLegendEntry entry(ChannelName(c), GetChannelColor(c), LegendSwatch::Square);
            entry.enabled = !hiddenChannels[static_cast<size_t>(c)];
            entries.push_back(entry);
        }
    }
    SetLegendEntries(entries);
    SetShowLegend(!entries.empty());
}

void UltraCanvasHistogramChart::OnChartThemeChanged() {
    RefreshLegend();
}

void UltraCanvasHistogramChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (display != HistogramDisplay::Combined || entryIndex >= 3) return;
    hiddenChannels[entryIndex] = !enabled;      // entries are red, green, blue
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasHistogramChart::OnHitRegionHoverChanged(int64_t regionId) {
    const int level = (regionId >= 0 && regionId <= 255) ? static_cast<int>(regionId) : -1;
    if (level == hoveredLevel) return;
    hoveredLevel = level;
    if (onLevelHover) onLevelHover(level);
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasHistogramChart::DescribeAxes(ChartAxisSet& axes) {
    ChartAxis counts("counts");
    counts.side = ChartAxisSide::Left;
    counts.title = countTitle;
    counts.compactNumbers = true;
    counts.decimals = 0;
    if (scale == HistogramScale::Logarithmic) {
        // Linear below one pixel, logarithmic above: an empty level stays at zero.
        counts.scale = ChartScale::SymLog;
        counts.symLogThreshold = 1.0;
    }
    counts.Observe(0.0);
    uint64_t most = 0;
    for (HistogramChannel c : ShownChannels()) most = std::max(most, histogram.MaxCount(c));
    counts.Observe(most > 0 ? static_cast<double>(most) : 1.0);
    if (scale == HistogramScale::Logarithmic) {
        // Decades, not linear steps: 0, 1, 10, 100, ... up to the tallest bin,
        // so the axis reads across the whole range the scale spreads out.
        counts.tickValues = {0.0};
        for (double decade = 1.0; decade <= std::max<double>(1.0, static_cast<double>(most)); decade *= 10.0) {
            counts.tickValues.push_back(decade);
        }
    }
    axes.Add(counts);

    ChartAxis levels("levels");
    levels.side = ChartAxisSide::Bottom;
    levels.title = levelTitle;
    levels.SetRange(-0.5, 255.5);            // every level a full column
    levels.decimals = 0;
    levels.tickValues = {0, 64, 128, 192, 255};
    axes.Add(levels);

    SetGridAxis(0);
}

void UltraCanvasHistogramChart::RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (!frame.axes || frame.axes->Count() < 2 || !frame.projection) return;
    const ChartAxis& counts = frame.axes->At(0);
    const ChartAxis& levels = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const double base = std::clamp(counts.Normalize(0.0), 0.0, 1.0);
    const double progress = frame.animationProgress;
    auto at = [&](double level, double v) { return projection.ToScreen(ChartNormalizedPoint(levels.Normalize(level), v)); };

    // Each channel as a stepped outline: one flat top per level, filled down
    // to zero, back to front so the overlaps of Combined mix.
    for (HistogramChannel c : ShownChannels()) {
        const std::array<uint64_t, 256>& bins = histogram.Bins(c);
        std::vector<Point2Dd> top;
        top.reserve(514);
        for (int level = 0; level < 256; ++level) {
            const double v = base + (counts.Normalize(static_cast<double>(bins[static_cast<size_t>(level)])) - base) * progress;
            top.push_back(at(level - 0.5, v));
            top.push_back(at(level + 0.5, v));
        }
        std::vector<Point2Dd> outline = top;
        outline.push_back(at(255.5, base));
        outline.push_back(at(-0.5, base));
        const Color color = GetChannelColor(c);
        ctx->SetFillPaint(color.WithAlpha(static_cast<uint8_t>(255.0f * fillOpacity)));
        ctx->FillLinePath(outline);
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.2f);
        ctx->DrawLinePath(top, false);
    }

    // The level under the pointer: a thin rule through its column.
    if (hoveredLevel >= 0) {
        ctx->SetStrokePaint(GetTheme().axisLineColor.WithAlpha(160));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(at(hoveredLevel, 0.0), at(hoveredLevel, 1.0));
    }

    // One hit region per level, the plot's full height: the read-out.
    if (histogram.Empty()) return;
    for (int level = 0; level < 256; ++level) {
        const Point2Dd a = at(level - 0.5, 1.0), b = at(level + 0.5, 0.0);
        AddHitRegion(Rect2Dd(std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)),
                     level, DescribeLevel(level));
    }
}

} // namespace UltraCanvas
