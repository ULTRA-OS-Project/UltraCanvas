// Tests/WindowShutdownTest.cpp
// What happens to a window that is never closed, only destroyed.
//
// Two things used to go wrong there, and both are guarded here.
//
// 1. The application died first. `windows` is a member of
//    UltraCanvasApplicationBase, so the windows were destroyed by the BASE
//    destructor - which runs after the platform half of the application has
//    gone and cleared its instance pointer. ~UltraCanvasWindowBase closes the
//    window's popups, closing a popup hides its own window, and hiding a
//    window on X11 asks the application for the display: the process died
//    with SIGSEGV on its way out, in every UltraCanvas application on every
//    platform. UltraCanvasApplicationBase::DestroyWindows(), called at the
//    top of each platform destructor, destroys them while the application is
//    still whole.
// 2. The native half was never released. ~UltraCanvasWindowBase cannot call a
//    virtual override, so a window destroyed without Close() first left its X
//    window, its xlib surface, its input context and its drag-drop cursors
//    behind. Each platform window class now releases them in its own
//    destructor.
//
// The first is why the last check here has nothing to read: reaching it at
// all is the result. The X server answers the second - a window that was
// really destroyed is BadWindow.
//
// X11 only (it asks the X server about windows the framework has dropped);
// runs headless under Xvfb. Skips - rather than fails - without a display,
// unless ULTRACANVAS_TEST_DISPLAY_REQUIRED says a display was to be there.
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"
#include "DisplayTestSupport.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(ULTRACANVAS_DISPLAY_WAYLAND)
#define SHUTDOWN_TEST_X11 1
#include <X11/Xlib.h>
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

// A display was required and is not there: that is a failure, not a skip, so
// a runner without one cannot report a pass (see the Xvfb steps in
// .github/workflows/build.yml).
#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

#ifdef SHUTDOWN_TEST_X11
namespace {

class TestApplication : public UltraCanvasApplication {
public:
    using UltraCanvasApplication::ProcessPostedTasks;
};

// X kills a program that asks about a window it no longer has, so the asking
// is done with an error handler that only counts.
int g_xErrors = 0;
int CountXError(Display*, XErrorEvent*) { ++g_xErrors; return 0; }

// Whether the X server still has this window. A window the framework really
// destroyed answers BadWindow.
bool XWindowAlive(Display* display, Window window) {
    XSync(display, False);
    g_xErrors = 0;
    XWindowAttributes attributes;
    const Status ok = XGetWindowAttributes(display, window, &attributes);
    XSync(display, False);
    return ok && g_xErrors == 0;
}

std::shared_ptr<UltraCanvasWindow> OpenWindow(const std::string& title) {
    WindowConfig config;
    config.title = title;
    config.type = WindowType::Borderless;
    config.x = 80;
    config.y = 80;
    config.width = 320;
    config.height = 240;
    auto window = CreateWindow(config);
    if (!window || !window->IsCreated()) return nullptr;
    window->Show();
    return window;
}

// A menu opened as a popup on `owner`, which gives the popup a window of its
// own - the window whose Hide() used to be the crash.
std::shared_ptr<UltraCanvasMenu> OpenMenuOn(TestApplication& app,
                                            const std::shared_ptr<UltraCanvasWindow>& owner) {
    auto menu = std::make_shared<UltraCanvasMenu>("ShutdownMenu", 0, 0, 160, 0);
    menu->SetMenuType(MenuType::PopupMenu);
    MenuStyle style = menu->GetStyle();
    style.enableAnimations = false;
    menu->SetStyle(style);
    menu->AddItem(MenuItemData::Action("One", []() {}));
    menu->AddItem(MenuItemData::Action("Two", []() {}));
    PopupElementSettings settings;
    menu->OpenMenu(Point2Di(40, 30), *owner, settings);
    for (int round = 0; round < 3; ++round) {
        XSync(app.GetDisplay(), False);
        app.ProcessPostedTasks();
        for (const auto& window : app.GetWindows()) {
            if (window && window->IsCreated()) window->UpdateAndRender();
        }
        XSync(app.GetDisplay(), False);
    }
    return menu;
}

} // namespace
#endif

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Window Shutdown Suite"                 << std::endl;
    std::cerr << "========================================" << std::endl;

#ifndef SHUTDOWN_TEST_X11
    SKIP_ALL("the checks ask the X server about destroyed windows");
#else
    if (!std::getenv("DISPLAY")) SKIP_ALL("there is no DISPLAY");
    XSetErrorHandler(CountXError);

    {
        TestApplication app;
        if (!app.Initialize("WindowShutdownTest")) SKIP_ALL("the application would not start");

        // ===== DestroyWindows() releases the native half =====
        std::cerr << "\n-- A window destroyed, never closed --" << std::endl;
        auto window = OpenWindow("Shutdown");
        if (!window) SKIP_ALL("the window could not be created");
        auto menu = OpenMenuOn(app, window);
        UltraCanvasWindowBase* popupHost = window->GetPopupWindow(*menu);
        TEST("setup: the menu has a window of its own", popupHost != nullptr);
        if (!popupHost) {
            std::cerr << "Results: " << (testCount - failCount) << "/" << testCount
                      << " passed" << std::endl;
            return 1;
        }

        const Window windowX = static_cast<Window>(window->GetNativeHandle());
        const Window popupX = static_cast<Window>(popupHost->GetNativeHandle());
        TEST("setup: the X server has both windows",
             XWindowAlive(app.GetDisplay(), windowX) && XWindowAlive(app.GetDisplay(), popupX));
        TEST("setup: the application holds them", app.GetWindows().size() >= 2);

        std::weak_ptr<UltraCanvasWindowBase> windowAlive;
        std::weak_ptr<UltraCanvasWindowBase> popupAlive;
        for (const auto& w : app.GetWindows()) {
            if (w.get() == window.get()) windowAlive = w;
            if (w.get() == popupHost) popupAlive = w;
        }
        // The caller's own reference goes; the application's is the last one,
        // as it is for a window an application opened and never closed.
        window.reset();
        menu.reset();

        app.DestroyWindows();
        TEST("the application holds no windows afterwards", app.GetWindows().empty());
        TEST("the window itself is gone, not merely unregistered", windowAlive.expired());
        TEST("the popup's own window is gone too", popupAlive.expired());
        // The point of the window destructors. Nothing closed this window, so
        // without one nothing ever called XDestroyWindow for it and the X
        // server kept it until the connection closed. (The popup's own window
        // is released along the way by ReleasePopupHost, which does close it -
        // it is checked here because the two must not disagree.)
        TEST("the X window was destroyed with it", !XWindowAlive(app.GetDisplay(), windowX));
        TEST("and so was the popup window's", !XWindowAlive(app.GetDisplay(), popupX));

        // ===== The application destroyed with a window still open =====
        // What UltraMail's message-pane suite did at the end of its main(),
        // and what used to end in SIGSEGV: ~UltraCanvasWindowBase ->
        // CloseAllPopups -> ReleasePopupHost -> LinuxWindow::Hide() ->
        // application->GetDisplay() through a null application.
        std::cerr << "\n-- The application destroyed with a window open --" << std::endl;
        auto second = OpenWindow("Shutdown Again");
        if (!second) SKIP_ALL("the second window could not be created");
        auto secondMenu = OpenMenuOn(app, second);
        TEST("setup: that window has an open popup in a window of its own",
             second->GetPopupWindow(*secondMenu) != nullptr);
        // Nothing is closed, nothing is reset: `app` goes out of scope here
        // holding a window that holds an open popup.
    }

    // Reaching this line is the check. The crash was in the application's
    // destructor, so a failure here is a SIGSEGV that never gets to print.
    TEST("the application was destroyed with its windows and popups open, and the process lived",
         true);

    std::cerr << "\nResults: " << (testCount - failCount) << "/" << testCount
              << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
#endif
}
