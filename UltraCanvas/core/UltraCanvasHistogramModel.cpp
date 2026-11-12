// core/UltraCanvasHistogramModel.cpp
// The UI-free model of the image histogram.
// Version: 1.2.0 - ClippedRange
// Version: 1.1.0 - moved to the core library; Add, RgbTotal
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasHistogramModel.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace UltraCanvas {

int HistogramLuminosity(int red, int green, int blue) {
    // Integer Rec. 601 luma, rounded: (299 R + 587 G + 114 B + 500) / 1000.
    return (299 * red + 587 * green + 114 * blue + 500) / 1000;
}

uint64_t ImageHistogram::MaxCount(HistogramChannel channel) const {
    uint64_t most = 0;
    for (uint64_t n : Bins(channel)) most = n > most ? n : most;
    return most;
}

int ImageHistogram::Percentile(HistogramChannel channel, double fraction) const {
    const std::array<uint64_t, 256>& b = Bins(channel);
    uint64_t total = 0;
    for (uint64_t n : b) total += n;
    if (total == 0) return 0;
    if (fraction <= 0.0) fraction = 0.0;
    if (fraction >= 1.0) fraction = 1.0;
    // The first level whose running count reaches the fraction of the pixels.
    const double target = fraction * static_cast<double>(total);
    uint64_t running = 0;
    for (int level = 0; level < 256; ++level) {
        running += b[static_cast<size_t>(level)];
        if (static_cast<double>(running) >= target && running > 0) return level;
    }
    return 255;
}

HistogramStatistics ImageHistogram::Statistics(HistogramChannel channel) const {
    HistogramStatistics s;
    const std::array<uint64_t, 256>& b = Bins(channel);
    double sum = 0.0, squares = 0.0;
    int lowest = -1, highest = -1;
    for (int level = 0; level < 256; ++level) {
        const uint64_t n = b[static_cast<size_t>(level)];
        if (n == 0) continue;
        s.count += n;
        sum += static_cast<double>(n) * level;
        squares += static_cast<double>(n) * level * level;
        if (lowest < 0) lowest = level;
        highest = level;
    }
    if (s.count == 0) return s;
    const double count = static_cast<double>(s.count);
    s.mean = sum / count;
    s.standardDeviation = std::sqrt(std::max(0.0, squares / count - s.mean * s.mean));
    s.median = Percentile(channel, 0.5);
    s.minimum = lowest;
    s.maximum = highest;
    s.shadowsClipped = static_cast<double>(b[0]) / count;
    s.highlightsClipped = static_cast<double>(b[255]) / count;
    return s;
}

ImageHistogram& ImageHistogram::Add(const ImageHistogram& other) {
    for (size_t c = 0; c < bins.size(); ++c)
        for (size_t level = 0; level < 256; ++level) bins[c][level] += other.bins[c][level];
    pixelCount += other.pixelCount;
    return *this;
}

std::array<uint64_t, 256> ImageHistogram::RgbTotal() const {
    std::array<uint64_t, 256> total{};
    for (size_t level = 0; level < 256; ++level)
        total[level] = bins[0][level] + bins[1][level] + bins[2][level];
    return total;
}

HistogramRange ImageHistogram::ClippedRange(HistogramChannel channel, double fraction) const {
    HistogramRange range;
    if (pixelCount == 0) return range;
    fraction = std::clamp(fraction, 0.0, 1.0);
    // Whole pixels only: 0.2 % of 400 pixels lets none go.
    const uint64_t clip = static_cast<uint64_t>(static_cast<double>(pixelCount) * fraction);
    const std::array<uint64_t, 256>& b = Bins(channel);
    uint64_t seen = 0;
    while (range.low < 255 && seen + b[static_cast<size_t>(range.low)] <= clip)
        seen += b[static_cast<size_t>(range.low++)];
    seen = 0;
    while (range.high > range.low && seen + b[static_cast<size_t>(range.high)] <= clip)
        seen += b[static_cast<size_t>(range.high--)];
    return range;
}

ImageHistogram ComputeImageHistogram(const uint8_t* pixels, int width, int height, int strideBytes,
                                     HistogramPixelFormat format, bool skipTransparent) {
    ImageHistogram h;
    if (!pixels || width <= 0 || height <= 0) return h;
    const int bytesPerPixel = format == HistogramPixelFormat::RGB8 ? 3 : 4;
    if (strideBytes < width * bytesPerPixel) return h;
    auto& red = h.bins[0];
    auto& green = h.bins[1];
    auto& blue = h.bins[2];
    auto& luminosity = h.bins[3];
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
        for (int x = 0; x < width; ++x) {
            int r = 0, g = 0, b = 0, a = 255;
            if (format == HistogramPixelFormat::ARGB32Premultiplied ||
                format == HistogramPixelFormat::XRGB32) {
                uint32_t p = 0;
                std::memcpy(&p, row + static_cast<size_t>(x) * 4, sizeof(p));
                a = format == HistogramPixelFormat::XRGB32 ? 255 : static_cast<int>(p >> 24);
                r = static_cast<int>((p >> 16) & 0xFF);
                g = static_cast<int>((p >> 8) & 0xFF);
                b = static_cast<int>(p & 0xFF);
                if (a > 0 && a < 255) {
                    // Back to straight colour, rounded.
                    r = std::min(255, (r * 255 + a / 2) / a);
                    g = std::min(255, (g * 255 + a / 2) / a);
                    b = std::min(255, (b * 255 + a / 2) / a);
                }
            } else {
                const uint8_t* p = row + static_cast<size_t>(x) * static_cast<size_t>(bytesPerPixel);
                r = p[0];
                g = p[1];
                b = p[2];
                if (format == HistogramPixelFormat::RGBA8) a = p[3];
            }
            if (a == 0 && skipTransparent) continue;
            ++red[static_cast<size_t>(r)];
            ++green[static_cast<size_t>(g)];
            ++blue[static_cast<size_t>(b)];
            ++luminosity[static_cast<size_t>(HistogramLuminosity(r, g, b))];
            ++h.pixelCount;
        }
    }
    return h;
}

} // namespace UltraCanvas
