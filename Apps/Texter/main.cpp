// Apps/UltraTexter/main.cpp
// UltraTexter - Standalone Text Editor Application with Multi-Window Support
// Version: 2.0.0
// Last Modified: 2026-03-05
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
static UltraCanvasApplication* g_app = nullptr;
static TexterWindowManager* g_windowManager = nullptr;

// ===== WINDOW MANAGER =====
class TexterWindowManager {
private:
    UltraCanvasApplication* app;
    std::vector<std::shared_ptr<UltraCanvasTextEditor>> editors;
    TextEditorConfig baseConfig;
    bool useDarkTheme;

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
                editorPtr->Close();
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

        // File saved
        editorPtr->onFileSaved = [](const std::string& path, int tabIndex) {
            debugOutput << "File saved: " << path << std::endl;
        };

        // Window close: remove from manager
        editorPtr->SetWindowCloseCallback([this, weakEditor]() {
            auto ed = weakEditor.lock();
            if (!ed) return;

            auto it = std::find(editors.begin(), editors.end(), ed);
            if (it != editors.end()) {
                editors.erase(it);
            }
        });
    }

    // Find which editor window (if any) is at the given screen coordinates,
    // excluding the source editor.
    std::shared_ptr<UltraCanvasTextEditor> FindEditorAtScreenPoint(int screenX, int screenY,
                                                                    UltraCanvasTextEditor* exclude) {
        for (auto& ed : editors) {
            if (ed.get() == exclude) continue;

            int wx, wy;
            ed->GetScreenPosition(wx, wy);
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
void SignalHandler(int signal) {
    debugOutput << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;

    if (g_app) {
        g_app->RequestExit();
    }

    std::exit(EXIT_SUCCESS);
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
        app.SetDefaultWindowIcon(GetResourcesDir()+"media/appicons/Texter.png");

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
    debugOutput << "UltraTexter - Text Editor powered by UltraCanvas Framework" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Usage: " << programName << " [options] [file]" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Options:" << std::endl;
    debugOutput << "  -h, --help        Show this help message" << std::endl;
    debugOutput << "  -v, --version     Show version information" << std::endl;
    debugOutput << "  -d, --dark        Start with dark theme" << std::endl;
    debugOutput << "  -l, --lang LANG   Set syntax highlighting language" << std::endl;
    debugOutput << std::endl;
    debugOutput << "Examples:" << std::endl;
    debugOutput << "  " << programName << "                    # Start with empty document" << std::endl;
    debugOutput << "  " << programName << " myfile.cpp         # Open myfile.cpp" << std::endl;
    debugOutput << "  " << programName << " -d myfile.py       # Open with dark theme" << std::endl;
    debugOutput << "  " << programName << " -l Python script   # Open 'script' with Python highlighting" << std::endl;
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    debugOutput << std::endl;

    // Parse command line arguments
    std::string fileToOpen;
    std::string language;
    bool useDarkTheme = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            debugOutput << "UltraTexter version 2.0.0" << std::endl;
            debugOutput << "UltraCanvas Framework" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg == "--dark" || arg == "-d") {
            useDarkTheme = true;
        } else if (arg == "--lang" || arg == "-l") {
            if (i + 1 < argc) {
                language = argv[++i];
            } else {
                debugOutput << "Error: --lang requires a language name" << std::endl;
                return EXIT_FAILURE;
            }
        } else if (arg[0] != '-') {
            // Assume it's a file path
            fileToOpen = arg;
        } else {
            debugOutput << "Unknown argument: " << arg << std::endl;
            debugOutput << "Use --help for usage information" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Create application instance
    UltraCanvasApplication app;
    g_app = &app;

    try {
        // Initialize system
        if (!InitializeSystem(app, "UltraTexter")) {
            return EXIT_FAILURE;
        }

        // Create editor configuration
        TextEditorConfig editorConfig;
        editorConfig.title = "UltraTexter";
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

        // Show splash screen
        UltraCanvasSplashScreen splash;
        SplashScreenConfig splashConfig;
        splashConfig.width = 500;
        splashConfig.height = 500;
        splashConfig.imagePath = GetResourcesDir() + "media/appicon/Texter.png";
        splashConfig.title = "UltraTexter";
        splashConfig.version = "1.0.12";
        splashConfig.websiteURL = "https://www.ultraos.eu/";
        splashConfig.websiteDisplay = "www.ultraos.eu";
        splashConfig.showTimeout = std::chrono::milliseconds(2000);
        splash.onSplashClosed = [firstWindow]() {
            firstWindow->RestoreSessionAndRecoverBackups();
        };
        splash.Show(splashConfig);


        debugOutput << "✓ Main window created" << std::endl;

        // Close splash screen now that main window is ready
        //splash.Close();

        // Open file from command line
        if (!fileToOpen.empty()) {
            debugOutput << "Opening file: " << fileToOpen << std::endl;
            if (firstWindow->OpenFile(fileToOpen)) {
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
