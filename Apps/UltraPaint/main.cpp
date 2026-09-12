// Apps/UltraPaint/main.cpp
// UltraPaint - bitmap editor built on the UltraCanvas framework: layers,
// selections, brushes and shapes on the framework's raster-editing layer
// (UCRasterDocument / UltraCanvasPaintSurface), adjustments and filters
// through PixelFX (libvips).
// Version: 1.1.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework

#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include "UltraPaintWindow.h"

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
    debugOutput << "UltraPaint - Bitmap Editor powered by UltraCanvas Framework" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Usage: " << programName << " [options] [image]" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Options:" << std::endl;
    debugOutput << "  -h, --help        Show this help message" << std::endl;
    debugOutput << "  -v, --version     Show version information" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Examples:" << std::endl;
    debugOutput << "  " << programName << "                 # blank canvas" << std::endl;
    debugOutput << "  " << programName << " photo.jpg       # open an image" << std::endl;
    debugOutput << "  " << programName << " work.ucraster   # open a layered project" << std::endl;
    debugOutput << "  " << programName << " logo.svg        # a drawing: asks for the raster size" << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> pathsToOpen;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            debugOutput << "UltraPaint version " << ULTRAPAINT_VERSION << std::endl;
            debugOutput << "UltraCanvas Framework" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg[0] != '-') {
            pathsToOpen.push_back(arg);
        } else {
            debugOutput << "Unknown argument: " << arg << std::endl;
            debugOutput << "Use --help for usage information" << std::endl;
            return EXIT_FAILURE;
        }
    }

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

        // The editor is multi-window: UltraPaintWindow keeps every open window
        // alive itself (File > New Window, and "Open new window" on a dropped
        // image add to that list), and the application exits with the last of
        // them.
        if (!UltraPaintWindow::OpenWindow(pathsToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }

        debugOutput << "=== UltraPaint Ready ===" << std::endl;
        app.Run();
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
