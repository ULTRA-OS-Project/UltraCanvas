// Apps/UltraTexter/main.cpp
// UltraTexter - Standalone Text Editor Application
// Version: 1.0.0
// Last Modified: 2026-01-24
// Author: UltraCanvas Framework

#include <iostream>
#include <memory>
#include <exception>
#include <string>

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

// ===== GLOBAL APPLICATION STATE =====
static UltraCanvasApplication* g_app = nullptr;
static std::shared_ptr<UltraCanvasWindow> g_window = nullptr;
static std::shared_ptr<UltraCanvasTextEditor> g_textEditor = nullptr;

// ===== ERROR HANDLING =====
void HandleFatalError(const std::string& error) {
    std::cerr << "FATAL ERROR: " << error << std::endl;

#ifdef _WIN32
    MessageBoxA(nullptr, error.c_str(), "UltraTexter - Fatal Error", MB_ICONERROR | MB_OK);
#endif

    if (g_window) {
        g_window.reset();
    }

    std::exit(EXIT_FAILURE);
}

// ===== SIGNAL HANDLERS =====
#ifdef __linux__
void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;

    if (g_window) {
        g_window.reset();
    }

    std::exit(EXIT_SUCCESS);
}
#endif

// ===== SYSTEM INITIALIZATION =====
bool InitializeSystem(UltraCanvasApplication& app, const std::string& appName) {
    std::cout << "=== UltraTexter - Text Editor ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
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

    if (g_textEditor) {
        g_textEditor.reset();
        std::cout << "✓ Text editor released" << std::endl;
    }

    if (g_window) {
        g_window.reset();
        std::cout << "✓ Window released" << std::endl;
    }

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
            std::cout << "UltraTexter version 1.0.0" << std::endl;
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

        // Create main window
        std::cout << "Creating main window..." << std::endl;

        g_window = std::make_shared<UltraCanvasWindow>();

        WindowConfig windowConfig;
        windowConfig.title = "UltraTexter";
        windowConfig.width = 1280;
        windowConfig.height = 800;
        windowConfig.backgroundColor = useDarkTheme ? Color(30, 30, 30) : Color(240, 240, 240);
        windowConfig.deleteOnClose = true;

        if (!g_window->Create(windowConfig)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }

        //app.AddWindow(g_window);
        std::cout << "✓ Main window created" << std::endl;

        // Create text editor configuration
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

        // Create text editor component (fills the window)
        std::cout << "Creating text editor..." << std::endl;

        g_textEditor = CreateTextEditor(
                "MainEditor",
                1,
                0, 0,
                windowConfig.width,
                windowConfig.height,
                editorConfig
        );

        if (!g_textEditor) {
            HandleFatalError("Failed to create text editor");
            return EXIT_FAILURE;
        }

        // Apply theme
        if (useDarkTheme) {
            g_textEditor->ApplyDarkTheme();
        }

        // Setup callbacks
        g_textEditor->onQuitRequest = []() {
            std::cout << "Quit requested" << std::endl;
            if (g_window) {
                g_window->Close();
            }
        };

        g_textEditor->onFileLoaded = [](const std::string& path, int tabIndex) {
            std::cout << "File loaded: " << path << std::endl;
            if (g_window) {
                g_window->SetWindowTitle("UltraTexter - " + path);
            }
        };

        g_textEditor->onFileSaved = [](const std::string& path, int tabIndex) {
            std::cout << "File saved: " << path << std::endl;
        };

        g_textEditor->onModifiedChange = [](bool modified, int tabIndex) {
            if (g_window && g_textEditor) {
                std::string title = "UltraTexter";
                std::string filePath = g_textEditor->GetActiveFilePath();
                if (!filePath.empty()) {
                    title += " - " + filePath;
                }
                if (modified) {
                    title += " *";
                }
                g_window->SetWindowTitle(title);
            }
        };

        // Add editor to window
        g_window->AddChild(g_textEditor);
        std::cout << "✓ Text editor created" << std::endl;

        // Load file if specified on command line
        if (!fileToOpen.empty()) {
            std::cout << "Opening file: " << fileToOpen << std::endl;
            if (g_textEditor->OpenFile(fileToOpen)) {
                std::cout << "✓ File loaded successfully" << std::endl;
            } else {
                std::cerr << "Warning: Failed to load file: " << fileToOpen << std::endl;
            }
        }

        // Show window
        g_window->Show();

        std::cout << std::endl;
        std::cout << "=== UltraTexter Ready ===" << std::endl;
        std::cout << "• Use File menu for New/Open/Save operations" << std::endl;
        std::cout << "• Use Edit menu for text editing operations" << std::endl;
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