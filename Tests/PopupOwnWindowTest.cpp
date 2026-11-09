// Tests/PopupOwnWindowTest.cpp
// Popups in windows of their own (WindowType::Popup).
//
// A popup - a menu, a dropdown's list, anything opened with OpenPopup - was
// composited onto the window it belongs to, so a window above that one covered
// it: every menu of UltraDesktop's bars, which live in a window kept below all
// others, went under the applications' windows. These tests open a window,
// cover it with a second one, open a menu in the first and read the screen
// back: the menu must be on top, cut to its rounded outline, take clicks in
// its own window as clicks in the window it belongs to, close on a click
// outside it and when focus leaves the application, and go with its window,
// so it never keeps the application running.
//
// X11 only (it reads the X server's stacking order, shapes and the screen's
// pixels); runs headless under Xvfb. Skips - rather than fails - without a
// display.
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(ULTRACANVAS_DISPLAY_WAYLAND)
#define POPUP_TEST_X11 1
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

#ifdef POPUP_TEST_X11
namespace {

// The application, with the step of its loop that runs what was posted to
// the UI thread in reach of the test: a popup's window closes there.
class TestApplication : public UltraCanvasApplication {
public:
    using UltraCanvasApplication::ProcessPostedTasks;
};

// Colours nothing else on the screen has: the window the menu belongs to, the
// window over it, and the menu's panel.
const Color kTargetColor(200, 40, 40);
const Color kCoverColor(40, 40, 200);
const Color kMenuColor(30, 160, 60);

bool NearlyEqual(const Color& a, const Color& b, int tolerance = 24) {
    auto close = [tolerance](uint8_t l, uint8_t r) {
        return std::abs(static_cast<int>(l) - static_cast<int>(r)) <= tolerance;
    };
    return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b);
}

// A pixel of the screen as the X server shows it - whichever window is on
// top there.
Color ScreenPixel(Display* display, int x, int y) {
    Color out(0, 0, 0);
    XImage* image = XGetImage(display, DefaultRootWindow(display), x, y, 1, 1, AllPlanes, ZPixmap);
    if (!image) return out;
    const unsigned long pixel = XGetPixel(image, 0, 0);
    out = Color(static_cast<uint8_t>((pixel & image->red_mask) >> 16),
                static_cast<uint8_t>((pixel & image->green_mask) >> 8),
                static_cast<uint8_t>(pixel & image->blue_mask));
    XDestroyImage(image);
    return out;
}

// Position of a window in the root window's stacking order (higher is
// nearer the top); -1 when it is not a mapped child of the root.
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

int MapState(Display* display, Window window) {
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, window, &attributes)) return -1;
    return attributes.map_state;
}

// What the loop does between events, without the events: the tasks posted
// to the UI thread, then a frame of every window - the windows of the popups
// among them - with the X server caught up before and after.
void Frames(TestApplication& app, int count = 3) {
    for (int round = 0; round < count; ++round) {
        XSync(app.GetDisplay(), False);
        app.ProcessPostedTasks();
        const auto windows = app.GetWindows();
        for (const auto& window : windows) {
            if (!window || !window->IsCreated()) continue;
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

// A pointer event as the X11 backend makes it, for `window`, at `local`
// (that window's coordinates)
void Pointer(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindowBase>& window,
             UCEventType type, const Point2Di& local) {
    UCEvent event;
    event.type = type;
    event.button = (type == UCEventType::MouseMove) ? UCMouseButton::NoneButton : UCMouseButton::Left;
    event.pointerWindow = local;
    event.pointer = local;
    event.targetWindow = window;
    event.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(event);
}

void Click(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindowBase>& window, const Point2Di& local) {
    Pointer(app, window, UCEventType::MouseMove, local);
    Pointer(app, window, UCEventType::MouseDown, local);
    Pointer(app, window, UCEventType::MouseUp, local);
}

std::shared_ptr<UltraCanvasWindowBase> Shared(UltraCanvasApplication& app, UltraCanvasWindowBase* window) {
    for (const auto& w : app.GetWindows()) {
        if (w.get() == window) return w;
    }
    return nullptr;
}

} // namespace
#endif

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Popup Own Window Suite"               << std::endl;
    std::cerr << "========================================" << std::endl;

#ifndef POPUP_TEST_X11
    SKIP_ALL("the checks read the X server's stacking order, shapes and pixels");
#else
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    TestApplication app;
    if (!app.Initialize("PopupOwnWindowTest")) SKIP_ALL("application would not initialise");
    Display* display = app.GetDisplay();

    TEST("popups have windows of their own by default", UltraCanvasWindowBase::GetPopupsInOwnWindows());

    // The window the menu belongs to, and one over it that covers the place
    // the menu opens at - as an application's window covers the desktop's
    // bars and what opens from them.
    auto target = OpenWindow("Target", 100, 100, 400, 300, kTargetColor);
    if (!target) SKIP_ALL("window could not be created");
    auto cover = OpenWindow("Cover", 120, 120, 300, 260, kCoverColor);
    if (!cover) SKIP_ALL("second window could not be created");
    XRaiseWindow(display, static_cast<Window>(cover->GetNativeHandle()));
    Frames(app);

    bool firstRan = false, secondRan = false;
    auto menu = std::make_shared<UltraCanvasMenu>("TestMenu", 0, 0, 180, 0);
    menu->SetMenuType(MenuType::PopupMenu);
    MenuStyle style = menu->GetStyle();
    style.backgroundColor = kMenuColor;
    style.borderRadius = 8;
    style.enableAnimations = false;
    menu->SetStyle(style);
    menu->AddItem(MenuItemData::Action("First", [&firstRan]() { firstRan = true; }));
    menu->AddItem(MenuItemData::Action("Second", [&secondRan]() { secondRan = true; }));

    PopupElementSettings settings;
    menu->OpenMenu(Point2Di(60, 40), *target, settings);
    Frames(app);

    UltraCanvasWindowBase* menuWindow = target->GetPopupWindow(*menu);
    TEST("the menu is shown in a window of its own", menuWindow != nullptr);
    if (!menuWindow) {
        std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
        return 1;
    }
    auto menuWindowPtr = Shared(app, menuWindow);
    TEST("that window is a popup's window", menuWindow->GetConfig().type == WindowType::Popup);
    TEST("it knows the window and the popup it shows",
         menuWindow->IsPopupHost() && menuWindow->GetPopupHostOwner() == target.get() &&
         menuWindow->GetHostedPopup() == menu.get());

    const Window menuX = static_cast<Window>(menuWindow->GetNativeHandle());
    XWindowAttributes attributes;
    TEST("it is override-redirect, out of the window manager's hands",
         XGetWindowAttributes(display, menuX, &attributes) && attributes.override_redirect == True);
    TEST("it is mapped", attributes.map_state == IsViewable);
    TEST("it is above the window that covers the window the menu belongs to",
         StackIndex(display, menuX) > StackIndex(display, static_cast<Window>(cover->GetNativeHandle())));

    int originX = 0, originY = 0;
    target->GetContentScreenOrigin(originX, originY);
    const Point2Df menuPos = menu->GetPositionInWindow();
    const Size2Di menuSize = menu->GetSize();
    int menuWindowX = 0, menuWindowY = 0;
    menuWindow->GetWindowPosition(menuWindowX, menuWindowY);
    std::cerr << "  menu at " << menuPos.x << "," << menuPos.y << " " << menuSize.width << "x" << menuSize.height
              << " in its window; its own window at " << menuWindowX << "," << menuWindowY << std::endl;
    TEST("its window is where the menu is in the window it belongs to",
         menuWindowX == originX + static_cast<int>(menuPos.x) && menuWindowY == originY + static_cast<int>(menuPos.y));
    int width = 0, height = 0;
    menuWindow->GetWindowSize(width, height);
    TEST("its window is the menu's size", width == menuSize.width && height == menuSize.height);

    // Inside the panel, right of the labels: the menu, not the window over it
    const Color inside = ScreenPixel(display, menuWindowX + menuSize.width - 6, menuWindowY + menuSize.height / 2);
    std::cerr << "  screen inside the menu: " << int(inside.r) << "," << int(inside.g) << "," << int(inside.b) << std::endl;
    TEST("the screen shows the menu, not the window over its own", NearlyEqual(inside, kMenuColor));

    // The rounded corner: the window is cut to the panel's outline
    int shapeEvent = 0, shapeError = 0;
    if (XShapeQueryExtension(display, &shapeEvent, &shapeError)) {
        int count = 0, ordering = 0;
        XRectangle* rects = XShapeGetRectangles(display, menuX, ShapeBounding, &count, &ordering);
        TEST("its outline follows the rounded panel (a shape of several rectangles)", count > 1);
        if (rects) XFree(rects);
        const Color corner = ScreenPixel(display, menuWindowX, menuWindowY);
        std::cerr << "  screen at the menu's corner: " << int(corner.r) << "," << int(corner.g) << ","
                  << int(corner.b) << std::endl;
        TEST("its corner shows the window beneath, not black", NearlyEqual(corner, kCoverColor));
    }

    TEST("the window it belongs to does not composite it", !NearlyEqual([&]() {
        Color c;
        target->GetPixelColor(static_cast<int>(menuPos.x) + menuSize.width - 6,
                              static_cast<int>(menuPos.y) + menuSize.height / 2, c);
        return c;
    }(), kMenuColor));

    // Hidden without being closed, its window goes too, and comes back
    menu->SetVisible(false);
    Frames(app);
    TEST("a popup hidden while open hides its window", MapState(display, menuX) != IsViewable);
    menu->SetVisible(true);
    menu->RequestRedraw();
    Frames(app);
    TEST("shown again, so is its window", MapState(display, menuX) == IsViewable);

    // A click in its own window is a click on the menu: the second item
    const int secondRowY = style.paddingTop + style.itemHeight + style.itemHeight / 2;
    Click(app, menuWindowPtr, Point2Di(menuSize.width / 2, secondRowY));
    Frames(app);
    TEST("a click in the menu's window runs the item under it", secondRan && !firstRan);
    TEST("the item closed the menu", !menu->IsVisible());
    TEST("closed, its window is hidden at once",
         MapState(display, menuX) != IsViewable);
    TEST("and closed on the next turn of the loop",
         menuWindowPtr && menuWindowPtr->GetState() == WindowState::Closed);

    // A click outside it, in the window it belongs to, closes it
    menu->OpenMenu(Point2Di(60, 40), *target, settings);
    Frames(app);
    TEST("opened again: in a new window of its own", target->GetPopupWindow(*menu) != nullptr);
    Click(app, target, Point2Di(380, 280));
    Frames(app);
    TEST("a click outside it closes it", !menu->IsVisible() && target->GetPopupWindow(*menu) == nullptr);

    // Focus leaving the application closes it: it floats above every window
    menu->OpenMenu(Point2Di(60, 40), *target, settings);
    Frames(app);
    {
        UCEvent focus;
        focus.type = UCEventType::WindowFocus;
        focus.targetWindow = target;
        focus.nativeWindowHandle = target->GetNativeHandle();
        app.DispatchEvent(focus);
        UCEvent blur = focus;
        blur.type = UCEventType::WindowBlur;
        app.DispatchEvent(blur);
    }
    Frames(app);
    TEST("focus leaving the application closes it", !menu->IsVisible());

    // A popup that reaches past the window's edge takes the pointer there too
    bool buttonClicked = false;
    auto button = CreateButton("EdgeButton", 0, 0, 160, 32, "Past the edge");
    button->SetOnClick([&buttonClicked]() { buttonClicked = true; });
    PopupElementSettings buttonSettings;
    target->OpenPopup(Point2Di(300, 200), *button, buttonSettings);
    Frames(app);
    UltraCanvasWindowBase* buttonWindow = target->GetPopupWindow(*button);
    TEST("a popup past the window's edge has its own window", buttonWindow != nullptr);
    if (buttonWindow) {
        // 150 px into the button: 50 px past the window's right edge
        Click(app, Shared(app, buttonWindow), Point2Di(150, 16));
        TEST("a click on it past the window's edge reaches it", buttonClicked);
    }
    target->ClosePopup(*button);
    Frames(app);

    // Turned off, popups are composited onto their window, as before
    UltraCanvasWindowBase::SetPopupsInOwnWindows(false);
    menu->OpenMenu(Point2Di(60, 40), *target, settings);
    Frames(app);
    TEST("turned off: no window of its own", target->GetPopupWindow(*menu) == nullptr);
    Color composited;
    target->GetPixelColor(static_cast<int>(menuPos.x) + menuSize.width - 6,
                          static_cast<int>(menuPos.y) + menuSize.height / 2, composited);
    TEST("turned off: the window composites it", NearlyEqual(composited, kMenuColor));
    menu->CloseMenu();
    UltraCanvasWindowBase::SetPopupsInOwnWindows(true);
    Frames(app);

    // A popup's window is no application window: with the other window
    // closed, closing the one the menu belongs to ends the application
    menu->OpenMenu(Point2Di(60, 40), *target, settings);
    Frames(app);
    auto lastWindow = Shared(app, target->GetPopupWindow(*menu));
    TEST("open once more", lastWindow != nullptr);
    cover->Close();
    TEST("closing the last window ends the application, its popup's window regardless",
         app.ClosingEndsApplication(target.get()));
    target->Close();
    TEST("closing the window closes its popup's window",
         lastWindow && lastWindow->GetState() == WindowState::Closed);
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
