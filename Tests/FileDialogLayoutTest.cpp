// Tests/FileDialogLayoutTest.cpp
// The file dialog's toolbar glyphs sit in the middle of their buttons, and its
// listing comes without the Filer's hover icon menu.
//
// Two glyphs were off centre. The Up button was created without a label, so
// it carried UltraCanvasButton's default "Button" and was laid out as an icon
// beside text: the arrow started at the 8px left padding, 6px right of centre
// in the 28px button, and lost its right arm to the button's clip. The view
// buttons are an UltraCanvasSegmentedControl, which put an icon-only
// segment's glyph at the left padding instead of the segment's centre, 3px
// right of centre in a 30px segment.
//
// The hover icon menu (Copy / Cut / Rename / Delete on the hovered file) is a
// file manager's toolbar; a picker turns it off unless the caller asks for it
// (FileDialogConfig::hoverIconMenu).
//
// Reads the composited pixels back, so it runs under Xvfb and skips - rather
// than fails - without a display.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSegmentedControl.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

// The glyphs are drawn black; the button faces, the selected segment's blue
// and the borders are all lighter than this.
bool IsInk(const Color& px) {
    return px.r < 100 && px.g < 100 && px.b < 100;
}

struct InkBox {
    bool found = false;
    int left = 0, top = 0, right = 0, bottom = 0;   // inclusive
    float CenterX() const { return (left + right) / 2.0f; }
    float CenterY() const { return (top + bottom) / 2.0f; }
};

// The dark pixels inside `area` (window coordinates).
InkBox FindInk(UltraCanvasWindow* window, int x0, int y0, int x1, int y1) {
    InkBox box;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px) || !IsInk(px)) continue;
            if (!box.found) {
                box = {true, x, y, x, y};
            } else {
                box.left = std::min(box.left, x);
                box.right = std::max(box.right, x);
                box.top = std::min(box.top, y);
                box.bottom = std::max(box.bottom, y);
            }
        }
    }
    return box;
}

void Report(const char* what, float glyph, float cell) {
    std::cerr << "      " << what << ": glyph centre " << glyph
              << ", cell centre " << cell << std::endl;
}

// The UltraCanvas settings folder (UltraCanvasFileDialogSettings.h), moved
// into `root` so the test never touches a real installation's FileDialog.conf.
void RedirectSettingsFolder(const std::filesystem::path& root) {
    const std::string path = PathToUtf8(root);
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("APPDATA", path.c_str());
#elif defined(__APPLE__)
    setenv("HOME", path.c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", path.c_str(), 1);
#endif
}

// The window as a binary PPM, for a human to look at.
void WritePpm(UltraCanvasWindow* window, int width, int height, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
            out.put(static_cast<char>(px.r)).put(static_cast<char>(px.g)).put(static_cast<char>(px.b));
        }
    }
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   File Dialog Layout Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    // The dialog remembers its view and size in the settings folder: keep
    // the test's copy away from the user's, and start from the defaults.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path(ec) / "FileDialogLayoutTest";
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "config", ec);
    fs::create_directories(scratch / "listing" / "A folder", ec);
    std::ofstream(scratch / "listing" / "picture.png") << "not really";
    RedirectSettingsFolder(scratch / "config");

    UltraCanvasApplication app;
    if (!app.Initialize("FileDialogLayoutTest")) SKIP_ALL("application would not initialise");
    UltraCanvasDialogManager::SetUseNativeDialogs(false);

    FileDialogConfig config;
    config.title = "File dialog layout";
    config.dialogType = FileDialogType::Open;
    config.initialDirectory = PathToUtf8(scratch / "listing");
    auto dialog = UltraCanvasDialogManager::CreateFileDialog(config);
    if (!dialog) SKIP_ALL("the file dialog could not be created");
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
    for (int frame = 0; frame < 4; ++frame) dialog->UpdateAndRender();

    std::cerr << "\n--- Up button ---" << std::endl;
    {
        auto* up = dynamic_cast<UltraCanvasButton*>(dialog->FindChildById("FileDialogUp"));
        TEST("The dialog has its Up button", up != nullptr);
        if (up) {
            const Rect2Df r = up->GetBoundsInWindow();
            // Inside the border and the focus ring.
            const int x0 = static_cast<int>(std::lround(r.x)) + 2;
            const int y0 = static_cast<int>(std::lround(r.y)) + 2;
            const int x1 = static_cast<int>(std::lround(r.x + r.width)) - 2;
            const int y1 = static_cast<int>(std::lround(r.y + r.height)) - 2;
            const InkBox ink = FindInk(dialog.get(), x0, y0, x1, y1);
            TEST("The arrow is drawn", ink.found);
            if (ink.found) {
                const float cx = r.x + r.width / 2.0f - 0.5f;
                const float cy = r.y + r.height / 2.0f - 0.5f;
                Report("horizontal", ink.CenterX(), cx);
                Report("vertical", ink.CenterY(), cy);
                TEST("The arrow is centred across the button", std::fabs(ink.CenterX() - cx) <= 1.0f);
                TEST("The arrow is centred down the button", std::fabs(ink.CenterY() - cy) <= 1.0f);
                // The arrow is 16px wide (24px icon, ink from 4 to 20): none
                // of it is cut off by the button's edge.
                TEST("The arrow is drawn whole", ink.right - ink.left + 1 >= 15);
            }
        }
    }

    std::cerr << "\n--- View buttons ---" << std::endl;
    {
        auto* views = dynamic_cast<UltraCanvasSegmentedControl*>(dialog->FindChildById("FileDialogView"));
        TEST("The dialog has its view buttons", views != nullptr);
        if (views) {
            const Rect2Df r = views->GetBoundsInWindow();
            const int y0 = static_cast<int>(std::lround(r.y)) + 2;
            const int y1 = static_cast<int>(std::lround(r.y + r.height)) - 2;
            const int count = views->GetSegmentCount();
            TEST("There are six views", count == 6);
            TEST("The views share the width equally",
                 views->GetWidthMode() == SegmentWidthMode::Equal);
            // Equal segments: the width shared out in whole pixels after the
            // spacing, starting inside the control's left border.
            const SegmentedControlStyle st = views->GetStyle();
            const int spacing = st.segmentSpacing;
            const int segmentWidth = count > 0
                    ? (static_cast<int>(r.width) - spacing * (count - 1)) / count : 0;
            for (int i = 0; i < count; ++i) {
                const int first = static_cast<int>(st.borderWidth) + i * (segmentWidth + spacing);
                const int last = first + segmentWidth - 1;
                const int left = static_cast<int>(std::lround(r.x)) + first;
                const int right = static_cast<int>(std::lround(r.x)) + last;
                // One pixel in from each side: the separators are not ink.
                const InkBox ink = FindInk(dialog.get(), left + 1, y0, right, y1);
                const std::string name = "View " + std::to_string(i + 1);
                TEST(name + ": its glyph is drawn", ink.found);
                if (!ink.found) continue;
                const float cx = (left + right) / 2.0f;
                const float cy = r.y + r.height / 2.0f - 0.5f;
                Report("horizontal", ink.CenterX(), cx);
                TEST(name + ": its glyph is centred across the segment",
                     std::fabs(ink.CenterX() - cx) <= 1.0f);
                TEST(name + ": its glyph is centred down the segment",
                     std::fabs(ink.CenterY() - cy) <= 1.0f);
            }
        }
    }

    std::cerr << "\n--- Listing ---" << std::endl;
    {
        auto* listing = dynamic_cast<UltraCanvasFilerWidget*>(dialog->FindChildById("FileDialogListing"));
        TEST("The dialog has its listing", listing != nullptr);
        if (listing) {
            TEST("The listing has no hover icon menu", !listing->IsHoverIconMenuEnabled());
        }
    }
    {
        FileDialogConfig withMenu = config;
        withMenu.hoverIconMenu = true;
        auto other = UltraCanvasDialogManager::CreateFileDialog(withMenu);
        auto* listing = other ? dynamic_cast<UltraCanvasFilerWidget*>(other->FindChildById("FileDialogListing"))
                              : nullptr;
        TEST("A caller that asks for the hover icon menu gets it",
             listing && listing->IsHoverIconMenuEnabled());
    }

    if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        WritePpm(dialog.get(), static_cast<int>(dialog->GetWidth()), static_cast<int>(dialog->GetHeight()),
                 std::string(shotDir) + "/file-dialog.ppm");
    }

    dialog->CloseDialog(DialogResult::Cancel);
    fs::remove_all(scratch, ec);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
