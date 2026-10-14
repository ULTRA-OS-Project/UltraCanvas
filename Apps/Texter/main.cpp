// Apps/UltraTexter/main.cpp
// UltraTexter - Standalone Text Editor Application with Multi-Window Support
// One copy per user: a second start hands its files to the running copy,
// which opens them as File > Open does, and quits
// (UltraCanvasSingleInstance.h).
// Version: 2.1.0 - one UltraTexter per user: a second start's files open in
//                  the running copy; every file named on the command line
//                  opens, not only the last
// Version: 2.0.3
// Last Modified: 2026-10-14
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

// OS-specific includes
#ifdef _WIN32
#include <windows.h>
    #include <crtdbg.h>
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <signal.h>
#include "UltraCanvasDebug.h"
#endif

using namespace UltraCanvas;

// ===== FORWARD DECLARATIONS =====
class TexterWindowManager;

// ===== GLOBAL APPLICATION STATE =====
static TexterWindowManager* g_windowManager = nullptr;

// ===== WINDOW MANAGER =====
class TexterWindowManager {
private:
    UltraCanvasApplication* app;
    std::vector<std::shared_ptr<UltraCanvasTextEditor>> editors;
    TextEditorConfig baseConfig;
    bool useDarkTheme;
    // The window the user worked in last: where handed-over files go.
    std::weak_ptr<UltraCanvasTextEditor> lastActiveEditor;

    void WireEditorCallbacks(std::shared_ptr<UltraCanvasTextEditor> editor) {
        auto* editorPtr = editor.get();
        std::weak_ptr<UltraCanvasTextEditor> weakEditor = editor;

        // Allow auto-close when last document is closed and other windows exist
        editorPtr->canCloseEmptyWindow = [this]() {
            return static_cast<int>(editors.size()) > 1;
        };

        // New Window menu item
        editorPtr->onNewWindowRequest = [this]() {
            CreateNewWindow();
        };

        // Tab dragged out: move to target window or create a new one
        editorPtr->onTabDraggedOut = [this, editorPtr](std::shared_ptr<DocumentTab> doc, int screenX, int screenY) {
            auto target = FindEditorAtScreenPoint(screenX, screenY, editorPtr);
            if (target) {
                target->AcceptDocument(doc);
            } else {
                CreateWindowWithDocument(doc, screenX, screenY);
            }

            if (editorPtr->GetDocumentCount() == 0) {
                editorPtr->PerformClose();
            }
        };

        // Theme toggled: sync to all other windows
        editorPtr->onThemeChanged = [this, editorPtr](bool isDark) {
            useDarkTheme = isDark;
            for (auto& ed : editors) {
                if (ed.get() == editorPtr) continue;
                if (isDark) {
                    ed->ApplyDarkTheme();
                } else {
                    ed->ApplyLightTheme();
                }
                ed->RequestRedraw();
            }
        };

        // Focus: remember the window the user works in
        editorPtr->onWindowFocus = [this, weakEditor]() {
            lastActiveEditor = weakEditor;
        };

        // File saved
        editorPtr->onFileSaved = [](const std::string& path, int tabIndex) {
            debugOutput << "File saved: " << path << std::endl;
        };

        // Window close: remove from manager
        editorPtr->onWindowClosed = [this, weakEditor]() {
            auto ed = weakEditor.lock();
            if (!ed) return;

            auto it = std::find(editors.begin(), editors.end(), ed);
            if (it != editors.end()) {
                editors.erase(it);
            }
        };
    }

    // Find which editor window (if any) is at the given screen coordinates,
    // excluding the source editor.
    std::shared_ptr<UltraCanvasTextEditor> FindEditorAtScreenPoint(int screenX, int screenY,
                                                                    UltraCanvasTextEditor* exclude) {
        for (auto& ed : editors) {
            if (ed.get() == exclude) continue;

            int wx, wy;
            ed->GetWindowPosition(wx, wy);
            int ww = ed->GetWidth();
            int wh = ed->GetHeight();

            if (screenX >= wx && screenX < wx + ww &&
                screenY >= wy && screenY < wy + wh) {
                return ed;
            }
        }
        return nullptr;
    }

public:
    TexterWindowManager(UltraCanvasApplication* appPtr,
                        const TextEditorConfig& config, bool dark)
        : app(appPtr), baseConfig(config), useDarkTheme(dark) {}

    std::shared_ptr<UltraCanvasTextEditor> CreateNewWindow(int screenX = -1, int screenY = -1) {
        auto editor = CreateTextEditor(baseConfig);

        if (!editor) {
            debugOutput << "Failed to create text editor window" << std::endl;
            return nullptr;
        }

        // Apply theme
        if (useDarkTheme) {
            editor->ApplyDarkTheme();
        }

        // Wire callbacks
        WireEditorCallbacks(editor);

        editors.push_back(editor);
        return editor;
    }

    std::shared_ptr<UltraCanvasTextEditor> CreateWindowWithDocument(
            std::shared_ptr<DocumentTab> doc, int screenX, int screenY) {
        auto editor = CreateNewWindow(screenX, screenY);
        if (!editor) return nullptr;

        // The constructor created an empty "Untitled" tab — extract it
        // so only the transferred document remains
        auto emptyDoc = editor->ExtractDocument(0);

        // Accept the transferred document
        editor->AcceptDocument(doc);

        return editor;
    }

    int GetWindowCount() const { return static_cast<int>(editors.size()); }

    // Files a later start handed over (UltraCanvasSingleInstance::ListenOpen),
    // absolute and UTF-8: opened as File > Open opens several - each in a tab
    // of the window the user worked in last - except that a file already open
    // in any window is switched to there instead of opened a second time. The
    // window that shows the last of them comes forward.
    void OpenHandedOverFiles(const std::vector<std::string>& paths) {
        std::shared_ptr<UltraCanvasTextEditor> target = lastActiveEditor.lock();
        if (!target || std::find(editors.begin(), editors.end(), target) == editors.end()) {
            target = editors.empty() ? nullptr : editors.front();
        }
        if (!target) target = CreateNewWindow();
        if (!target) return;

        std::shared_ptr<UltraCanvasTextEditor> shown;
        for (const auto& path : paths) {
            std::shared_ptr<UltraCanvasTextEditor> editor = target;
            for (const auto& ed : editors) {
                if (ed->HasOpenFile(path)) {
                    editor = ed;
                    break;
                }
            }
            debugOutput << "Opening handed-over file: " << path << std::endl;
            if (editor->OpenFile(path) >= 0) shown = editor;
        }
        if (!shown) shown = target;
        if (shown->IsMinimized()) shown->Restore();
        shown->RaiseAndFocus();
    }
};

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
bool InitializeSystem(UltraCanvasApplication& app, const std::string& appName) {
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

        if (!app.Initialize(appName)) {
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
    std::cout << "  -d, --dark        Start with dark theme" << std::endl;
    std::cout << "  -l, --lang LANG   Set syntax highlighting language" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                    # Start with empty document" << std::endl;
    std::cout << "  " << programName << " myfile.cpp         # Open myfile.cpp" << std::endl;
    std::cout << "  " << programName << " -d myfile.py       # Open with dark theme" << std::endl;
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
        if (!InitializeSystem(app, "UltraTexter")) {
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
        editorConfig.darkTheme = useDarkTheme;

        if (!language.empty()) {
            editorConfig.defaultLanguage = language;
        }

        // Create window manager
        TexterWindowManager windowManager(&app, editorConfig, useDarkTheme);
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
