// Apps/UltraAIApp/main.cpp
// Entry point for the UltraAI dashboard app: a grid of icon buttons,
// one per AI capability. Clicking an icon opens a modal dialog
// driven by the matching in-process mock adapter — no network or
// external models required.
// Version: 0.1.1
// Last Modified: 2026-07-12
// Author: UltraAI Module

#include "UltraAIDashboard.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasDebug.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using namespace UltraCanvas;
using namespace UltraAIApp;

namespace {

UltraCanvasApplication* g_app = nullptr;

#ifdef __linux__
void OnSignal(int sig) {
    if (g_app) g_app->RequestExit();
    std::exit(sig == SIGTERM ? EXIT_SUCCESS : EXIT_FAILURE);
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraAI — choose-a-service dashboard demo.\n"
              << "Usage: " << prog << " [options]\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")    { PrintUsage(argv[0]); return 0; }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraAI app 0.1.0\n"; return 0;
        }
    }

#ifdef __linux__
    if (!XInitThreads()) {
        std::cerr << "warning: XInitThreads failed\n";
    }
    std::signal(SIGINT,  OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    g_app = &app;

    try {
        if (!app.Initialize("UltraAI")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }

        UltraCanvasDialogManager::SetUseNativeDialogs(false);

        UltraAIDashboard dashboard(app);
        if (!dashboard.Create()) {
            std::cerr << "Failed to create dashboard window\n";
            return EXIT_FAILURE;
        }
        dashboard.Show();

        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return EXIT_FAILURE;
    }

    g_app = nullptr;
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
