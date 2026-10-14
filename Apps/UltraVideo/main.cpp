// Apps/UltraVideo/main.cpp
// UltraVideo - a video editor on the VideoFX engine and the framework's
// UltraCanvasVideoTimeline element. Opens the project or the media files
// named on the command line.
// Version: 0.2.0 - registers the engine's files with the codec registry (RegisterEngineFormats)
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#include "UltraVideoFormats.h"
#include "UltraVideoWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef ULTRAVIDEO_VERSION
#error "ULTRAVIDEO_VERSION must come from Docs/UltraVideo/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplication::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraVideo - a video editor on VideoFX.\n"
              << "Usage: " << prog << " [options] [project.ultravideo | media files...]\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraVideo " << ULTRAVIDEO_VERSION << "\n";
            return EXIT_SUCCESS;
        }
        if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
        paths.push_back(a);
    }

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraVideo::UltraVideoWindow> window;
    try {
        if (!app.Initialize("UltraVideo")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraVideo.png"));
        // The engine's files join FileLoader's lists before a dialog asks
        UltraVideo::RegisterEngineFormats();
        window = std::make_unique<UltraVideo::UltraVideoWindow>(app);
        if (!window->Create(paths)) {
            std::cerr << "Failed to create the main window\n";
            return EXIT_FAILURE;
        }
        window->Show();
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
    window.reset();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
