// Tests/WindowCurrentDesktopTest.cpp
// A window asked to come forward comes to the virtual desktop in view (X11).
//
// RaiseAndFocus asked the window manager to activate the window and nothing
// else. For a window on another virtual desktop a manager answers that by
// marking it "wants attention" where it lies (openbox, metacity) or by
// changing the desktop (icewm): UltraDesktop's "Desktop settings..." and its
// UltraClipboard button did nothing the person could see when UOS-Settings or
// UltraClipboard was already open on another desktop. The window now asks to
// be moved to the current desktop first (_NET_WM_DESKTOP), with the window it
// belongs to, and IsOnCurrentDesktop says where it is.
//
// The test plays the manager's part on a display without one, as Xvfb is in
// CI: it sets _NET_CURRENT_DESKTOP on the root and _NET_WM_DESKTOP on the
// windows, and a second connection reads the requests a manager would get
// (the client messages sent to the root). With a window manager on the
// display those properties are the manager's, so the test does not run there.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "DisplayRequired.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstdlib>
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

namespace {

constexpr long kEveryDesktop = 0xFFFFFFFFL;

// One request a window manager would have received.
struct Request {
    std::string what;   // "_NET_WM_DESKTOP" or "_NET_ACTIVE_WINDOW"
    Window window = 0;
    long first = 0;     // the desktop asked for / the source indication
    long second = 0;    // the source indication / the timestamp
};

// The window manager's ear: a connection of its own that hears what clients
// send to the root for the manager.
class Manager {
public:
    bool Start() {
        display = XOpenDisplay(nullptr);
        if (!display) return false;
        root = DefaultRootWindow(display);
        wmDesktop = XInternAtom(display, "_NET_WM_DESKTOP", False);
        activeWindow = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
        currentDesktop = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
        XSelectInput(display, root, SubstructureNotifyMask);
        XSync(display, False);
        return true;
    }

    ~Manager() {
        if (!display) return;
        XDeleteProperty(display, root, currentDesktop);
        XSync(display, False);
        XCloseDisplay(display);
    }

    // A real manager names the window it runs behind on the root.
    bool AnotherManagerRuns() const {
        Atom type = 0;
        int format = 0;
        unsigned long count = 0, left = 0;
        unsigned char* data = nullptr;
        const bool found = XGetWindowProperty(display, root, XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False),
                                              0, 1, False, XA_WINDOW, &type, &format, &count, &left, &data) == 0 &&
                           data && count >= 1;
        if (data) XFree(data);
        return found;
    }

    void SetCurrentDesktop(long index) { SetCardinal(root, currentDesktop, index); }
    void SetWindowDesktop(Window window, long index) { SetCardinal(window, wmDesktop, index); }
    void ClearWindowDesktop(Window window) {
        XDeleteProperty(display, window, wmDesktop);
        XSync(display, False);
    }

    // What the application asked for since the last call, in order. `app`
    // is the application's connection: its requests reach the server first.
    std::vector<Request> Take(Display* app) {
        XSync(app, False);
        XSync(display, False);
        std::vector<Request> requests;
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != ClientMessage) continue;
            Request request;
            if (event.xclient.message_type == wmDesktop) request.what = "_NET_WM_DESKTOP";
            else if (event.xclient.message_type == activeWindow) request.what = "_NET_ACTIVE_WINDOW";
            else continue;
            request.window = event.xclient.window;
            request.first = event.xclient.data.l[0];
            request.second = event.xclient.data.l[1];
            requests.push_back(request);
        }
        return requests;
    }

private:
    void SetCardinal(Window window, Atom property, long value) {
        XChangeProperty(display, window, property, XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&value), 1);
        XSync(display, False);
    }

    Display* display = nullptr;
    Window root = 0;
    Atom wmDesktop = 0, activeWindow = 0, currentDesktop = 0;
};

bool Is(const Request& request, const char* what, Window window) {
    return request.what == what && request.window == window;
}

std::shared_ptr<UltraCanvasWindow> MakeWindow(const std::string& title, UltraCanvasWindowBase* parent = nullptr) {
    WindowConfig config;
    config.title = title;
    config.width = 320;
    config.height = 200;
    if (parent) {
        config.type = WindowType::Dialog;
        config.parentWindow = parent;
    }
    auto window = CreateWindow(config);
    if (window) window->Show();
    return window;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Window: The Current Virtual Desktop"    << std::endl;
    std::cerr << "========================================" << std::endl;

    UltraCanvasApplication app;
    Display* display = nullptr;
    if (std::getenv("DISPLAY") && app.Initialize("WindowCurrentDesktopTest")) display = app.GetDisplay();
    Manager manager;
    if (!display || !manager.Start()) {
        if (DisplayTest::DisplayRequired()) {
            std::cerr << "FAIL: a display was required, and there is no X11 display" << std::endl;
            return 1;
        }
        std::cerr << "SKIP: no X11 display" << std::endl;
        return 0;
    }
    if (manager.AnotherManagerRuns()) {
        if (DisplayTest::DisplayRequired()) {
            std::cerr << "FAIL: a display was required, and this one has a window manager: the test plays the "
                         "manager itself (run it under Xvfb)" << std::endl;
            return 1;
        }
        std::cerr << "SKIP: a window manager runs on this display, and the test plays the manager itself "
                     "(run it under Xvfb)" << std::endl;
        return 0;
    }

    auto mainWindow = MakeWindow("WindowCurrentDesktopTest main");
    if (!mainWindow) {
        std::cerr << "FAIL: the window could not be created" << std::endl;
        return 1;
    }
    const auto mainX = static_cast<Window>(mainWindow->GetNativeHandle());
    manager.Take(display);

    std::cerr << "\n--- No manager names a desktop ---" << std::endl;
    {
        TEST("A window no desktop is named for counts as on the current one", mainWindow->IsOnCurrentDesktop());
        TEST("and has nowhere to be moved to", !mainWindow->MoveToCurrentDesktop());
        mainWindow->RaiseAndFocus();
        const std::vector<Request> requests = manager.Take(display);
        TEST("RaiseAndFocus asks for the activation alone",
             requests.size() == 1 && Is(requests[0], "_NET_ACTIVE_WINDOW", mainX));
    }

    std::cerr << "\n--- The window on desktop 1, desktop 2 in view ---" << std::endl;
    manager.SetCurrentDesktop(1);
    manager.SetWindowDesktop(mainX, 0);
    {
        TEST("The window is not on the current desktop", !mainWindow->IsOnCurrentDesktop());
        TEST("MoveToCurrentDesktop says it asked", mainWindow->MoveToCurrentDesktop());
        const std::vector<Request> requests = manager.Take(display);
        TEST("It asks to be moved, once", requests.size() == 1 && Is(requests[0], "_NET_WM_DESKTOP", mainX));
        TEST("to the desktop in view", !requests.empty() && requests[0].first == 1);
        TEST("as an application (source indication 1)", !requests.empty() && requests[0].second == 1);
    }
    {
        mainWindow->RaiseAndFocus();
        const std::vector<Request> requests = manager.Take(display);
        TEST("RaiseAndFocus asks for two things", requests.size() == 2);
        TEST("first to be moved to the desktop in view",
             requests.size() == 2 && Is(requests[0], "_NET_WM_DESKTOP", mainX) && requests[0].first == 1);
        TEST("then to be activated", requests.size() == 2 && Is(requests[1], "_NET_ACTIVE_WINDOW", mainX));
    }

    std::cerr << "\n--- The window where the person is ---" << std::endl;
    manager.SetWindowDesktop(mainX, 1);   // what the manager does with the request
    {
        TEST("Moved, the window is on the current desktop", mainWindow->IsOnCurrentDesktop());
        TEST("and is not moved again", !mainWindow->MoveToCurrentDesktop());
        mainWindow->RaiseAndFocus();
        const std::vector<Request> requests = manager.Take(display);
        TEST("RaiseAndFocus asks for the activation alone",
             requests.size() == 1 && Is(requests[0], "_NET_ACTIVE_WINDOW", mainX));
    }

    std::cerr << "\n--- A window on every desktop ---" << std::endl;
    manager.SetWindowDesktop(mainX, kEveryDesktop);
    {
        TEST("It is on the current desktop", mainWindow->IsOnCurrentDesktop());
        mainWindow->RaiseAndFocus();
        const std::vector<Request> requests = manager.Take(display);
        TEST("and stays on every desktop when it is raised",
             requests.size() == 1 && Is(requests[0], "_NET_ACTIVE_WINDOW", mainX));
    }

    std::cerr << "\n--- A dialog and the window it belongs to ---" << std::endl;
    {
        auto dialog = MakeWindow("WindowCurrentDesktopTest dialog", mainWindow.get());
        TEST("The dialog opens", dialog != nullptr);
        if (dialog) {
            const auto dialogX = static_cast<Window>(dialog->GetNativeHandle());
            manager.SetWindowDesktop(mainX, 0);
            manager.SetWindowDesktop(dialogX, 0);
            manager.Take(display);

            dialog->RaiseAndFocus();
            std::vector<Request> requests = manager.Take(display);
            TEST("The dialog raised asks for three things", requests.size() == 3);
            TEST("its window moved first", requests.size() == 3 && Is(requests[0], "_NET_WM_DESKTOP", mainX) &&
                                           requests[0].first == 1);
            TEST("then itself", requests.size() == 3 && Is(requests[1], "_NET_WM_DESKTOP", dialogX) &&
                                requests[1].first == 1);
            TEST("then its activation", requests.size() == 3 && Is(requests[2], "_NET_ACTIVE_WINDOW", dialogX));

            // The window on every desktop (a desktop's own, of its panels).
            manager.SetWindowDesktop(mainX, kEveryDesktop);
            dialog->RaiseAndFocus();
            requests = manager.Take(display);
            TEST("A window on every desktop stays there when its dialog is brought over",
                 requests.size() == 2 && Is(requests[0], "_NET_WM_DESKTOP", dialogX) &&
                 Is(requests[1], "_NET_ACTIVE_WINDOW", dialogX));

            // The dialog not placed by the manager, its window elsewhere.
            manager.SetWindowDesktop(mainX, 0);
            manager.ClearWindowDesktop(dialogX);
            TEST("A dialog no desktop is named for still brings its window", dialog->MoveToCurrentDesktop());
            requests = manager.Take(display);
            TEST("and asks for that window alone",
                 requests.size() == 1 && Is(requests[0], "_NET_WM_DESKTOP", mainX) && requests[0].first == 1);

            dialog->Close();
        }
    }

    mainWindow->Close();

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << " / " << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
