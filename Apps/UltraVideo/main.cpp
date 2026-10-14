// Apps/UltraVideo/main.cpp
// UltraVideo - a video editor on the VideoFX engine and the framework's
// UltraCanvasVideoTimeline element. Opens the project or the media files
// named on the command line. When UltraVideo is already running, the files go
// to it instead - a project opens there, media is imported into its project -
// and this start quits (UltraCanvasSingleInstance.h).
// Version: 0.3.0 - one copy per user: a second start's files open in the
//                  running copy
// Version: 0.2.0 - registers the engine's files with the codec registry (RegisterEngineFormats)
// Version: 0.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#include "UltraVideoFormats.h"
#include "UltraVideoWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"
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

    // One copy per user: a second start hands its files to the running copy
    // (absolute, since that copy was started in another folder) and quits.
    // Declared before the window and closed before it goes, so no request
    // reaches a window that is going.
    UltraCanvasSingleInstance instance("org.ultraos.ultravideo", "UltraVideo");
    const UltraCanvasSingleInstance::Start start = instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(paths));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

    UltraCanvasApplication app;
    std::unique_ptr<UltraVideo::UltraVideoWindow> window;
    try {
        if (!app.Initialize("UltraVideo")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraVideo.png"));
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraVideo");
        // The engine's files join FileLoader's lists before a dialog asks
        UltraVideo::RegisterEngineFormats();
        window = std::make_unique<UltraVideo::UltraVideoWindow>(app);
        // Listening starts before the command line's files are opened: a
        // file that cannot be read shows a native error dialog that holds the
        // start-up, and a start made meanwhile is still answered.
        //
        // A later start's files: a project opens (asking to save the current
        // one first when it has changes), media is imported into the open
        // project - as dropping them on the window does. Then the window, or
        // the save question it asks, comes forward.
        UltraVideo::UltraVideoWindow* shown = window.get();
        instance.ListenOpen([shown](const std::vector<std::string>& opened) {
            shown->OpenFiles(opened);
            UltraCanvasSingleInstance::BringToFront();
        });

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
    instance.Close();
    window.reset();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
