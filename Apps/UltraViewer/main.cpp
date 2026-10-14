// Apps/UltraViewer/main.cpp
// UltraViewer - universal media viewer built on the UltraCanvas framework:
// one full-window UltraCanvasMediaViewer displays bitmaps, vector graphics,
// video and audio (with player transport controls), documents (PDF),
// e-books (EPUB/FB2/MOBI/AZW), spreadsheets (ODS/CSV/TSV), 3D models (STL),
// text / source / markdown and UltraCanvas Document containers (*.ucd).
// One UltraViewer per user: a second start hands its files and folders to the
// running copy, whose viewer shows them in place of what it showed
// (UltraCanvasSingleInstance.h); --help and --version print and quit without
// asking it.
// Version: 1.1.0 - one UltraViewer per user: a second start's files are shown
//                  in the running viewer, a start without files brings it forward
// Version: 1.0.1
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraViewerWindow.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <signal.h>
#include "UltraCanvasDebug.h"
#endif

using namespace UltraCanvas;

static UltraCanvasApplication* g_app = nullptr;

// ===== ERROR HANDLING =====
static void HandleFatalError(const std::string& error) {
    debugOutput << "FATAL ERROR: " << error << std::endl;
#ifdef _WIN32
    MessageBoxA(nullptr, error.c_str(), "UltraViewer - Fatal Error", MB_ICONERROR | MB_OK);
#endif
    std::exit(EXIT_FAILURE);
}

// ===== SIGNAL HANDLERS =====
#ifdef __linux__
static void SignalHandler(int signal) {
    debugOutput << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;
    if (g_app) g_app->RequestExit();
    std::exit(EXIT_SUCCESS);
}
#endif

static void PrintUsage(const char* programName) {
    std::cout << "UltraViewer - Universal Media Viewer powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [file|folder ...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -v, --version     Show version information" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                    # Empty viewer (use Open or drag & drop)" << std::endl;
    std::cout << "  " << programName << " ~/Pictures         # Browse a folder" << std::endl;
    std::cout << "  " << programName << " photo.jpg          # Show a file, browse its folder" << std::endl;
    std::cout << "  " << programName << " a.png b.mp4 c.pdf  # View exactly these files" << std::endl;
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    std::vector<std::string> pathsToOpen;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "UltraViewer version " << ULTRAVIEWER_VERSION << std::endl;
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
    UltraCanvasSingleInstance instance("org.ultraos.ultraviewer", "UltraViewer");
    const UltraCanvasSingleInstance::Start start =
        instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(pathsToOpen));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy shows them

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
        debugOutput << "=== UltraViewer - Universal Media Viewer ===" << std::endl;
        if (!app.Initialize("UltraViewer")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(
                NormalizePath(GetResourcesDir() + "media/appicon/UltraViewer.png"));
        // The framework's own dialogs, not the platform's: the toolbar's Open
        // and Save buttons get UltraCanvasFileDialog (folder tree, filer-widget
        // listing, view buttons), the same on every platform. Its message
        // boxes - a failed save, the replace prompt - follow the same setting.
        UltraCanvasDialogManager::SetUseNativeDialogs(false);
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraViewer");

        UltraViewerWindow mainWindow;
        // Listening starts before the command line's files are opened: a
        // file that cannot be read shows a native error dialog that holds the
        // start-up, and a start made meanwhile is still answered.
        //
        // A later start's files are shown here the way a drop on the viewer
        // shows them: the one window's viewer switches to them (a folder, a
        // file and its folder, or exactly those files). It replaces what was
        // on view - a viewer has nothing unsaved to lose - and the window
        // comes to the front. `shown` outlives the listener: Close() below
        // runs before mainWindow goes.
        UltraViewerWindow* shown = &mainWindow;
        instance.ListenOpen([shown](const std::vector<std::string>& paths) {
            shown->ShowPaths(paths);
            shown->BringForward();
        });

        if (!mainWindow.Initialize(pathsToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }
        mainWindow.Show();

        debugOutput << "=== UltraViewer Ready ===" << std::endl;
        app.Run();
        instance.Close();   // no request reaches the window once it starts to go
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Unhandled exception: ") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        HandleFatalError("Unknown exception occurred");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// ===== WINDOWS-SPECIFIC ENTRY POINT =====
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
#endif
