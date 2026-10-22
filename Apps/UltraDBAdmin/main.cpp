// Apps/UltraDBAdmin/main.cpp
// UltraDBAdmin - browse and query the databases UltraDatabase reaches, SQLite
// files and PostgreSQL servers, in the spirit of phpMyAdmin
// (Docs/Research/UltraDBAdminDesignProposal.md). This is the application:
// the single-instance claim, the window, the event loop. The window is
// ui/DbAdminWindow; what needs no window is in engine/.
//
// One window per user: a second start brings the running one forward and
// quits (UltraCanvasSingleInstance.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/DbAdminTheme.h"
#include "ui/DbAdminWindow.h"

#include "engine/DbAdminPaths.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

// ULTRADBADMIN_VERSION comes from the build alone: CMake reads the first line
// of Docs/UltraDBAdmin/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and
// passes it as a compile definition. No fallback here, so a build that lost
// it fails instead of reporting a wrong number.
#ifndef ULTRADBADMIN_VERSION
#error "ULTRADBADMIN_VERSION is not defined: build through CMake, which reads it from Docs/UltraDBAdmin/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace {

// Ctrl-C and SIGTERM: one flag store, nothing else - a handler may not log,
// lock or exit. The next loop iteration ends the loop on the main thread, and
// the window's destructor stops its threads before any static destructor runs.
void SignalHandler(int) { UltraCanvasApplicationBase::RequestExitFromSignal(); }

void PrintUsage(const char* programName) {
    std::printf(
        "UltraDBAdmin - browse and query SQLite files and PostgreSQL servers\n"
        "Powered by the UltraCanvas framework\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)    Open the UltraDBAdmin window\n"
        "  -v, --version   Show version information\n"
        "  -h, --help      Show this message\n"
        "\n"
        "Connections are kept in %s\n"
        "(connections.json, no passwords; those are in vault/, encrypted).\n",
        programName, UltraDBAdmin::DataDirectory().c_str());
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-h") || !std::strcmp(argv[i], "--help")) {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (!std::strcmp(argv[i], "-v") || !std::strcmp(argv[i], "--version")) {
            std::printf("UltraDBAdmin %s\n", ULTRADBADMIN_VERSION);
            return EXIT_SUCCESS;
        }
        std::fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    UltraCanvasSingleInstance instance("org.ultraos.ultradbadmin", "UltraDBAdmin");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy came forward

    UltraCanvasApplication app;

#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    // The window's queries and the SQL console run on threads of their own.
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    try {
        if (!app.Initialize("UltraDBAdmin")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraDBAdmin");
        // One icon, everywhere the app is drawn: the window and its taskbar
        // entry read this file; the .ico embedded in the Windows binary and
        // the desktop entry's theme icon are rendered from the same
        // media/appicon/UltraDBAdmin.svg (see CMakeLists.txt).
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraDBAdmin.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);
        // Before the window: its elements are styled from the theme as they are made.
        UltraCanvasTheme::SetCurrent(UltraDBAdmin::AppTheme());

        UltraDBAdmin::DbAdminWindow window;
        if (!window.Initialize(UltraDBAdmin::DataDirectory())) {
            debugOutput << "Failed to create the UltraDBAdmin window" << std::endl;
            return EXIT_FAILURE;
        }
        window.Show();
        instance.ListenRaise();   // a later start brings this window forward
        app.Run();
        instance.Close();         // no request reaches the window once it starts to go
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
