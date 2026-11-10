// include/Plugins/Charts/Engine/Charts/UltraCanvasHistogramModel.h
// The UI-free model of the image histogram: how many pixels of a picture
// have each of the 256 levels of red, green, blue and luminosity, and the
// statistics a photo editor shows beside it.
//
// ComputeImageHistogram reads a pixel buffer in any of the usual layouts
// (Cairo's premultiplied ARGB32, RGBA8, RGB8). Premultiplied pixels are
// un-premultiplied first, so a half-transparent red counts as red, and fully
// transparent pixels are left out unless asked for: they have no colour.
// Luminosity is Rec. 601 luma (0.299 R + 0.587 G + 0.114 B), the weighting
// photo editors' luminosity histograms use.
//
// UltraCanvasHistogramChart draws it. Guide: Docs/UltraCanvas/UltraCanvasHistogramChart.md
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace UltraCanvas {

enum class HistogramChannel { Red, Green, Blue, Luminosity };

enum class HistogramPixelFormat {
    ARGB32Premultiplied,   // one native-endian uint32 per pixel, A in the top byte (Cairo)
    XRGB32,                // the same without alpha: the top byte is undefined (Cairo RGB24)
    RGBA8,                 // bytes R, G, B, A
    RGB8                   // bytes R, G, B (opaque)
};

struct HistogramStatistics {
    uint64_t count = 0;            // pixels counted
    double mean = 0.0;             // levels 0..255
    double standardDeviation = 0.0;
    int median = 0;
    int minimum = 0;               // the lowest level with any pixel
    int maximum = 0;               // the highest
    // Shares of the pixels at 0 and at 255: shadows and highlights clipped.
    double shadowsClipped = 0.0;
    double highlightsClipped = 0.0;
};

struct ImageHistogram {
    // bins[channel][level], channels in HistogramChannel order.
    std::array<std::array<uint64_t, 256>, 4> bins{};
    uint64_t pixelCount = 0;       // pixels counted (transparent ones skipped)

    const std::array<uint64_t, 256>& Bins(HistogramChannel channel) const {
        return bins[static_cast<size_t>(channel)];
    }
    uint64_t CountAt(HistogramChannel channel, int level) const {
        return (level < 0 || level > 255) ? 0 : Bins(channel)[static_cast<size_t>(level)];
    }
    bool Empty() const { return pixelCount == 0; }
    // The tallest bin of the channel.
    uint64_t MaxCount(HistogramChannel channel) const;
    // The level below which `fraction` (0..1) of the pixels lie.
    int Percentile(HistogramChannel channel, double fraction) const;
    HistogramStatistics Statistics(HistogramChannel channel) const;
};

// The luminosity level of a colour: Rec. 601 luma, rounded.
int HistogramLuminosity(int red, int green, int blue);

// Counts the pixels of a width x height buffer whose rows are strideBytes
// apart. skipTransparent leaves out pixels with alpha 0 (they have no colour).
ImageHistogram ComputeImageHistogram(const uint8_t* pixels, int width, int height, int strideBytes,
                                     HistogramPixelFormat format, bool skipTransparent = true);

} // namespace UltraCanvas
