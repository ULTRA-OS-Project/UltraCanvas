// include/Plugins/Charts/Engine/Charts/UltraCanvasHistogramChart.h
// An image histogram on the chart engine: how many pixels have each of the
// 256 levels, shown for the red, green and blue channels together (Combined:
// the three overlaid in their own colours) or for one channel alone - red,
// green, blue or luminosity.
//
// The engine supplies the axes (levels 0..255 along the bottom, pixel counts
// up the side, linear or logarithmic), the grid, the legend, the hover
// tooltips and themes; this class adds the stepped, filled channel outlines
// and a read-out of the level under the pointer. The counts come from
// UltraCanvasHistogramModel (UI-free); SetImage computes them from a picture.
//
// Axis indices: 0 = counts, 1 = levels.
// Guide: Docs/UltraCanvas/UltraCanvasHistogramChart.md
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasHistogramModel.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UCImageRaster;

// What the histogram shows.
enum class HistogramDisplay {
    Combined,      // red, green and blue overlaid, each in its own colour
    Red,
    Green,
    Blue,
    Luminosity     // the brightness of each pixel (Rec. 601 luma)
};

enum class HistogramScale {
    Linear,
    Logarithmic    // tall spikes no longer flatten the rest; empty levels stay at zero
};

class UltraCanvasHistogramChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasHistogramChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetHistogram(const ImageHistogram& histogram);
    const ImageHistogram& GetHistogram() const { return histogram; }
    // Counts the image's pixels (at its own size; transparent pixels left
    // out). False, and an empty histogram, for an image without pixels.
    bool SetImage(const std::shared_ptr<UCImageRaster>& image);
    void ClearHistogram();

    // ---- presentation ---------------------------------------------------
    void SetDisplay(HistogramDisplay display);
    HistogramDisplay GetDisplay() const { return display; }
    void SetScale(HistogramScale scale);
    HistogramScale GetScale() const { return scale; }
    // The channels' colours; alpha 0 = the defaults (red, green, blue, grey).
    void SetChannelColor(HistogramChannel channel, const Color& color);
    Color GetChannelColor(HistogramChannel channel) const;
    // How opaque a channel's fill is, 0..1 (its outline is solid).
    void SetFillOpacity(float opacity);
    void SetAxisTitles(const std::string& levelTitle, const std::string& countTitle);

    // The channels the current display draws, back to front (in Combined,
    // those the legend has not switched off).
    std::vector<HistogramChannel> ShownChannels() const;
    // The level under the pointer, -1 when none.
    int GetHoveredLevel() const { return hoveredLevel; }
    // The text the hover read-out shows for a level.
    std::string DescribeLevel(int level) const;
    // A level is hovered or left (-1).
    std::function<void(int level)> onLevelHover;

    // ---- engine contract --------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnHitRegionHoverChanged(int64_t regionId) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    ImageHistogram histogram;
    HistogramDisplay display = HistogramDisplay::Combined;
    HistogramScale scale = HistogramScale::Linear;
    std::array<Color, 4> channelColors{};
    float fillOpacity = 0.45f;
    std::string levelTitle = "Level";
    std::string countTitle = "Pixels";
    int hoveredLevel = -1;
    std::array<bool, 4> hiddenChannels{};     // switched off in the legend (Combined)

    void RefreshLegend();
};

inline std::shared_ptr<UltraCanvasHistogramChart> CreateHistogramChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasHistogramChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
