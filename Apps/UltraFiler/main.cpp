// Apps/UltraFiler/main.cpp
// UltraFiler - file manager application built on the UltraCanvas framework:
// folder tree (UltraCanvasTreeView) + folder content (UltraCanvasFilerWidget)
// + media preview (UltraCanvasMediaViewer) in a Windows Explorer style window.
// One copy per user: a second start hands its folder to the running copy,
// which opens it in a new tab, and quits (UltraCanvasSingleInstance.h).
// Version: 0.13.0 - one UltraFiler per user: a second start's folder opens in
//                  a new tab of the running copy; the elevated helper never
//                  claims
// Version: 0.12.1
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
#include "UltraCanvasElevatedFileOperations.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraFilerWindow.h"
#include "UltraFilerSettingsDialog.h"

// Every format plugin this build produced registers itself before main(),
// because the executable links UltraCanvasAllFormats - no list here, and no
// list to update when a plugin is added to the framework. See
// UltraCanvas/Plugins/UltraCanvasAllFormats.h.

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
    MessageBoxA(nullptr, error.c_str(), "UltraFiler - Fatal Error", MB_ICONERROR | MB_OK);
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
    std::cout << "UltraFiler - File Manager powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [folder]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -v, --version     Show version information" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                  # Open the home folder" << std::endl;
    std::cout << "  " << programName << " /home/user/Docs  # Open a specific folder" << std::endl;
    std::cout << std::endl;
    std::cout << "While UltraFiler is running, starting it again opens the folder in a new" << std::endl;
    std::cout << "tab of the running window." << std::endl;
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    // Started by our own "Delete as administrator" retry? That is a second
    // copy of this executable, launched elevated through the UAC prompt with
    // the helper flag on its command line: it deletes what the user
    // consented to and exits here, before any window exists. Calling this
    // is also what lets the Filer widget offer the retry at all.
    // The helper is started BY the running UltraFiler, so it must never
    // claim the one copy per user below: it would find that copy and hand
    // itself over instead of deleting anything.
    int helperExit = 0;
    if (ElevatedFileOperations::RunHelperIfRequested(argc, argv, helperExit))
        return helperExit;

    std::string folderToOpen;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "UltraFiler version " ULTRAFILER_VERSION << std::endl;
            std::cout << "UltraCanvas Framework" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg[0] != '-') {
            folderToOpen = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // One UltraFiler per user. The helper above and --help / --version print
    // or work and quit without it. A later start hands its folder to this
    // copy, which opens it in a new tab; one without a folder brings the
    // window forward. Outlives app.Run(), so it sits outside the try.
    UltraCanvasSingleInstance instance("org.ultraos.ultrafiler", "UltraFiler");
    const UltraCanvasSingleInstance::Start start = instance.Claim(
            UltraCanvasSingleInstance::MakeOpenRequest({folderToOpen}));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy shows it

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
        debugOutput << "=== UltraFiler - File Manager ===" << std::endl;
        if (!app.Initialize("UltraFiler")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraFiler");
        app.SetDefaultWindowIcon(
                NormalizePath(GetResourcesDir() + "media/appicon/UltraFiler.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);
        // Round indicators only: a checkbox entry of any menu - the file
        // display's own, the folder tree's, the Extras the window adds - is
        // drawn in a circle like a radio entry, with its tick inside.
        SetDefaultMenuCheckboxShape(MenuCheckboxShape::Round);

        UltraFilerWindow mainWindow;
        // Listening starts before the command line's files are opened: a
        // file that cannot be read shows a native error dialog that holds the
        // start-up, and a start made meanwhile is still answered.
        //
        // A later start's folder opens in a new tab here; a later start
        // without one brings the window forward.
        instance.ListenOpen([&mainWindow](const std::vector<std::string>& paths) {
            mainWindow.OpenFolders(paths);
        });

        if (!mainWindow.Initialize(folderToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }
        mainWindow.Show();

        debugOutput << "=== UltraFiler Ready ===" << std::endl;
        app.Run();
        instance.Close();   // no request reaches the window once it starts to go
        // Tear down the retained settings-dialog widget tree while `app` (and
        // thus the Application singleton) is still alive, instead of at static
        // destruction after main() returns — see UltraFilerSettingsDialog::Shutdown.
        UltraFilerSettingsDialog::Shutdown();
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
