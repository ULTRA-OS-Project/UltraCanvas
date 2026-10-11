// Tests/TabPillStyleScreenshotTest.cpp
// TabStyle::Pill on composited pixels: the open tab is a capsule with a one
// pixel outline in activeTabBorderColor, an inactive tab is nothing but its
// text on the bar when inactiveTabColor is transparent.
//
// It doubles as the screenshot fixture for the style: set
// ULTRACANVAS_SCREENSHOT_DIR to a directory and it writes the window there as
// tab-pill-variants.ppm, five tab bars in five colourways (an outlined pill
// on a tinted bar, a tinted pill, a solid accent pill, neutral chips, a dark
// bar), each with an open, a plain and a hovered tab - the last one caught
// mid-drag, with the capsule ghost and the insertion line - for a human to
// look at after `convert tab-pill-variants.ppm pills.png`.
//
// Runs headless under Xvfb and skips - rather than fails - without a display.
// With GDK_SCALE set the pixel checks stand down, as they are written for
// whole logical pixels; the screenshot is still written.
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef UC_MEDIA_DIR
#error "UC_MEDIA_DIR must point at the repository's media folder"
#endif

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

int Distance(const Color& a, const Color& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

Color PixelAt(const std::shared_ptr<UltraCanvasWindow>& window, int x, int y) {
    Color px;
    if (!window->GetPixelColor(x, y, px)) return Colors::Black;
    return px;
}

// The window as a binary PPM: no library needed to write it.
bool WritePpm(const std::shared_ptr<UltraCanvasWindow>& window, int width, int height, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << " " << height << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color px = PixelAt(window, x, y);
            row[static_cast<size_t>(x) * 3 + 0] = px.r;
            row[static_cast<size_t>(x) * 3 + 1] = px.g;
            row[static_cast<size_t>(x) * 3 + 2] = px.b;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(out);
}

// One colourway of the pill style.
struct Variant {
    const char* name;
    Color bar;
    Color activeFill, activeBorder, activeText;
    Color inactiveFill, inactiveText;
    Color hoverFill, hoverBorder;
    Color closeButton, activeCloseButton;
    Color content, contentDivider;
    float chipRadius;   // 0 = capsule
    bool newTabButton;
};

std::shared_ptr<UltraCanvasTabbedContainer> MakeBar(const std::string& id, int x, int y, int w, int h, const Variant& v) {
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(id, x, y, w, h);
    tabs->SetTabStyle(TabStyle::Pill);
    tabs->SetTabHeight(36);
    tabs->SetTabMaxWidth(220);
    tabs->SetTabMinWidth(60);
    tabs->SetCloseMode(TabCloseMode::Closable);
    tabs->fontSize = 12;
    tabs->SetPillInset(2, 4);
    tabs->SetPillCornerRadius(v.chipRadius);
    tabs->SetTabBarColor(v.bar);
    tabs->SetActiveTabBackgroundColor(v.activeFill);
    tabs->SetActiveTabBorderColor(v.activeBorder);
    tabs->SetActiveTabTextColor(v.activeText);
    tabs->SetInactiveTabBackgroundColor(v.inactiveFill);
    tabs->SetInactiveTabTextColor(v.inactiveText);
    tabs->SetHoveredTabBackgroundColor(v.hoverFill);
    tabs->SetHoveredTabBorderColor(v.hoverBorder);
    tabs->SetCloseButtonColor(v.closeButton);
    tabs->SetActiveTabCloseButtonColor(v.activeCloseButton);
    tabs->SetCloseButtonHoverColor(v.activeText);
    tabs->contentAreaColor = v.content;
    tabs->tabContentBorderColor = v.contentDivider;
    tabs->newTabButtonColor = Colors::Transparent;
    tabs->newTabButtonIconColor = v.inactiveText;
    tabs->newTabButtonHoverColor = v.hoverFill;
    tabs->SetNewTabButtonShape(NewTabButtonShape::Circle);
    tabs->SetShowNewTabButton(v.newTabButton);

    tabs->AddTab("Inbox");
    tabs->SetTabIcon(0, UC_MEDIA_DIR "/icons/home-icon.png");
    tabs->AddTab("UltraMail: Add/Delete account");
    tabs->SetTabIcon(1, UC_MEDIA_DIR "/icons/settings.png");
    tabs->AddTab("Drafts");
    tabs->SetTabIcon(2, UC_MEDIA_DIR "/icons/document.png");
    tabs->SetActiveTab(1);
    // The third tab is shown as the pointer would leave it: hovered.
    tabs->hoveredTabIndex = 2;
    return tabs;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Tab Pill Style Screenshot Suite"      << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TabPillStyleScreenshotTest")) SKIP_ALL("application would not initialise");

    const int kWidth = 600, kHeight = 480;
    WindowConfig cfg;
    cfg.title = "TabPillStyleScreenshotTest";
    cfg.width = kWidth;
    cfg.height = kHeight;
    cfg.resizable = false;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    const Color blue(96, 146, 224);
    const Color teal(34, 150, 130);
    const Color ink(30, 37, 46);
    const Color mutedInk(84, 96, 112);

    const Variant variants[] = {
        // 1. The reference: a white capsule with a blue outline on a blue-grey bar;
        //    the other tabs are text only until hovered.
        {"1. Outlined pill on a tinted bar", Color(229, 234, 241),
         Colors::White, blue, ink,
         Colors::Transparent, mutedInk,
         Color(255, 255, 255, 140), Colors::Transparent,
         mutedInk, Colors::Transparent, Colors::White, Color(205, 212, 222), 0.0f, true},
        // 2. A pale teal fill inside a teal outline, on a near-white bar.
        {"2. Tinted pill with accent outline", Color(250, 251, 252),
         Color(228, 244, 240), teal, Color(20, 60, 55),
         Colors::Transparent, Color(90, 96, 104),
         Color(238, 240, 243), Colors::Transparent,
         Color(110, 116, 125), Colors::Transparent, Colors::White, Color(226, 229, 234), 0.0f, false},
        // 3. The open tab in solid blue with white text; the others as grey pills.
        {"3. Solid accent pill", Color(245, 246, 248),
         Color(41, 112, 196), Colors::Transparent, Colors::White,
         Color(226, 229, 234), Color(60, 64, 72),
         Color(212, 216, 224), Colors::Transparent,
         Color(110, 116, 125), Color(225, 236, 250), Colors::White, Color(226, 229, 234), 0.0f, false},
        // 4. Neutral chips: squarer corners, a grey outline instead of an accent.
        {"4. Neutral chips (corner radius 6)", Color(252, 252, 253),
         Colors::White, Color(205, 205, 214), ink,
         Color(240, 241, 244), Color(90, 90, 100),
         Color(232, 233, 237), Colors::Transparent,
         Color(120, 120, 130), Colors::Transparent, Colors::White, Color(228, 228, 232), 6.0f, false},
        // 5. The same outline, brighter, on a dark bar.
        {"5. Dark bar", Color(30, 30, 36),
         Color(46, 46, 58), Color(120, 170, 240), Color(240, 240, 245),
         Colors::Transparent, Color(170, 170, 185),
         Color(255, 255, 255, 24), Colors::Transparent,
         Color(170, 170, 185), Colors::Transparent, Color(24, 24, 30), Color(60, 60, 72), 0.0f, false},
    };

    std::vector<std::shared_ptr<UltraCanvasUIElement>> elements;
    std::vector<std::shared_ptr<UltraCanvasTabbedContainer>> bars;
    int y = 14;
    int index = 0;
    for (const Variant& v : variants) {
        auto label = std::make_shared<UltraCanvasLabel>("Label" + std::to_string(index), 20, y, 560, 18);
        label->SetText(v.name);
        label->SetFontSize(11);
        label->SetTextColor(Color(70, 70, 80));
        elements.push_back(label);
        y += 20;
        auto bar = MakeBar("Pills" + std::to_string(index), 20, y, 560, 60, v);
        bars.push_back(bar);
        elements.push_back(bar);
        y += 72;
        ++index;
    }
    for (auto& e : elements) window->AddChild(e);

    DisplayTest::ActivateWindow(app, window);
    DisplayTest::Frame(window, elements);   // lay the bars out, so tab bounds can be read

    // The last bar is caught mid-drag: its first tab is being carried over
    // the third, so the capsule ghost and the insertion line are in the
    // picture too.
    {
        auto& dragged = bars.back();
        dragged->SetAllowTabReordering(true);
        dragged->draggingTabIndex = 0;
        dragged->isDraggingTab = true;
        dragged->dragInsertionIndex = 2;
        Rect2Di third = dragged->GetTabBounds(2);
        dragged->dragCurrentPosition = Point2Di(third.x + third.width / 3, third.y + third.height / 2);
    }

    for (int frame = 0; frame < 3; ++frame) DisplayTest::Frame(window, elements);

    if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        std::string path = std::string(shotDir) + "/tab-pill-variants.ppm";
        std::cerr << (WritePpm(window, kWidth, kHeight, path) ? "   wrote " : "   could not write ") << path << std::endl;
    }

    const bool scaled = std::getenv("GDK_SCALE") != nullptr || std::getenv("QT_SCALE_FACTOR") != nullptr;
    if (scaled) {
        std::cerr << "   device scale overridden: pixel checks skipped" << std::endl;
        return 0;
    }

    // The checks read the first bar: the reference colourway.
    auto& bar = bars[0];
    const Variant& v = variants[0];
    Rect2Df origin = bar->GetBoundsInWindow();
    auto pillInWindow = [&](int tab) {
        Rect2Di p = bar->GetPillBounds(tab);
        return Rect2Di(static_cast<int>(origin.x) + p.x, static_cast<int>(origin.y) + p.y, p.width, p.height);
    };
    const int tolerance = 60;   // an antialiased edge pixel is never the pure colour

    Rect2Di open = pillInWindow(1);
    TEST("the open pill is inset in its slot",
         open.width > 0 && open.width == bar->GetTabBounds(1).width - 4 && open.height == 36 - 8);
    Color leftEdge = PixelAt(window, open.x, open.y + open.height / 2);
    TEST("the open pill's left edge is the accent outline", Distance(leftEdge, v.activeBorder) < tolerance);
    Color topEdge = PixelAt(window, open.x + open.width / 2, open.y);
    TEST("the open pill's top edge is the accent outline", Distance(topEdge, v.activeBorder) < tolerance);
    Color inside = PixelAt(window, open.x + open.width / 2, open.y + 3);
    TEST("inside the outline the open pill is white", Distance(inside, v.activeFill) < 20);
    Color outside = PixelAt(window, open.x + open.width / 2, open.y - 2);
    TEST("above the pill the bar shows through", Distance(outside, v.bar) < 20);
    Color outlineIsOnePixel = PixelAt(window, open.x + open.width / 2, open.y + 1);
    TEST("the outline is one pixel wide", Distance(outlineIsOnePixel, v.activeFill) < tolerance);

    Rect2Di plain = pillInWindow(0);
    Color plainLeft = PixelAt(window, plain.x, plain.y + plain.height / 2);
    Color plainTop = PixelAt(window, plain.x + plain.width / 2, plain.y);
    TEST("an inactive tab has no outline on the left", Distance(plainLeft, v.bar) < 20);
    TEST("an inactive tab has no outline on top", Distance(plainTop, v.bar) < 20);

    std::cerr << "----------------------------------------" << std::endl;
    std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
