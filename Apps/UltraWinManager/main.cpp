// Apps/UltraWinManager/main.cpp
// Entry point for UltraWin Manager — the graphical front-end for Windows
// applications on ULTRA OS (environments, installed programs, and the
// Windows VM). Bootstrap follows the UltraAI dashboard app.
// Version: 0.1.1 - one UltraWin Manager per user: a second start brings the
//                  running window forward instead of opening another copy
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinManagerWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraWin/UltraWin.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

UltraCanvasApplication* g_app = nullptr;

#ifdef __linux__
void OnSignal(int sig) {
    if (g_app) g_app->RequestExit();
    std::exit(sig == SIGTERM ? EXIT_SUCCESS : EXIT_FAILURE);
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::cout << "UltraWin Manager — Windows applications on ULTRA "
                         "OS.\nUsage: "
                      << argv[0] << " [-h] [-v]\n";
            return 0;
        }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraWin Manager " << UltraWin_GetVersion() << "\n";
            return 0;
        }
    }

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    // One UltraWin Manager per user: a second start brings the running
    // window forward and quits. --help/--version above never claim.
    UltraCanvasSingleInstance instance("org.ultraos.ultrawinmanager", "UltraWin Manager");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

    UltraCanvasApplication app;
    g_app = &app;
    try {
        if (!app.Initialize("UltraWinManager")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        // Another copy holds the lock but did not answer: say so, quit.
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraWin Manager");
        UltraWin_Initialize();

        UltraWinManager::ManagerWindow window(app);
        if (!window.Create()) {
            std::cerr << "Failed to create the manager window\n";
            return EXIT_FAILURE;
        }
        window.Show();
        instance.ListenRaise();   // a later start brings the window forward
        app.Run();
        instance.Close();         // before the window goes
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    g_app = nullptr;
    return EXIT_SUCCESS;
}
