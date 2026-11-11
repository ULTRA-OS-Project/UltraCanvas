// Tests/TooltipOwnWindowTest.cpp
// The tooltip in a window of its own (WindowType::Tooltip).
//
// The tooltip used to be drawn into the window it belongs to, while that
// window composited its frame. A window above it covered it - every tooltip of
// UltraDesktop's bars, which live in a window kept below all others, went
// under the applications' windows - and it stopped at the window's edges.
// These tests open a window, cover it with a second one, show a tooltip of
// the first and read the screen back: the tooltip must be on top, outside the
// window it belongs to where there is room, let the pointer through, and go
// when that window closes, so it never keeps the application running.
//
// X11 only (it reads the X server's stacking order and the screen's pixels);
// runs headless under Xvfb. Skips - rather than fails - without a display.
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(ULTRACANVAS_DISPLAY_WAYLAND)
#define TOOLTIP_TEST_X11 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
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

#ifdef TOOLTIP_TEST_X11
namespace {

// Colours nothing else on the screen has: the window the tooltip belongs to,
// the window over it, and the tooltip's background.
const Color kTargetColor(200, 40, 40);
const Color kCoverColor(40, 40, 200);
const Color kTooltipColor(30, 160, 60);

bool NearlyEqual(const Color& a, const Color& b, int tolerance = 24) {
    auto close = [tolerance](uint8_t l, uint8_t r) {
        return std::abs(static_cast<int>(l) - static_cast<int>(r)) <= tolerance;
    };
    return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b);
}

// A pixel of the screen as the X server shows it - whichever window is on
// top there.
bool ScreenPixel(Display* display, int x, int y, Color& out) {
    XImage* image = XGetImage(display, DefaultRootWindow(display), x, y, 1, 1, AllPlanes, ZPixmap);
    if (!image) return false;
    const unsigned long pixel = XGetPixel(image, 0, 0);
    out = Color(static_cast<uint8_t>((pixel & image->red_mask) >> 16),
                static_cast<uint8_t>((pixel & image->green_mask) >> 8),
                static_cast<uint8_t>(pixel & image->blue_mask));
    XDestroyImage(image);
    return true;
}

// Position of a window in the root window's stacking order (higher is
// nearer the top); -1 when it is not a child of the root.
int StackIndex(Display* display, Window window) {
    Window root = 0, parent = 0;
    Window* children = nullptr;
    unsigned int count = 0;
    int index = -1;
    if (XQueryTree(display, DefaultRootWindow(display), &root, &parent, &children, &count)) {
        for (unsigned int i = 0; i < count; ++i) {
            if (children[i] == window) index = static_cast<int>(i);
        }
        if (children) XFree(children);
    }
    return index;
}

// The X server has every request, and every window has drawn its frame.
void Settle(UltraCanvasApplication& app, const std::vector<UltraCanvasWindowBase*>& windows) {
    for (int round = 0; round < 3; ++round) {
        XSync(app.GetDisplay(), False);
        for (auto* window : windows) {
            if (!window) continue;
            window->RequestRedraw();
            window->RequestWindowComposition();
            window->UpdateAndRender();
        }
        XSync(app.GetDisplay(), False);
    }
}

std::shared_ptr<UltraCanvasWindow> OpenWindow(const std::string& title, int x, int y, int width, int height,
                                              const Color& color) {
    WindowConfig config;
    config.title = title;
    config.type = WindowType::Borderless;
    config.x = x;
    config.y = y;
    config.width = width;
    config.height = height;
    config.backgroundColor = color;
    auto window = CreateWindow(config);
    if (!window || !window->IsCreated()) return nullptr;
    window->SetBackgroundColor(color);
    window->Show();
    return window;
}

} // namespace
#endif

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Tooltip Own Window Suite"             << std::endl;
    std::cerr << "========================================" << std::endl;

#ifndef TOOLTIP_TEST_X11
    SKIP_ALL("the checks read the X server's stacking order and pixels");
#else
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TooltipOwnWindowTest")) SKIP_ALL("application would not initialise");
    Display* display = app.GetDisplay();

    TEST("a tooltip has a window of its own by default", UltraCanvasTooltipManager::GetUseOwnWindow());

    // The window the tooltip belongs to, and one over it that covers the
    // place above it where the tooltip goes - as an application's window
    // covers the desktop above its bars.
    auto target = OpenWindow("Target", 100, 300, 400, 200, kTargetColor);
    if (!target) SKIP_ALL("window could not be created");
    auto cover = OpenWindow("Cover", 100, 100, 400, 200, kCoverColor);
    if (!cover) SKIP_ALL("second window could not be created");
    XRaiseWindow(display, static_cast<Window>(cover->GetNativeHandle()));
    Settle(app, {target.get(), cover.get()});

    // Near the top of the window: inside it, the tooltip had to go below the
    // pointer; with the monitor's room it goes above, over the other window.
    TooltipStyle style = UltraCanvasTooltipManager::GetStyle();
    style.backgroundColor = kTooltipColor;
    style.borderWidth = 0;
    const Point2Di pointer(60, 10);
    UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(target.get(), "A tooltip that must be seen",
                                                               pointer, style);
    UltraCanvasWindowBase* tooltipWindow = UltraCanvasTooltipManager::GetTooltipWindow();
    TEST("the tooltip is shown in a window of its own", tooltipWindow != nullptr);
    if (!tooltipWindow) {
        std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
        return 1;
    }
    TEST("that window is a tooltip window", tooltipWindow->GetConfig().type == WindowType::Tooltip);
    TEST("the window it belongs to does not draw it",
         UltraCanvasTooltipManager::Render(target.get()) == nullptr);
    Settle(app, {target.get(), cover.get(), tooltipWindow});

    const Window tooltipX = static_cast<Window>(tooltipWindow->GetNativeHandle());
    XWindowAttributes attributes;
    TEST("it is override-redirect, out of the window manager's hands",
         XGetWindowAttributes(display, tooltipX, &attributes) && attributes.override_redirect == True);
    TEST("it is mapped", attributes.map_state == IsViewable);

    int shapeEvent = 0, shapeError = 0;
    if (XShapeQueryExtension(display, &shapeEvent, &shapeError)) {
        int count = -1, ordering = 0;
        XRectangle* rects = XShapeGetRectangles(display, tooltipX, ShapeInput, &count, &ordering);
        TEST("the pointer passes through it (empty input shape)", count == 0);
        if (rects) XFree(rects);
    }

    TEST("it is above the window that covers the window it belongs to",
         StackIndex(display, tooltipX) > StackIndex(display, static_cast<Window>(cover->GetNativeHandle())));

    int tooltipX0 = 0, tooltipY0 = 0;
    tooltipWindow->GetWindowPosition(tooltipX0, tooltipY0);
    int tooltipW = 0, tooltipH = 0;
    tooltipWindow->GetNativeWindowSize(tooltipW, tooltipH);
    int targetX = 0, targetY = 0;
    target->GetContentScreenOrigin(targetX, targetY);
    std::cerr << "  tooltip at " << tooltipX0 << "," << tooltipY0 << " " << tooltipW << "x" << tooltipH
              << ", its window at " << targetX << "," << targetY << std::endl;
    TEST("it is above the pointer, outside the window it belongs to", tooltipY0 + tooltipH <= targetY);
    TEST("it is right of the pointer", tooltipX0 > targetX + pointer.x);

    // Inside the left padding, clear of the text
    Color seen;
    const bool read = ScreenPixel(display, tooltipX0 + 3, tooltipY0 + tooltipH / 2, seen);
    std::cerr << "  screen under the tooltip: " << int(seen.r) << "," << int(seen.g) << "," << int(seen.b)
              << std::endl;
    TEST("the screen shows the tooltip, not the window over its own",
         read && NearlyEqual(seen, kTooltipColor) && !NearlyEqual(seen, kCoverColor));

    // A move of the pointer moves the window
    UltraCanvasTooltipManager::UpdateTooltipPosition(Point2Di(pointer.x + 40, pointer.y));
    XSync(display, False);
    int movedX = 0, movedY = 0;
    tooltipWindow->GetWindowPosition(movedX, movedY);
    TEST("the window follows the pointer", movedX == tooltipX0 + 40 && movedY == tooltipY0);

    // Hidden, the window is unmapped and kept for the next tooltip
    UltraCanvasTooltipManager::HideTooltipImmediately();
    XSync(display, False);
    TEST("hidden: no tooltip window is reported", UltraCanvasTooltipManager::GetTooltipWindow() == nullptr);
    TEST("hidden: the window is unmapped",
         XGetWindowAttributes(display, tooltipX, &attributes) && attributes.map_state == IsUnmapped);
    UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(target.get(), "Shown again", pointer, style);
    TEST("shown again: in the same window", UltraCanvasTooltipManager::GetTooltipWindow() == tooltipWindow);

    // Turned off, it is drawn inside the window it belongs to, as before
    UltraCanvasTooltipManager::SetUseOwnWindow(false);
    UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(target.get(), "Inside", pointer, style);
    TEST("turned off: no window of its own", UltraCanvasTooltipManager::GetTooltipWindow() == nullptr);
    TEST("turned off: the window it belongs to draws it",
         UltraCanvasTooltipManager::Render(target.get()) != nullptr);
    UltraCanvasTooltipManager::HideTooltipImmediately();
    UltraCanvasTooltipManager::SetUseOwnWindow(true);

    // The tooltip's window is no application window: with the other window
    // closed, closing the one the tooltip belongs to ends the application
    UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(target.get(), "Last", pointer, style);
    std::shared_ptr<UltraCanvasWindowBase> kept;
    for (const auto& window : app.GetWindows()) {
        if (window.get() == UltraCanvasTooltipManager::GetTooltipWindow()) kept = window;
    }
    TEST("the tooltip window is shown again", kept != nullptr);
    cover->Close();
    TEST("closing the last window ends the application, its tooltip's window regardless",
         app.ClosingEndsApplication(target.get()));

    // And it goes with that window
    target->Close();
    TEST("closing the window closes the tooltip's own window",
         kept && kept->GetState() == WindowState::Closed);
    TEST("no tooltip is left", !UltraCanvasTooltipManager::IsVisible() &&
                               UltraCanvasTooltipManager::GetTooltipWindow() == nullptr);
    bool liveLeft = false;
    for (const auto& window : app.GetWindows()) {
        if (window && window->GetState() != WindowState::Closed) liveLeft = true;
    }
    TEST("no window is left open to keep Run() going", !liveLeft);

    std::cerr << "========================================" << std::endl;
    std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
#endif
}
