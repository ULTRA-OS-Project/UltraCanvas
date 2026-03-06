// UltraCanvas Framework Demonstration Program Entry Point
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include <iostream>
#include <memory>
#include <exception>
#include <string>
#include <thread>
#include <chrono>

// UltraCanvas Core Headers
#include "UltraCanvasDemo.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
//#include "UltraCanvasCDRPlugin.h"

// OS-specific initialization if needed
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
std::unique_ptr<UltraCanvasDemoApplication> g_demoApp = nullptr;

// ===== ERROR HANDLING =====
void HandleFatalError(const std::string& error) {
    std::cerr << "FATAL ERROR: " << error << std::endl;

    // Try to show OS-specific error dialog
#ifdef _WIN32
    MessageBoxA(nullptr, error.c_str(), "UltraCanvas Demo - Fatal Error", MB_ICONERROR | MB_OK);
#endif

    // Clean shutdown
    if (g_demoApp) {
        g_demoApp->Shutdown();
        g_demoApp.reset();
    }

    std::exit(EXIT_FAILURE);
}

// ===== SIGNAL HANDLERS =====
#ifdef __linux__
void SignalHandler(int signal) {
    std::cerr << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;

    if (g_demoApp) {
        g_demoApp->Shutdown();
        g_demoApp.reset();
    }

    std::exit(EXIT_SUCCESS);
}
#endif

// ===== SYSTEM INITIALIZATION =====
bool InitializeSystem(UltraCanvasApplication& g_app, const std::string& aName) {
    std::cerr << "=== UltraCanvas Framework Demonstration Program ===" << std::endl;
    std::cerr << "Version: 1.0.0" << std::endl;
    std::cerr << "Build Date: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cerr << "Platform: ";

#ifdef _WIN32
    std::cerr << "Windows";

        // Enable memory leak detection in debug builds
        #ifdef _DEBUG
            _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
            std::cerr << " (Debug Build - Memory Leak Detection Enabled)";
        #endif

#elif __linux__
    std::cerr << "Linux";

    // Setup signal handlers for graceful shutdown
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Initialize X11 threading support
    if (!XInitThreads()) {
        std::cerr << "Warning: X11 threading initialization failed" << std::endl;
    }

#elif __APPLE__
    std::cerr << "macOS";
    #else
        std::cerr << "Unknown";
#endif

    std::cerr << std::endl << std::endl;

    try {
        // Initialize UltraCanvas framework
        std::cerr << "Initializing UltraCanvas framework..." << std::endl;

        if (!g_app.Initialize(aName)) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return false;
        }
//        RegisterCDRPlugin();
        std::cerr << "✓ UltraCanvas framework initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Framework initialization failed: ") + e.what());
        return false;
    }

    return true;
}

void ShutdownSystem() {
    std::cerr << std::endl << "Shutting down system..." << std::endl;

    // Shutdown demo application
    if (g_demoApp) {
        g_demoApp->Shutdown();
        g_demoApp.reset();
        std::cerr << "✓ Demo application shut down" << std::endl;
    }
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    std::cerr << std::endl;
    UltraCanvasApplication g_app;
    try {
        // Process command line arguments
        bool verboseMode = false;
        bool testMode = false;
        std::string startupComponent = "";

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg == "--verbose" || arg == "-v") {
                verboseMode = true;
                std::cerr << "Verbose mode enabled" << std::endl;
            } else if (arg == "--test" || arg == "-t") {
                testMode = true;
                std::cerr << "Test mode enabled" << std::endl;
            } else if (arg == "--component" || arg == "-c") {
                if (i + 1 < argc) {
                    startupComponent = argv[++i];
                    std::cerr << "Startup component: " << startupComponent << std::endl;
                }
            } else if (arg == "--help" || arg == "-h") {
                std::cerr << "UltraCanvas Demo Application" << std::endl;
                std::cerr << "Usage: " << argv[0] << " [options]" << std::endl;
                std::cerr << "Options:" << std::endl;
                std::cerr << "  -v, --verbose     Enable verbose output" << std::endl;
                std::cerr << "  -t, --test        Run in test mode" << std::endl;
                std::cerr << "  -c, --component   Start with specific component selected" << std::endl;
                std::cerr << "  -h, --help        Show this help message" << std::endl;
                return 0;
            } else {
                std::cerr << "Unknown argument: " << arg << std::endl;
                std::cerr << "Use --help for usage information" << std::endl;
            }
        }


        // Initialize system
        if (!InitializeSystem(g_app, argv[0])) {
            return EXIT_FAILURE;
        }

        // Create and initialize demo application
        std::cerr << "Creating demo application..." << std::endl;
        g_demoApp = CreateDemoApplication();

        if (!g_demoApp) {
            HandleFatalError("Failed to create demo application");
            return EXIT_FAILURE;
        }

        if (!g_demoApp->Initialize()) {
            HandleFatalError("Failed to initialize demo application");
            return EXIT_FAILURE;
        }

        // Auto-select startup component if specified
        if (!startupComponent.empty()) {
            std::cerr << "Auto-selecting component: " << startupComponent << std::endl;
            // Implementation would go here to programmatically select the component
        }

        std::cerr << std::endl;
        std::cerr << "=== Demo Application Ready ===" << std::endl;
        std::cerr << "Instructions:" << std::endl;
        std::cerr << "• Use the tree view on the left to browse component categories" << std::endl;
        std::cerr << "• Click on individual components to see implementation examples" << std::endl;
        std::cerr << "• Status icons indicate implementation progress:" << std::endl;
        std::cerr << "  ✓ Fully implemented" << std::endl;
        std::cerr << "  ⚠ Partially implemented" << std::endl;
        std::cerr << "  ✗ Not implemented" << std::endl;
        std::cerr << "  📋 Planned for future release" << std::endl;
        std::cerr << "• Close the window or press Ctrl+C to exit" << std::endl;
        std::cerr << std::endl;

        // Run the demo application
        g_demoApp->Run();

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

// ===== ADDITIONAL PLATFORM-SPECIFIC ENTRY POINTS =====

#ifdef _WIN32
// Windows-specific WinMain entry point (if needed for Windows apps)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Parse command line and call main
    return main(__argc, __argv);
}
#endif