// Tests/HistogramModelTest.cpp
// Unit tests for the image histogram model: counting each pixel layout,
// premultiplied alpha, transparent pixels, luminosity, the statistics and the
// percentiles, adding counts, the red-green-blue total and the clipped range
// an auto contrast stretches. UI-free.
//
// Version: 1.2.0 - ClippedRange
// Version: 1.1.0 - Add, RgbTotal; the model is in the core library
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasHistogramModel.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static uint32_t Argb(int a, int r, int g, int b) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

static void TestLayouts() {
    std::printf("\n[Pixel layouts]\n");
    // Two rows of two pixels, with a padded stride.
    const uint8_t rgba[2 * 12] = {255, 0, 0, 255,   0, 255, 0, 255,   9, 9, 9, 9,
                                  0, 0, 255, 255,   10, 20, 30, 255,  9, 9, 9, 9};
    const ImageHistogram h = ComputeImageHistogram(rgba, 2, 2, 12, HistogramPixelFormat::RGBA8);
    CHECK(h.pixelCount == 4, "RGBA8: four pixels counted, the stride padding skipped");
    CHECK(h.CountAt(HistogramChannel::Red, 255) == 1 && h.CountAt(HistogramChannel::Red, 0) == 2 &&
              h.CountAt(HistogramChannel::Red, 10) == 1,
          "red levels: 255 once, 0 twice, 10 once");
    CHECK(h.CountAt(HistogramChannel::Blue, 255) == 1 && h.CountAt(HistogramChannel::Green, 20) == 1,
          "green and blue counted from their own bytes");

    const uint8_t rgb[6] = {100, 150, 200, 50, 60, 70};
    const ImageHistogram h3 = ComputeImageHistogram(rgb, 2, 1, 6, HistogramPixelFormat::RGB8);
    CHECK(h3.pixelCount == 2 && h3.CountAt(HistogramChannel::Green, 150) == 1 && h3.CountAt(HistogramChannel::Blue, 70) == 1,
          "RGB8: three bytes a pixel");

    std::vector<uint32_t> argb = {Argb(255, 200, 100, 50), Argb(128, 64, 32, 0), Argb(0, 0, 0, 0)};
    const ImageHistogram ha = ComputeImageHistogram(reinterpret_cast<const uint8_t*>(argb.data()), 3, 1, 12,
                                                    HistogramPixelFormat::ARGB32Premultiplied);
    CHECK(ha.pixelCount == 2, "ARGB32: a fully transparent pixel is left out");
    CHECK(ha.CountAt(HistogramChannel::Red, 200) == 1, "an opaque pixel counts as it is");
    CHECK(ha.CountAt(HistogramChannel::Red, 128) == 1 && ha.CountAt(HistogramChannel::Green, 64) == 1,
          "a half-transparent pixel is un-premultiplied first (64 at alpha 128 is 128)");
    const ImageHistogram withClear = ComputeImageHistogram(reinterpret_cast<const uint8_t*>(argb.data()), 3, 1, 12,
                                                           HistogramPixelFormat::ARGB32Premultiplied, false);
    CHECK(withClear.pixelCount == 3 && withClear.CountAt(HistogramChannel::Red, 0) == 1,
          "skipTransparent = false counts it, as black");

    std::vector<uint32_t> xrgb = {0x00FF8040u, 0x12FF8040u};
    const ImageHistogram hx = ComputeImageHistogram(reinterpret_cast<const uint8_t*>(xrgb.data()), 2, 1, 8,
                                                    HistogramPixelFormat::XRGB32);
    CHECK(hx.pixelCount == 2 && hx.CountAt(HistogramChannel::Red, 255) == 2 && hx.CountAt(HistogramChannel::Blue, 64) == 2,
          "XRGB32: the undefined top byte is ignored, every pixel opaque");

    CHECK(ComputeImageHistogram(nullptr, 2, 2, 8, HistogramPixelFormat::RGBA8).Empty() &&
              ComputeImageHistogram(rgba, 2, 2, 4, HistogramPixelFormat::RGBA8).Empty(),
          "no pixels, or a stride shorter than a row: an empty histogram");
}

static void TestLuminosity() {
    std::printf("\n[Luminosity]\n");
    CHECK(HistogramLuminosity(255, 255, 255) == 255 && HistogramLuminosity(0, 0, 0) == 0,
          "white is 255, black 0");
    CHECK(HistogramLuminosity(255, 0, 0) == 76 && HistogramLuminosity(0, 255, 0) == 150 &&
              HistogramLuminosity(0, 0, 255) == 29,
          "Rec. 601 weights: pure red 76, green 150, blue 29");
    const uint8_t grey[4] = {128, 128, 128, 255};
    const ImageHistogram h = ComputeImageHistogram(grey, 1, 1, 4, HistogramPixelFormat::RGBA8);
    CHECK(h.CountAt(HistogramChannel::Luminosity, 128) == 1, "a grey's luminosity is its level");
}

static void TestStatistics() {
    std::printf("\n[Statistics and percentiles]\n");
    // Ten grey pixels: levels 0, 10, 20, ..., 90, and one at 255.
    std::vector<uint8_t> px;
    for (int i = 0; i < 10; ++i) {
        const uint8_t v = static_cast<uint8_t>(i * 10);
        px.insert(px.end(), {v, v, v});
    }
    px.insert(px.end(), {255, 255, 255});
    const ImageHistogram h = ComputeImageHistogram(px.data(), 11, 1, 33, HistogramPixelFormat::RGB8);
    const HistogramStatistics s = h.Statistics(HistogramChannel::Red);
    const double mean = (450.0 + 255.0) / 11.0;
    double ss = 0.0;
    for (int i = 0; i < 10; ++i) ss += (i * 10 - mean) * (i * 10 - mean);
    ss += (255 - mean) * (255 - mean);
    CHECK(s.count == 11 && Near(s.mean, mean, 1e-9), "mean of the levels");
    CHECK(Near(s.standardDeviation, std::sqrt(ss / 11.0), 1e-9), "population standard deviation");
    CHECK(s.minimum == 0 && s.maximum == 255, "lowest and highest level present");
    CHECK(s.median == 50, "median: the sixth of eleven, level 50");
    CHECK(Near(s.shadowsClipped, 1.0 / 11.0, 1e-12) && Near(s.highlightsClipped, 1.0 / 11.0, 1e-12),
          "shares at 0 and 255: shadows and highlights clipped");
    CHECK(h.Percentile(HistogramChannel::Red, 0.0) == 0 && h.Percentile(HistogramChannel::Red, 1.0) == 255 &&
              h.Percentile(HistogramChannel::Red, 0.25) == 20,
          "percentiles: 0 %, 100 % and 25 %");
    CHECK(h.MaxCount(HistogramChannel::Red) == 1, "the tallest bin");
    const HistogramStatistics none = ImageHistogram().Statistics(HistogramChannel::Blue);
    CHECK(none.count == 0 && none.mean == 0.0, "an empty histogram has empty statistics");
}

static void TestAddAndTotal() {
    std::printf("\n[Adding counts, the red-green-blue total]\n");
    const uint8_t top[6] = {10, 20, 30, 10, 10, 10};
    const uint8_t bottom[3] = {30, 20, 10};
    ImageHistogram whole = ComputeImageHistogram(top, 2, 1, 6, HistogramPixelFormat::RGB8);
    whole.Add(ComputeImageHistogram(bottom, 1, 1, 3, HistogramPixelFormat::RGB8));
    const uint8_t together[9] = {10, 20, 30, 10, 10, 10, 30, 20, 10};
    const ImageHistogram once = ComputeImageHistogram(together, 3, 1, 9, HistogramPixelFormat::RGB8);
    CHECK(whole.pixelCount == 3 && whole.bins == once.bins,
          "two strips added count the same as the picture counted at once");
    const std::array<uint64_t, 256> total = whole.RgbTotal();
    CHECK(total[10] == 5 && total[20] == 2 && total[30] == 2,
          "RgbTotal: every red, green and blue value at its level (5 tens, 2 twenties, 2 thirties)");
    uint64_t sum = 0;
    for (uint64_t n : total) sum += n;
    CHECK(sum == 3 * whole.pixelCount, "three values a pixel");
}

static void TestClippedRange() {
    std::printf("\n[Clipped range]\n");
    // 1,000 grey pixels: one at 0, one at 255, the rest spread over 40..199.
    std::vector<uint8_t> px;
    px.insert(px.end(), {0, 0, 0});
    for (int i = 0; i < 998; ++i) {
        const uint8_t v = static_cast<uint8_t>(40 + i % 160);
        px.insert(px.end(), {v, v, v});
    }
    px.insert(px.end(), {255, 255, 255});
    const ImageHistogram h = ComputeImageHistogram(px.data(), 1000, 1, 3000, HistogramPixelFormat::RGB8);
    const HistogramRange none = h.ClippedRange(HistogramChannel::Red, 0.0);
    CHECK(none.low == 0 && none.high == 255, "fraction 0: the lone black and white pixels hold the full range");
    const HistogramRange some = h.ClippedRange(HistogramChannel::Red, 0.002);
    CHECK(some.low == 40 && some.high == 199, "0.2 % (two pixels) lets each outlier go: 40..199");
    const HistogramRange tooFew = h.ClippedRange(HistogramChannel::Red, 0.0009);
    CHECK(tooFew.low == 0 && tooFew.high == 255, "under one whole pixel lets none go");

    const uint8_t flat[6] = {77, 77, 77, 77, 77, 77};
    const HistogramRange one = ComputeImageHistogram(flat, 2, 1, 6, HistogramPixelFormat::RGB8)
                                   .ClippedRange(HistogramChannel::Green, 0.002);
    CHECK(one.low == 77 && one.high == 77, "one level everywhere: low and high meet there");
    const HistogramRange empty = ImageHistogram().ClippedRange(HistogramChannel::Blue, 0.002);
    CHECK(empty.low == 0 && empty.high == 255, "an empty histogram: the whole scale");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Histogram Model\n");
    std::printf("====================================\n");
    TestLayouts();
    TestLuminosity();
    TestStatistics();
    TestAddAndTotal();
    TestClippedRange();
    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
