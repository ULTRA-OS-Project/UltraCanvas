// Tests/UltraSnipCaptureTest.cpp
// UltraSnip's arithmetic and pixels without a display: the rectangle from a
// drag's two corners, a rectangle clipped to the screen, the crop of a BGRx
// screen capture (with an origin, as a Windows virtual screen has) into
// straight RGBA, the topmost window under a point, where the recording bar
// goes, the elapsed-time words, and a file written whole under its UTF-8 name.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../Apps/UltraSnip/ui/UltraSnipCapture.h"

#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraSnip;

namespace {
int g_failures = 0;

void Check(bool condition, const char* what) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++g_failures;
}
} // namespace

int main() {
    // ---- the rectangle from a drag ----
    {
        const Rect2Di r = RectBetween(Point2Di(10, 20), Point2Di(5, 30));
        Check(r.x == 5 && r.y == 20 && r.width == 6 && r.height == 11, "RectBetween: either way round, both corners inside");
        const Rect2Di one = RectBetween(Point2Di(7, 7), Point2Di(7, 7));
        Check(one.width == 1 && one.height == 1, "RectBetween: a click is one pixel");
    }
    // ---- clipping ----
    {
        const Rect2Di screen(0, 0, 100, 50);
        const Rect2Di in = ClipRect(Rect2Di(-10, 40, 30, 30), screen);
        Check(in.x == 0 && in.y == 40 && in.width == 20 && in.height == 10, "ClipRect: cut to the screen");
        const Rect2Di off = ClipRect(Rect2Di(200, 200, 10, 10), screen);
        Check(off.width == 0 && off.height == 0, "ClipRect: off the screen is empty");
    }
    // ---- the crop of a capture ----
    {
        DesktopScreenImage shot;
        shot.x = -100;              // a monitor left of the primary
        shot.y = 0;
        shot.width = 8;
        shot.height = 4;
        shot.stride = 8 * 4 + 16;   // padded rows
        shot.pixels.assign(static_cast<size_t>(shot.stride) * shot.height, 0);
        for (int y = 0; y < shot.height; ++y)
            for (int x = 0; x < shot.width; ++x) {
                uint8_t* p = shot.pixels.data() + y * shot.stride + x * 4;
                p[0] = static_cast<uint8_t>(10 + x);   // B
                p[1] = static_cast<uint8_t>(100 + y);  // G
                p[2] = 200;                            // R
                p[3] = 0;                              // unused
            }
        ClipboardDib::RgbaImage out;
        Rect2Di taken;
        Check(CropToRgba(shot, Rect2Di(-98, 1, 3, 2), out, &taken), "CropToRgba: inside the shot");
        Check(out.width == 3 && out.height == 2 && taken.x == -98 && taken.y == 1, "CropToRgba: the size and place asked");
        Check(out.pixels.size() == 3 * 2 * 4, "CropToRgba: RGBA bytes");
        // the first pixel is shot (2,1): B 12, G 101, R 200
        Check(out.pixels[0] == 200 && out.pixels[1] == 101 && out.pixels[2] == 12 && out.pixels[3] == 255,
              "CropToRgba: B G R x becomes R G B A, opaque");
        // the last pixel is shot (4,2)
        const size_t last = (2 * 3 - 1) * 4;
        Check(out.pixels[last] == 200 && out.pixels[last + 1] == 102 && out.pixels[last + 2] == 14,
              "CropToRgba: the last pixel is the right one");
        Check(CropToRgba(shot, Rect2Di(-104, -2, 6, 4), out, &taken) && taken.x == -100 && taken.y == 0 &&
                  out.width == 2 && out.height == 2,
              "CropToRgba: an area over the edge is clipped");
        Check(!CropToRgba(shot, Rect2Di(50, 50, 5, 5), out), "CropToRgba: an area outside is refused");
        DesktopScreenImage empty;
        Check(!CropToRgba(empty, Rect2Di(0, 0, 5, 5), out), "CropToRgba: no shot, no crop");
    }
    // ---- the window under a point ----
    {
        std::vector<SnipWindowBounds> windows(3);
        windows[0].bounds = Rect2Di(0, 0, 100, 100);       // the bottom one
        windows[1].bounds = Rect2Di(50, 50, 100, 100);
        windows[2].bounds = Rect2Di(300, 300, 0, 0);       // gone: no size
        Check(TopmostWindowAt(windows, Point2Di(60, 60)) == 1, "TopmostWindowAt: the one on top wins");
        Check(TopmostWindowAt(windows, Point2Di(10, 10)) == 0, "TopmostWindowAt: the bottom one where it shows alone");
        Check(TopmostWindowAt(windows, Point2Di(300, 300)) == -1, "TopmostWindowAt: a sizeless window is not under anything");
        Check(TopmostWindowAt(windows, Point2Di(149, 149)) == 1 && TopmostWindowAt(windows, Point2Di(150, 150)) == -1,
              "TopmostWindowAt: the far edge is outside");
    }
    // ---- the recording bar ----
    {
        const Rect2Di screen(0, 0, 1000, 800);
        Rect2Di bar = PlaceBarOutside(Rect2Di(200, 300, 400, 200), screen, 280, 44, 12);
        Check(bar.x == 260 && bar.y == 244 && bar.width == 280 && bar.height == 44, "PlaceBarOutside: centred above with the gap");
        bar = PlaceBarOutside(Rect2Di(200, 10, 400, 200), screen, 280, 44, 12);
        Check(bar.y == 222, "PlaceBarOutside: below when there is no room above");
        bar = PlaceBarOutside(Rect2Di(0, 0, 1000, 800), screen, 280, 44, 12);
        Check(bar.y == 12 && bar.x == 360, "PlaceBarOutside: at the top when the area is the whole screen");
        bar = PlaceBarOutside(Rect2Di(-500, 300, 100, 100), screen, 280, 44, 12);
        Check(bar.x == 0, "PlaceBarOutside: never left of the screen");
        bar = PlaceBarOutside(Rect2Di(900, 300, 400, 100), screen, 280, 44, 12);
        Check(bar.x == 720, "PlaceBarOutside: never right of the screen");
    }
    // ---- words ----
    Check(FormatElapsed(0.0) == "0:00" && FormatElapsed(65.9) == "1:05" && FormatElapsed(3600 + 61) == "1:01:01",
          "FormatElapsed: m:ss and h:mm:ss");
    // ---- files ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() /
                             PathFromUtf8("ultrasnip-test-" + std::to_string(std::chrono::steady_clock::now()
                                                                                 .time_since_epoch()
                                                                                 .count()));
        const std::string file = PathToUtf8(dir / PathFromUtf8("\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\x9E \xF0\x9F\x93\xB7.bin"));
        std::string error;
        const std::vector<uint8_t> bytes = {1, 2, 3, 4, 5};
        Check(WriteFileBytes(file, bytes, error), "WriteFileBytes: written, the folder made first");
        std::error_code ec;
        Check(fs::file_size(PathFromUtf8(file), ec) == 5, "WriteFileBytes: five bytes under a Thai-and-emoji name");
        const std::string copy = PathToUtf8(dir / "copy" / "again.bin");
        Check(CopyFileTo(file, copy, error) && fs::file_size(PathFromUtf8(copy), ec) == 5, "CopyFileTo: copied into a new folder");
        Check(CopyFileTo(file, file, error), "CopyFileTo: onto itself is nothing to do");
        Check(!CopyFileTo(PathToUtf8(dir / "missing.bin"), copy, error) && !error.empty(), "CopyFileTo: a missing source says why");
        fs::remove_all(dir, ec);
    }

    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All UltraSnip capture checks passed\n");
    return 0;
}
