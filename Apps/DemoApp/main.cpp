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
    std::cout << "\nReceived signal " << signal << " - shutting down gracefully..." << std::endl;

    if (g_demoApp) {
        g_demoApp->Shutdown();
        g_demoApp.reset();
    }

    std::exit(EXIT_SUCCESS);
}
#endif

// ===== SYSTEM INITIALIZATION =====
bool InitializeSystem(UltraCanvasApplication& g_app) {
    std::cout << "=== UltraCanvas Framework Demonstration Program ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build Date: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "Platform: ";

#ifdef _WIN32
    std::cout << "Windows";

        // Enable memory leak detection in debug builds
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
        // Initialize UltraCanvas framework
        std::cout << "Initializing UltraCanvas framework..." << std::endl;

        if (!g_app.Initialize()) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return false;
        }

        std::cout << "✓ UltraCanvas framework initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Framework initialization failed: ") + e.what());
        return false;
    }

    return true;
}

void ShutdownSystem() {
    std::cout << std::endl << "Shutting down system..." << std::endl;

    // Shutdown demo application
    if (g_demoApp) {
        g_demoApp->Shutdown();
        g_demoApp.reset();
        std::cout << "✓ Demo application shut down" << std::endl;
    }
}

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    std::cout << std::endl;
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
                std::cout << "Verbose mode enabled" << std::endl;
            } else if (arg == "--test" || arg == "-t") {
                testMode = true;
                std::cout << "Test mode enabled" << std::endl;
            } else if (arg == "--component" || arg == "-c") {
                if (i + 1 < argc) {
                    startupComponent = argv[++i];
                    std::cout << "Startup component: " << startupComponent << std::endl;
                }
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "UltraCanvas Demo Application" << std::endl;
                std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
                std::cout << "Options:" << std::endl;
                std::cout << "  -v, --verbose     Enable verbose output" << std::endl;
                std::cout << "  -t, --test        Run in test mode" << std::endl;
                std::cout << "  -c, --component   Start with specific component selected" << std::endl;
                std::cout << "  -h, --help        Show this help message" << std::endl;
                return 0;
            } else {
                std::cout << "Unknown argument: " << arg << std::endl;
                std::cout << "Use --help for usage information" << std::endl;
            }
        }


        // Initialize system
        if (!InitializeSystem(g_app)) {
            return EXIT_FAILURE;
        }

        // Create and initialize demo application
        std::cout << "Creating demo application..." << std::endl;
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
            std::cout << "Auto-selecting component: " << startupComponent << std::endl;
            // Implementation would go here to programmatically select the component
        }

        std::cout << std::endl;
        std::cout << "=== Demo Application Ready ===" << std::endl;
        std::cout << "Instructions:" << std::endl;
        std::cout << "• Use the tree view on the left to browse component categories" << std::endl;
        std::cout << "• Click on individual components to see implementation examples" << std::endl;
        std::cout << "• Status icons indicate implementation progress:" << std::endl;
        std::cout << "  ✓ Fully implemented" << std::endl;
        std::cout << "  ⚠ Partially implemented" << std::endl;
        std::cout << "  ✗ Not implemented" << std::endl;
        std::cout << "  📋 Planned for future release" << std::endl;
        std::cout << "• Close the window or press Ctrl+C to exit" << std::endl;
        std::cout << std::endl;

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