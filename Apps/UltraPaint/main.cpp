// Apps/UltraPaint/main.cpp
// UltraPaint - bitmap editor built on the UltraCanvas framework: layers,
// selections, brushes and shapes on the framework's raster-editing layer
// (UCRasterDocument / UltraCanvasPaintSurface), adjustments and filters
// through PixelFX (libvips).
// One UltraPaint per user: a second start hands its images to the running
// copy, which opens each in a window of its own (UltraCanvasSingleInstance.h);
// --help and --version print and quit without asking it.
// Version: 1.2.0 - one UltraPaint per user: a second start's images open in
//                  the running copy, a start without images brings it forward
// Version: 1.1.1
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include "UltraPaintWindow.h"

// The 3D formats. Registering the plugin is what turns "UltraPaint reads STL"
// into "UltraPaint reads every model format this build has a reader for" —
// the importer asks the registry, not a hard-coded list.
#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN
#include "Models/UltraCanvasModelFormatsPlugin.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <signal.h>
#endif

using namespace UltraCanvas;

static UltraCanvasApplication* g_app = nullptr;

static void HandleFatalError(const std::string& error) {
    debugOutput << "FATAL ERROR: " << error << std::endl;
#ifdef _WIN32
    MessageBoxA(nullptr, error.c_str(), "UltraPaint - Fatal Error", MB_ICONERROR | MB_OK);
#endif
    std::exit(EXIT_FAILURE);
}

#ifdef __linux__
static void SignalHandler(int signal) {
    debugOutput << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;
    if (g_app) g_app->RequestExit();
    std::exit(EXIT_SUCCESS);
}
#endif

static void PrintUsage(const char* programName) {
    std::cout << "UltraPaint - Bitmap Editor powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [image...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -v, --version     Show version information" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                 # blank canvas" << std::endl;
    std::cout << "  " << programName << " photo.jpg       # open an image" << std::endl;
    std::cout << "  " << programName << " work.ucraster   # open a layered project" << std::endl;
    std::cout << "  " << programName << " logo.svg        # a drawing: asks for the raster size" << std::endl;
    std::cout << "  " << programName << " part.stl        # a 3D model: asks for the view and the size" << std::endl;
    std::cout << "  " << programName << " a.png b.png     # one window each - what a drop on the" << std::endl;
    std::cout << "                                   # application icon expands to" << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> pathsToOpen;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "UltraPaint version " << ULTRAPAINT_VERSION << std::endl;
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
    UltraCanvasSingleInstance instance("org.ultraos.ultrapaint", "UltraPaint");
    const UltraCanvasSingleInstance::Start start =
        instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(pathsToOpen));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy opened them

    UltraCanvasApplication app;
    g_app = &app;

#ifdef __linux__
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    try {
        debugOutput << "=== UltraPaint - Bitmap Editor ===" << std::endl;
        if (!app.Initialize("UltraPaint")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraPaint.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraPaint");
#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN
        RegisterModelFormatsPlugin();
#endif

        // The editor is multi-window: UltraPaintWindow keeps every open window
        // alive itself (File > New Window, and "Open new window" on a dropped
        // image add to that list), and the application exits with the last of
        // them.
        // Listening starts before the command line's files are opened: a
        // file that cannot be read shows a native error dialog that holds the
        // start-up, and a start made meanwhile is still answered.
        //
        // A later start's images open here as that start would have opened
        // them: each in a new window (OpenWindow gives every path one), never
        // in place of an image open now, so nothing unsaved is touched. The
        // first of them comes to the front.
        instance.ListenOpen([](const std::vector<std::string>& paths) {
            const std::shared_ptr<UltraPaintWindow> opened = UltraPaintWindow::OpenWindow(paths);
            if (opened) opened->BringForward();
            else UltraCanvasSingleInstance::BringToFront();
        });

        if (!UltraPaintWindow::OpenWindow(pathsToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }

        debugOutput << "=== UltraPaint Ready ===" << std::endl;
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
