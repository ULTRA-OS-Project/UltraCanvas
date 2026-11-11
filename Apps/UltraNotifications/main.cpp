// Apps/UltraNotifications/main.cpp
// Entry point: UltraNotifications, the notifications that stay - every
// notification, chat message and mail on the UltraMessage feed, after the
// desktop's toast has gone.
//
//   UltraNotifications                the window
//   UltraNotifications --background   no window: connected and counting the
//                                     unread for the desktop's bell; the
//                                     next start shows the window at once
//
// One copy per user (UltraCanvasSingleInstance): a later start brings the
// running one forward - a --background start changes nothing.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraNotificationsWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#ifndef ULTRANOTIFICATIONS_VERSION
#error "ULTRANOTIFICATIONS_VERSION must come from Docs/UltraNotifications/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraNotifications - the notifications of ULTRA OS, read, searched and dismissed.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --background   start without a window, counting the unread for the desktop\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

} // namespace

int main(int argc, char* argv[]) {
    bool background = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraNotifications " << ULTRANOTIFICATIONS_VERSION << "\n";
            return 0;
        }
        if (a == "--background") {
            background = true;
            continue;
        }
        std::cerr << "Unknown option: " << a << "\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraNotifications::UltraNotificationsWindow> window;
    // Declared after the window, so it stops listening before the window goes.
    UltraCanvasSingleInstance instance("org.ultraos.ultranotifications", "UltraNotifications");
    JSONValue request = JSONValue::MakeObject();
    if (background) request.Set("background", true);
    const UltraCanvasSingleInstance::Start start = instance.Claim(request);
    if (start == UltraCanvasSingleInstance::Start::HandedOff) {
        std::cout << (background ? "UltraNotifications is already running.\n"
                                 : "UltraNotifications is already running: brought it forward.\n");
        return EXIT_SUCCESS;
    }
    try {
        if (!app.Initialize("UltraNotifications")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraNotifications.png"));
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraNotifications");
        window = std::make_unique<UltraNotifications::UltraNotificationsWindow>(app);
        if (!window->Create()) {
            std::cerr << "Failed to create the main window\n";
            return EXIT_FAILURE;
        }
        if (background) window->SetKeepRunning(true);
        else window->Show();
        UltraNotifications::UltraNotificationsWindow* shown = window.get();
        instance.Listen([shown](const JSONValue& request) {
            // Another background start (the desktop starting again): it is
            // running, which is all that one wanted.
            if (request.Get("background").GetBoolean()) return;
            shown->Present();
        });
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
    instance.Close();
    window.reset();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
