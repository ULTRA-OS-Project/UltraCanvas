// Apps/UltraTexter/main.cpp
// UltraTexter - Standalone Text Editor Application with Multi-Window Support
// Version: 2.0.0
// Last Modified: 2026-03-05
// Author: UltraCanvas Framework

#include <iostream>
#include <memory>
#include <exception>
#include <string>
#include <vector>
#include <algorithm>

// UltraCanvas Core Headers
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTextEditor.h"
#include "UltraCanvasModalDialog.h"

// OS-specific includes
#ifdef _WIN32
#include <windows.h>
    #include <crtdbg.h>
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <signal.h>
#endif

using namespace UltraCanvas;

// ===== FORWARD DECLARATIONS =====
class TexterWindowManager;

// ===== GLOBAL APPLICATION STATE =====
static UltraCanvasApplication* g_app = nullptr;
static TexterWindowManager* g_windowManager = nullptr;

// ===== TEXTER WINDOW =====
struct TexterWindow {
    std::shared_ptr<UltraCanvasWindow> window;
    std::shared_ptr<UltraCanvasTextEditor> editor;
};

// ===== WINDOW MANAGER =====
class TexterWindowManager {
private:
    UltraCanvasApplication* app;
    std::vector<std::shared_ptr<TexterWindow>> windows;
    TextEditorConfig baseConfig;
    bool useDarkTheme;
    int nextWindowId = 0;

    void WireWindowCallbacks(std::shared_ptr<TexterWindow> tw) {
        auto* editor = tw->editor.get();
        auto* win = tw->window.get();
        std::weak_ptr<TexterWindow> weakTw = tw;

        // Quit request: close this window
        editor->onQuitRequest = [win]() {
            win->Close();
        };

        // Allow auto-close when last document is closed and other windows exist
        editor->canCloseEmptyWindow = [this]() {
            return static_cast<int>(windows.size()) > 1;
        };

        // New Window menu item
        editor->onNewWindowRequest = [this]() {
            CreateNewWindow();
        };

        // Tab dragged out: move to target window or create a new one
        editor->onTabDraggedOut = [this, editor](std::shared_ptr<DocumentTab> doc, int screenX, int screenY) {
            // Check if drop landed on another TexterWindow
            auto target = FindWindowAtScreenPoint(screenX, screenY, editor);
            if (target) {
                // Drag-in: move the tab to the target window
                target->editor->AcceptDocument(doc);
                UpdateWindowTitle(target.get());
            } else {
                // No target window at drop point — create a new one
                CreateWindowWithDocument(doc, screenX, screenY);
            }

            // Close source window if it has no more documents
            if (editor->GetDocumentCount() == 0) {
                for (auto& tw : windows) {
                    if (tw->editor.get() == editor) {
                        tw->window->Close();
                        break;
                    }
                }
            }
        };

        // Theme toggled: sync to all other windows
        editor->onThemeChanged = [this, editor](bool isDark) {
            useDarkTheme = isDark;
            for (auto& tw : windows) {
                if (tw->editor.get() == editor) continue;
                if (isDark) {
                    tw->editor->ApplyDarkTheme();
                } else {
                    tw->editor->ApplyLightTheme();
                }
                tw->window->SetBackgroundColor(
                    isDark ? Color(30, 30, 30) : Color(240, 240, 240));
                tw->window->RequestRedraw();
            }
        };

        // File loaded: update window title
        editor->onFileLoaded = [win](const std::string& path, int tabIndex) {
            win->SetWindowTitle("UltraTexter - " + path);
        };

        // Modified state changed: update window title
        editor->onModifiedChange = [win, editor](bool modified, int tabIndex) {
            std::string title = "UltraTexter";
            std::string filePath = editor->GetActiveFilePath();
            if (!filePath.empty()) {
                title += " - " + filePath;
            }
            if (modified) {
                title += " *";
            }
            win->SetWindowTitle(title);
        };

        // Tab changed: update window title
        editor->onTabChanged = [win, editor](int tabIndex) {
            std::string title = "UltraTexter";
            std::string filePath = editor->GetActiveFilePath();
            if (!filePath.empty()) {
                title += " - " + filePath;
            }
            if (editor->HasUnsavedChanges()) {
                title += " *";
            }
            win->SetWindowTitle(title);
        };

        // File saved
        editor->onFileSaved = [](const std::string& path, int tabIndex) {
            std::cerr << "File saved: " << path << std::endl;
        };

        // Window resize: sync editor size
        win->SetWindowResizeCallback([editor](int width, int height) {
            editor->SetSize(width, height);
        });

        // Window close: remove from manager
        win->SetWindowCloseCallback([this, weakTw]() {
            auto tw = weakTw.lock();
            if (!tw) return;

            auto it = std::find(windows.begin(), windows.end(), tw);
            if (it != windows.end()) {
                windows.erase(it);
            }
        });
    }

    // Find which TexterWindow (if any) is at the given screen coordinates,
    // excluding the source editor's window.
    std::shared_ptr<TexterWindow> FindWindowAtScreenPoint(int screenX, int screenY,
                                                           UltraCanvasTextEditor* exclude) {
        for (auto& tw : windows) {
            if (tw->editor.get() == exclude) continue;

            int wx, wy;
            tw->window->GetScreenPosition(wx, wy);
            int ww = tw->window->GetWidth();
            int wh = tw->window->GetHeight();

            if (screenX >= wx && screenX < wx + ww &&
                screenY >= wy && screenY < wy + wh) {
                return tw;
            }
        }
        return nullptr;
    }

    void UpdateWindowTitle(TexterWindow* tw) {
        std::string title = "UltraTexter";
        std::string filePath = tw->editor->GetActiveFilePath();
        if (!filePath.empty()) {
            title += " - " + filePath;
        }
        if (tw->editor->HasUnsavedChanges()) {
            title += " *";
        }
        tw->window->SetWindowTitle(title);
    }

public:
    TexterWindowManager(UltraCanvasApplication* appPtr,
                        const TextEditorConfig& config, bool dark)
        : app(appPtr), baseConfig(config), useDarkTheme(dark) {}

    std::shared_ptr<TexterWindow> CreateNewWindow(int screenX = -1, int screenY = -1) {
        auto tw = std::make_shared<TexterWindow>();

        // Create window
        tw->window = std::make_shared<UltraCanvasWindow>();

        WindowConfig windowConfig;
        windowConfig.title = "UltraTexter";
        windowConfig.width = 1280;
        windowConfig.height = 800;
        windowConfig.backgroundColor = useDarkTheme ? Color(30, 30, 30) : Color(240, 240, 240);
        windowConfig.deleteOnClose = true;

        if (!tw->window->Create(windowConfig)) {
            std::cerr << "Failed to create window" << std::endl;
            return nullptr;
        }

        // Create editor
        int windowId = nextWindowId++;
        tw->editor = CreateTextEditor(
            "Editor_" + std::to_string(windowId),
            windowId,
            0, 0,
            windowConfig.width,
            windowConfig.height,
            baseConfig
        );

        if (!tw->editor) {
            std::cerr << "Failed to create text editor" << std::endl;
            return nullptr;
        }

        // Apply theme
        if (useDarkTheme) {
            tw->editor->ApplyDarkTheme();
        }

        // Wire callbacks
        WireWindowCallbacks(tw);

        // Add editor to window and show
        tw->window->AddChild(tw->editor);
        tw->window->Show();

        windows.push_back(tw);
        return tw;
    }

    std::shared_ptr<TexterWindow> CreateWindowWithDocument(
            std::shared_ptr<DocumentTab> doc, int screenX, int screenY) {
        auto tw = CreateNewWindow(screenX, screenY);
        if (!tw) return nullptr;

        // The constructor created an empty "Untitled" tab — extract it
        // so only the transferred document remains
        auto emptyDoc = tw->editor->ExtractDocument(0);

        // Accept the transferred document
        tw->editor->AcceptDocument(doc);

        UpdateWindowTitle(tw.get());
        return tw;
    }

    int GetWindowCount() const { return static_cast<int>(windows.size()); }
};

// ===== ERROR HANDLING =====
void HandleFatalError(const std::string& error) {
    std::cerr << "FATAL ERROR: " << error << std::endl;

#ifdef _WIN32
    MessageBoxA(nullptr, error.c_str(), "UltraTexter - Fatal Error", MB_ICONERROR | MB_OK);
#endif

    std::exit(EXIT_FAILURE);
}

// ===== SIGNAL HANDLERS =====
#ifdef __linux__
void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;

    if (g_app) {
        g_app->RequestExit();
    }

    std::exit(EXIT_SUCCESS);
}
#endif

// ===== SYSTEM INITIALIZATION =====
bool InitializeSystem(UltraCanvasApplication& app, const std::string& appName) {
    std::cout << "=== UltraTexter - Text Editor ===" << std::endl;
    std::cout << "Version: 2.0.0" << std::endl;
    std::cout << "Build Date: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "Platform: ";

#ifdef _WIN32
    std::cout << "Windows";
    #ifdef _DEBUG
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        std::cout << " (Debug Build - Memory Leak Detection Enabled)";
    #endif
#elif __linux__
    std::cout << "Linux";

    // Setup signal handlers for graceful shutdown
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Initialize X11 threading support
    if (!XInitThreads()) {
        std::cerr << "Warning: X11 threading initialization failed" << std::endl;
    }
#elif __APPLE__
    std::cout << "macOS";
#else
    std::cout << "Unknown";
#endif

    std::cout << std::endl << std::endl;

    try {
        std::cout << "Initializing UltraCanvas framework..." << std::endl;

        if (!app.Initialize(appName)) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return false;
        }

        std::cout << "✓ UltraCanvas framework initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Framework initialization failed: ") + e.what());
        return false;
    }

    UltraCanvasDialogManager::SetUseNativeDialogs(true);

    return true;
}

// ===== SHUTDOWN =====
void ShutdownSystem() {
    std::cout << std::endl << "Shutting down UltraTexter..." << std::endl;
    std::cout << "✓ UltraTexter shut down complete" << std::endl;
}

// ===== PRINT USAGE =====
void PrintUsage(const char* programName) {
    std::cout << "UltraTexter - Text Editor powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [file]" << std::endl;
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
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    std::cout << std::endl;

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
            std::cout << "UltraTexter version 2.0.0" << std::endl;
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
            fileToOpen = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cout << "Use --help for usage information" << std::endl;
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
        std::cout << "Creating main window..." << std::endl;
        auto firstWindow = windowManager.CreateNewWindow();
        if (!firstWindow) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }
        std::cout << "✓ Main window created" << std::endl;

        // Open file from command line
        if (!fileToOpen.empty()) {
            std::cout << "Opening file: " << fileToOpen << std::endl;
            if (firstWindow->editor->OpenFile(fileToOpen)) {
                std::cout << "✓ File loaded successfully" << std::endl;
            } else {
                std::cerr << "Warning: Failed to load file: " << fileToOpen << std::endl;
            }
        }

        std::cout << std::endl;
        std::cout << "=== UltraTexter Ready ===" << std::endl;
        std::cout << "• Use File menu for New/Open/Save operations" << std::endl;
        std::cout << "• Use Edit menu for text editing operations" << std::endl;
        std::cout << "• Drag tabs out to create new windows" << std::endl;
        std::cout << "• Close the window or use File > Quit to exit" << std::endl;
        std::cout << std::endl;

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
