// Apps/UltraFiler/main.cpp
// UltraFiler - file manager application built on the UltraCanvas framework:
// folder tree (UltraCanvasTreeView) + folder content (UltraCanvasFilerWidget)
// + media preview (UltraCanvasMediaViewer) in a Windows Explorer style window.
// Version: 0.10.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasUtils.h"
#include "UltraFilerWindow.h"
#include "UltraFilerSettingsDialog.h"

// A file manager is judged on what it can show, and a format plugin shows
// nothing until the application registers it. UltraFiler registered none, so
// every format outside core - the whole Vector matrix (DXF, DWG and the rest)
// and every 3D format but STL - previewed as a type glyph and greyed itself
// out on the Display > Thumbnails and Display > Detail view pages, in builds
// that had the readers compiled in and sitting idle. Each plugin is built
// only when its CMake option is on, so each include is guarded.
#ifdef ULTRAFILER_HAS_VECTOR_PLUGIN
#include "UltraCanvasVectorFormatsPlugin.h"
#endif
#ifdef ULTRAFILER_HAS_MODELS_PLUGIN
#include "Models/UltraCanvasModelFormatsPlugin.h"
#endif
// Each viewer plugin puts its OWN directory on the include path of whatever
// links it, so these are unprefixed. Writing them as "CDR/..." worked only
// while the Vector plugin happened to be linked too - it is what contributes
// the Plugins/Vector directory these sit under - and the Vector plugin is off
// by default, so the prefixed form broke every build that did not ask for it.
#ifdef ULTRAFILER_HAS_CDR_PLUGIN
#include "UltraCanvasCDRPlugin.h"
#endif
#ifdef ULTRAFILER_HAS_XAR_PLUGIN
#include "UltraCanvasXARPlugin.h"
#endif
#ifdef ULTRAFILER_HAS_EPS_PLUGIN
#include "UltraCanvasEPSPlugin.h"
#endif

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
    debugOutput << "UltraFiler - File Manager powered by UltraCanvas Framework" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Usage: " << programName << " [options] [folder]" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Options:" << std::endl;
    debugOutput << "  -h, --help        Show this help message" << std::endl;
    debugOutput << "  -v, --version     Show version information" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Examples:" << std::endl;
    debugOutput << "  " << programName << "                  # Open the home folder" << std::endl;
    debugOutput << "  " << programName << " /home/user/Docs  # Open a specific folder" << std::endl;
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    std::string folderToOpen;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            debugOutput << "UltraFiler version " ULTRAFILER_VERSION << std::endl;
            debugOutput << "UltraCanvas Framework" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg[0] != '-') {
            folderToOpen = arg;
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
        debugOutput << "=== UltraFiler - File Manager ===" << std::endl;
        if (!app.Initialize("UltraFiler")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(
                NormalizePath(GetResourcesDir() + "media/appicon/UltraFiler.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);

        // Before the window: the settings pages read what this build can show
        // when they are first built, and the Filer's format list is what
        // decides which switches are live.
#ifdef ULTRAFILER_HAS_VECTOR_PLUGIN
        RegisterVectorFormatsPlugin();
#endif
#ifdef ULTRAFILER_HAS_MODELS_PLUGIN
        RegisterModelFormatsPlugin();
#endif
        // The dedicated viewer plugins go AFTER the Vector plugin, never
        // before: both read some of the same extensions, the registry's last
        // registration owns them, and for those the viewers are the better
        // reader - libcdr parses CorelDRAW files no converter here writes,
        // the XAR plugin covers the compressed Xara files the converter's
        // reader does not, and the EPS plugin interprets PostScript rather
        // than looking for a preview bitmap in it. The Vector plugin keeps
        // what only it reads (DXF, the DWG family, EMF, WMF) and stays the
        // only writer, since saving matches on GetSaveExtensions instead.
#ifdef ULTRAFILER_HAS_CDR_PLUGIN
        RegisterCDRPlugin();
#endif
#ifdef ULTRAFILER_HAS_XAR_PLUGIN
        RegisterXARPlugin();
#endif
#ifdef ULTRAFILER_HAS_EPS_PLUGIN
        RegisterEPSPlugin();
#endif

        UltraFilerWindow mainWindow;
        if (!mainWindow.Initialize(folderToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }
        mainWindow.Show();

        debugOutput << "=== UltraFiler Ready ===" << std::endl;
        app.Run();
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
