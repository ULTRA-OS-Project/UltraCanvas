// Apps/ArtCreator/main.cpp
// ArtCreator - vector drawing editor on the UltraCanvas framework's vector
// editing layer (VectorStorage::VectorDocument, VectorEdit,
// UltraCanvasVectorCanvas) with the Vector plugin's converters for the
// file formats.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include "ArtCreatorWindow.h"
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "UltraCanvasVectorFormatsPlugin.h"
#endif

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef __linux__
#include <X11/Xlib.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

// ARTCREATOR_VERSION comes from the build alone: CMake reads the first
// line of Docs/ArtCreator/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build
// that lost it fails instead of reporting a wrong number.
#ifndef ARTCREATOR_VERSION
#error "ARTCREATOR_VERSION is not defined: build ArtCreator through the top-level CMakeLists.txt"
#endif

using namespace UltraCanvas;

namespace {

UltraCanvasApplication* g_app = nullptr;

void SignalHandler(int signum) {
    debugOutput << "Signal " << signum << " received, shutting down" << std::endl;
    if (g_app) g_app->RequestExit();
}

void HandleFatalError(const std::string& message) {
    debugOutput << "FATAL: " << message << std::endl;
    UltraCanvasDialogManager::ShowError(message, "ArtCreator");
}

// --help and --version answer on stdout (errors on stderr): they are for the
// shell, not the diagnostic stream, which a Release build keeps off.
void PrintUsage(const char* programName) {
    std::cout << "ArtCreator " << ARTCREATOR_VERSION << " - Vector Drawing Editor powered by UltraCanvas Framework" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << programName << " [options] [drawing...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -v, --version     Show version information" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << "                 # a new A4 drawing" << std::endl;
    std::cout << "  " << programName << " logo.svg        # open a drawing" << std::endl;
    std::cout << "  " << programName << " a.svg b.xar     # one window each" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> pathsToOpen;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "ArtCreator version " << ARTCREATOR_VERSION << std::endl;
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
        debugOutput << "=== ArtCreator - Vector Drawing Editor ===" << std::endl;
        if (!app.Initialize("ArtCreator")) {
            HandleFatalError("Failed to initialize UltraCanvas application");
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/ArtCreator.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
        // The file formats: SVG, XAR, EMF, WMF, DXF, DWG in; those plus PDF,
        // AI, EPS, CDR out. Without the plugin the editor still draws, but
        // Open and Save say so.
        RegisterVectorFormatsPlugin();
#endif

        if (!ArtCreatorWindow::OpenWindow(pathsToOpen)) {
            HandleFatalError("Failed to create main window");
            return EXIT_FAILURE;
        }

        debugOutput << "=== ArtCreator Ready ===" << std::endl;
        app.Run();
    } catch (const std::exception& e) {
        HandleFatalError(std::string("Unhandled exception: ") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        HandleFatalError("Unknown exception occurred");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
#endif
