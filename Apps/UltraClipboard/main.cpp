// Apps/UltraClipboard/main.cpp
// Entry point: the clipboard history's main window. UltraDesktop's quick
// panel starts it with --search TEXT (the panel's search, carried over) or
// --edit ID (an entry's edit dialog at once).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraClipboardWindow.h"

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

#ifndef ULTRACLIPBOARD_VERSION
#error "ULTRACLIPBOARD_VERSION must come from Docs/UltraClipboard/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraClipboard - the clipboard history of ULTRA OS.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --search TEXT  start with this search\n"
              << "  --edit ID      open the edit dialog of entry ID\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string search;
    int64_t editId = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraClipboard " << ULTRACLIPBOARD_VERSION << "\n";
            return 0;
        }
        if (a == "--search" && i + 1 < argc) {
            search = argv[++i];
            continue;
        }
        if (a == "--edit" && i + 1 < argc) {
            editId = std::strtoll(argv[++i], nullptr, 10);
            continue;
        }
        std::cerr << "Unknown option: " << a << "\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraClipboard::UltraClipboardWindow> window;
    try {
        if (!app.Initialize("UltraClipboard")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraClipboard.png"));
        window = std::make_unique<UltraClipboard::UltraClipboardWindow>(app);
        if (!window->Create(search, editId)) {
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
