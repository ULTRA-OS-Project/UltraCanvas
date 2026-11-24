// Apps/UltraTexter/main.cpp
// UltraTexter - Standalone Text Editor Application with Multi-Window Support
// One copy per user: a second start hands its files to the running copy,
// which opens them as File > Open does, and quits
// (UltraCanvasSingleInstance.h).
// Version: 2.4.0 - the window manager is TexterWindowManager.h, which MobilePC starts Texter with
// Version: 2.3.0 - the look is the theme's: --dark is ThemePolicy::Dark for the run; no theme sync across windows
// Version: 2.2.0 - the start-up messages go through Tr; the application's name
//                  is given to Initialize as a literal, the name of its catalogue
// Version: 2.1.0 - one UltraTexter per user: a second start's files open in
//                  the running copy; every file named on the command line
//                  opens, not only the last
// Version: 2.0.3
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include <iostream>
#include <memory>
#include <chrono>
#include <exception>
#include <string>
#include <vector>
#include <algorithm>

// UltraCanvas Core Headers
#include "UltraCanvasApplication.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasTextEditor.h"
#include "UltraCanvasSplashScreen.h"
#include "TexterWindowManager.h"

// OS-specific includes
#ifdef _WIN32
#include <windows.h>
    #include <crtdbg.h>
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <signal.h>
#include "UltraCanvasDebug.h"
#include "UltraLocalisation/UltraLocalisation.h"
#endif

using namespace UltraCanvas;

// ===== GLOBAL APPLICATION STATE =====
static TexterWindowManager* g_windowManager = nullptr;

// ===== ERROR HANDLING =====
void HandleFatalError(const std::string& error) {
    debugOutput << "FATAL ERROR: " << error << std::endl;

#ifdef _WIN32
    MessageBoxA(nullptr, error.c_str(), "UltraTexter - Fatal Error", MB_ICONERROR | MB_OK);
#endif

    std::exit(EXIT_FAILURE);
}

// ===== SIGNAL HANDLERS =====
#ifdef __linux__
// The one call a signal handler may make: it sets a flag the main loop
// turns into RequestExit, so main returns and the destructors run in order.
void SignalHandler(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

// ===== SYSTEM INITIALIZATION =====
bool InitializeSystem(UltraCanvasApplication& app) {
    debugOutput << "=== UltraTexter - Text Editor ===" << std::endl;
    debugOutput << "Version: 2.0.0" << std::endl;
    debugOutput << "Build Date: " << __DATE__ << " " << __TIME__ << std::endl;
    debugOutput << "Platform: ";

#ifdef _WIN32
    debugOutput << "Windows";
    #ifdef _DEBUG
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        debugOutput << " (Debug Build - Memory Leak Detection Enabled)";
    #endif
#elif __linux__
    debugOutput << "Linux";

    // Setup signal handlers for graceful shutdown
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Initialize X11 threading support
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#elif __APPLE__
    debugOutput << "macOS";
#else
    debugOutput << "Unknown";
#endif

    debugOutput << std::endl << std::endl;

    try {
        debugOutput << "Initializing UltraCanvas framework..." << std::endl;

        // The literal is the application's name in the locale catalogues too
        // (scripts/extract_ui_strings.py reads it): media/locale/<lang>/UltraTexter.json.
        if (!app.Initialize("UltraTexter")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return false;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir()+"media/appicon/Texter.png"));

        debugOutput << "✓ UltraCanvas framework initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Framework initialization failed: ") + e.what());
        return false;
    }

    UltraCanvasDialogManager::SetUseNativeDialogs(true);

    return true;
}

// ===== SHUTDOWN =====
void ShutdownSystem() {
    debugOutput << std::endl << "Shutting down UltraTexter..." << std::endl;
    debugOutput << "✓ UltraTexter shut down complete" << std::endl;
}

// ===== PRINT USAGE =====
void PrintUsage(const char* programName) {
    std::cout << "UltraTexter - Text Editor powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [file...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -v, --version     Show version information" << std::endl;
    std::cout << "  -d, --dark        Start with the dark look for this run (the desktop's look otherwise)" << std::endl;
    std::cout << "  -l, --lang LANG   Set syntax highlighting language" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                    # Start with empty document" << std::endl;
    std::cout << "  " << programName << " myfile.cpp         # Open myfile.cpp" << std::endl;
    std::cout << "  " << programName << " -d myfile.py       # Open with the dark look" << std::endl;
    std::cout << "  " << programName << " -l Python script   # Open 'script' with Python highlighting" << std::endl;
    std::cout << std::endl;
    std::cout << "While UltraTexter is running, starting it again opens the files in the" << std::endl;
    std::cout << "running copy (the options apply to a first start only)." << std::endl;
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    debugOutput << std::endl;

    // Parse command line arguments
    std::vector<std::string> filesToOpen;
    std::string language;
    bool useDarkTheme = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "UltraTexter version " ULTRATEXTER_VERSION << std::endl;
            std::cout << "UltraCanvas Framework" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg == "--dark" || arg == "-d") {
            useDarkTheme = true;
        } else if (arg == "--lang" || arg == "-l") {
            if (i + 1 < argc) {
                language = argv[++i];
            } else {
                std::cerr << "Error: --lang requires a language name" << std::endl;
                return EXIT_FAILURE;
            }
        } else if (arg[0] != '-') {
            // Assume it's a file path
            filesToOpen.push_back(arg);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // One UltraTexter per user. --help and --version above print and quit
    // without it. A later start hands its files to this copy; its --dark and
    // --lang are not handed over - they choose how a first start sets itself
    // up, and the running copy keeps the theme and languages it has.
    // Outlives app.Run(), so it sits outside the try.
    UltraCanvasSingleInstance instance("org.ultraos.ultratexter", "UltraTexter");
    const UltraCanvasSingleInstance::Start start =
            instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(filesToOpen));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy opens them

    // Create application instance
    UltraCanvasApplication app;

    try {
        // Initialize system
        if (!InitializeSystem(app)) {
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning) {
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraTexter");
        }

        // Create editor configuration
        TextEditorConfig editorConfig;
        editorConfig.title = "UltraTexter " + UltraCanvasTextEditor::version;
        editorConfig.showMenuBar = true;
        editorConfig.showToolbar = true;
        editorConfig.showStatusBar = true;
        editorConfig.showLineNumbers = true;
        // --dark: the framework's dark preset with the desktop's fonts for
        // this run; otherwise the desktop's look, and Appearance.conf over
        // it, which View > Theme writes.
        if (useDarkTheme) app.SetThemePolicy(ThemePolicy::Dark);

        if (!language.empty()) {
            editorConfig.defaultLanguage = language;
        }

        // Create window manager
        TexterWindowManager windowManager(&app, editorConfig);
        g_windowManager = &windowManager;

        // Create the first window
        debugOutput << "Creating main window..." << std::endl;
        auto firstWindow = windowManager.CreateNewWindow();
        if (!firstWindow) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }

        // A later start's files open here; a later start without files
        // brings the window forward. Listening starts before the splash, the
        // crash-recovery question and the command line's files, any of which
        // can hold the start-up: a start made meanwhile is still answered,
        // and its files open once the window is ready.
        instance.ListenOpen([&windowManager](const std::vector<std::string>& paths) {
            windowManager.OpenHandedOverFiles(paths);
        });

        // Show splash screen
        UltraCanvasSplashScreen splash;
        SplashScreenConfig splashConfig;
        splashConfig.width = 500;
        splashConfig.height = 500;
        splashConfig.imagePath = NormalizePath(GetResourcesDir() + "media/appicon/Texter.png");
        splashConfig.title = "UltraTexter";
        splashConfig.version = UltraCanvasTextEditor::version;
        splashConfig.websiteURL = "https://www.ultraos.eu/";
        splashConfig.websiteDisplay = "www.ultraos.eu";
        splashConfig.showTimeout = 2000;

        // Phase 1 runs synchronously before Show(): session tabs are already
        // in place when the splash fades, rather than popping in afterwards.
        firstWindow->RestoreSessionDocuments();

        // Phase 2 fires when the splash closes (timeout or click). The recovery
        // dialog can then take focus without the always-on-top splash competing
        // with it (the previous code worked around this with a 100ms timer).
        splash.onSplashClosed = [firstWindow]() {
            firstWindow->PromptCrashRecovery();
        };

        splash.Show(splashConfig, firstWindow.get());

        debugOutput << "✓ Main window created" << std::endl;

        // Fallback: if the splash never became visible (e.g. resource load
        // failed and Show reset the window), onSplashClosed will not fire,
        // so run Phase 2 inline.
        if (!splash.IsVisible()) {
            firstWindow->PromptCrashRecovery();
        }

        // Open the files from the command line
        for (const auto& fileToOpen : filesToOpen) {
            debugOutput << "Opening file: " << fileToOpen << std::endl;
            if (firstWindow->OpenFile(fileToOpen) >= 0) {
                debugOutput << "✓ File loaded successfully" << std::endl;
            } else {
                debugOutput << "Warning: Failed to load file: " << fileToOpen << std::endl;
            }
        }

        debugOutput << std::endl;
        debugOutput << "=== UltraTexter Ready ===" << std::endl;
        debugOutput << "• Use File menu for New/Open/Save operations" << std::endl;
        debugOutput << "• Use Edit menu for text editing operations" << std::endl;
        debugOutput << "• Drag tabs out to create new windows" << std::endl;
        debugOutput << "• Close the window or use File > Quit to exit" << std::endl;
        debugOutput << std::endl;

        // Run application main loop
        app.Run();
        instance.Close();   // no request reaches the windows once they start to go

    } catch (const std::exception& e) {
        HandleFatalError(std::string("Unhandled exception: ") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        HandleFatalError("Unknown exception occurred");
        return EXIT_FAILURE;
    }

    // Clean shutdown
    g_windowManager = nullptr;
    ShutdownSystem();
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
