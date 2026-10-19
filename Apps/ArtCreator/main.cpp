// Apps/ArtCreator/main.cpp
// ArtCreator - vector drawing editor on the UltraCanvas framework's vector
// editing layer (VectorStorage::VectorDocument, VectorEdit,
// UltraCanvasVectorCanvas) with the Vector plugin's converters for the
// file formats.
// One ArtCreator per user: a second start hands its drawings to the running
// copy, which opens each in a window of its own (UltraCanvasSingleInstance.h);
// --help and --version print and quit without asking it.
// Version: 1.1.0 - one ArtCreator per user: a second start's drawings open in
//                  the running copy, a start without drawings brings it forward
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include "ArtCreatorWindow.h"
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "UltraCanvasVectorFormatsPlugin.h"
#endif

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef __linux__
#include <X11/Xlib.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

// ARTCREATOR_VERSION comes from the build alone: CMake reads the first
// line of Docs/ArtCreator/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build
// that lost it fails instead of reporting a wrong number.
#ifndef ARTCREATOR_VERSION
#error "ARTCREATOR_VERSION is not defined: build ArtCreator through the top-level CMakeLists.txt"
#endif

using namespace UltraCanvas;

namespace {

// The one call a signal handler may make: it sets a flag the main loop
// turns into RequestExit, so main returns and the destructors run in order.
void SignalHandler(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}

void HandleFatalError(const std::string& message) {
    debugOutput << "FATAL: " << message << std::endl;
    UltraCanvasDialogManager::ShowError(message, "ArtCreator");
}

// --help and --version answer on stdout (errors on stderr): they are for the
// shell, not the diagnostic stream, which a Release build keeps off.
void PrintUsage(const char* programName) {
    std::cout << "ArtCreator " << ARTCREATOR_VERSION << " - Vector Drawing Editor powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [drawing...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -v, --version     Show version information" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                 # a new A4 drawing" << std::endl;
    std::cout << "  " << programName << " logo.svg        # open a drawing" << std::endl;
    std::cout << "  " << programName << " a.svg b.xar     # one window each" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> pathsToOpen;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "ArtCreator version " << ARTCREATOR_VERSION << std::endl;
            std::cout << "UltraCanvas Framework" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg[0] != '-') {
            pathsToOpen.push_back(arg);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Only a start that opens a window claims the one copy per user; the
    // print-and-quit options above never get here. The paths go over as
    // absolute ones: the running copy was started in another folder. Declared
    // before the application, so it outlives app.Run().
    UltraCanvasSingleInstance instance("org.ultraos.artcreator", "ArtCreator");
    const UltraCanvasSingleInstance::Start start =
        instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(pathsToOpen));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy opened them

    UltraCanvasApplication app;

#ifdef __linux__
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    try {
        debugOutput << "=== ArtCreator - Vector Drawing Editor ===" << std::endl;
        if (!app.Initialize("ArtCreator")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/ArtCreator.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "ArtCreator");
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
        // The file formats: SVG, XAR, EMF, WMF, DXF, DWG in; those plus PDF,
        // AI, EPS, CDR out. Without the plugin the editor still draws, but
        // Open and Save say so.
        RegisterVectorFormatsPlugin();
#endif

        // Listening starts before the command line's files are opened: a
        // file that cannot be read shows a native error dialog that holds the
        // start-up, and a start made meanwhile is still answered.
        //
        // A later start's drawings open here as that start would have opened
        // them: each in a new window (OpenWindow gives every path one), never
        // in place of a drawing open now, so nothing unsaved is touched. The
        // first of them comes to the front.
        instance.ListenOpen([](const std::vector<std::string>& paths) {
            const std::shared_ptr<ArtCreatorWindow> opened = ArtCreatorWindow::OpenWindow(paths);
            if (opened) opened->BringForward();
            else UltraCanvasSingleInstance::BringToFront();
        });

        if (!ArtCreatorWindow::OpenWindow(pathsToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }

        debugOutput << "=== ArtCreator Ready ===" << std::endl;
        app.Run();
        instance.Close();   // no request reaches a window once they start to go
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Unhandled exception: ") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        HandleFatalError("Unknown exception occurred");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
#endif
