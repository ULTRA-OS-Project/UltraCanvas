// Apps/UltraToDo/main.cpp
// Entry point: UltraToDo, the task list of ULTRA OS - tasks with a planned
// end, an estimated time, a description and attached files, the most urgent
// first, with a reminder before a task has to be started and an alarm at
// its planned end.
//
//   UltraToDo                the window
//   UltraToDo --background   no window: the reminders and alarms run, and the
//                            next start shows the window at once
//
// One copy per user (UltraCanvasSingleInstance): a later start brings the
// running one forward.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraToDoWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasNotificationActivation.h"
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

#ifndef ULTRATODO_VERSION
#error "ULTRATODO_VERSION must come from Docs/UltraToDo/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraToDo - tasks, the most urgent first, with reminders and alarms.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --background   start without a window; the reminders and alarms run\n"
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
            std::cout << "UltraToDo " << ULTRATODO_VERSION << "\n";
            return 0;
        }
        if (a == "--background") {
            background = true;
            continue;
        }
        // Windows starts the program for a click on one of its notifications.
        if (IsNotificationActivationArgument(a)) continue;
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
    std::unique_ptr<UltraToDo::UltraToDoWindow> window;
    // Declared after the window, so it stops listening before the window goes.
    UltraCanvasSingleInstance instance("org.ultraos.ultratodo", "UltraToDo");
    JSONValue request = JSONValue::MakeObject();
    if (background) request.Set("background", true);
    const UltraCanvasSingleInstance::Start start = instance.Claim(request);
    if (start == UltraCanvasSingleInstance::Start::HandedOff) {
        using Note = UltraCanvasSingleInstance::HandOffNote;
        std::cout << UltraCanvasSingleInstance::HandOffMessage("UltraToDo",
                                                               background ? Note::Running : Note::BroughtForward)
                  << "\n";
        return EXIT_SUCCESS;
    }
    try {
        if (!app.Initialize("UltraToDo")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraToDo.png"));
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraToDo");
        window = std::make_unique<UltraToDo::UltraToDoWindow>(app);
        if (!window->Create()) {
            std::cerr << "Failed to create the main window\n";
            return EXIT_FAILURE;
        }
        window->SetKeepRunning(background);
        if (!background) window->Show();
        UltraToDo::UltraToDoWindow* shown = window.get();
        instance.Listen([shown](const JSONValue& request) {
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
