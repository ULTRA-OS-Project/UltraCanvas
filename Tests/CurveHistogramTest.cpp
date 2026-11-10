// Tests/CurveHistogramTest.cpp
// The Curves dialog's histogram backdrop filled from one count of a picture
// (UltraCanvasCurveEditor::SetHistograms): Red, Green and Blue come from
// their channels, RGB is the three added level by level, transparent pixels
// are left out, and an empty count removes every backdrop. The image viewer
// and UltraPaint both fill their Curves dialog this way.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasCurveEditor.h"
#include "UltraCanvasHistogramModel.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// Three opaque pixels, (200, 60, 30) twice and (30, 60, 200) once, and one
// fully transparent pixel of (90, 90, 90): straight RGBA8, as an UltraPaint
// layer holds it.
std::vector<uint8_t> Pixels() {
    return {200, 60, 30, 255,   200, 60, 30, 255,   30, 60, 200, 255,   90, 90, 90, 0};
}

void TestBackdrops() {
    std::cout << "\nSetHistograms\n";
    UltraCanvasCurveEditor editor("curves", 0, 0, 256, 256);
    const std::vector<uint8_t> px = Pixels();
    editor.SetHistograms(ComputeImageHistogram(px.data(), 4, 1, 16, HistogramPixelFormat::RGBA8));

    const std::vector<uint32_t>& red = editor.GetHistogram(ToneCurveChannel::Red);
    const std::vector<uint32_t>& green = editor.GetHistogram(ToneCurveChannel::Green);
    const std::vector<uint32_t>& blue = editor.GetHistogram(ToneCurveChannel::Blue);
    const std::vector<uint32_t>& rgb = editor.GetHistogram(ToneCurveChannel::RGB);
    Check(red.size() == 256 && green.size() == 256 && blue.size() == 256 && rgb.size() == 256,
          "every channel gets 256 levels");
    Check(red[200] == 2 && red[30] == 1 && green[60] == 3 && blue[30] == 2 && blue[200] == 1,
          "Red, Green and Blue count their own channel");
    Check(rgb[200] == 3 && rgb[30] == 3 && rgb[60] == 3,
          "RGB adds the three channels level by level");
    Check(red[90] == 0 && rgb[90] == 0, "the transparent pixel is left out");

    editor.SetHistograms(ImageHistogram());
    Check(editor.GetHistogram(ToneCurveChannel::RGB).empty() && editor.GetHistogram(ToneCurveChannel::Red).empty(),
          "an empty count removes every backdrop");

    // The per-channel call still works beside it.
    editor.SetHistogram(ToneCurveChannel::Green, std::vector<uint32_t>(128, 1));
    Check(editor.GetHistogram(ToneCurveChannel::Green).size() == 256 &&
              editor.GetHistogram(ToneCurveChannel::Green)[0] == 1 &&
              editor.GetHistogram(ToneCurveChannel::Green)[1] == 0,
          "SetHistogram resamples other bin counts into 256 levels, as before");
}

} // namespace

int main() {
    std::cout << "UltraCanvas Curves histogram backdrop\n";
    TestBackdrops();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed." : "Some checks FAILED.") << "\n";
    return g_failures == 0 ? 0 : 1;
}
