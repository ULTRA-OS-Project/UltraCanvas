// Apps/UltraSnip/main.cpp
// Entry point of UltraSnip, the screenshot and screen-recording tool of ULTRA
// OS. Started bare it shows its window; with a shape (--rectangle, --window,
// --screen) it takes that snip at once, --video records instead of taking a
// picture, --delay N waits first. A second start hands its request to the
// running copy (UltraCanvasSingleInstance), which is how UltraDesktop's
// screenshot button and Super+Shift+S reach it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraSnipTypes.h"
#include "ui/UltraSnipWindow.h"

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

#ifndef ULTRASNIP_VERSION
#error "ULTRASNIP_VERSION must come from Docs/UltraSnip/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraSnip - screenshots and screen recordings.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --rectangle    take a picture of an area you drag out\n"
              << "  --window       take a picture of a window you click\n"
              << "  --screen       take a picture of the whole screen\n"
              << "  --video        record a video instead of taking a picture (with a shape above)\n"
              << "  --delay N      wait N seconds first (3, 5 or 10)\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n"
              << "Without a shape the window opens; pictures go to Pictures/Screenshots and\n"
              << "the clipboard, recordings to Videos/Screen Recordings.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    UltraSnip::SnipRequest request;
    bool shape = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraSnip " << ULTRASNIP_VERSION << "\n";
            return 0;
        }
        if (a == "--rectangle") { request.mode = UltraSnip::SnipMode::Rectangle; shape = true; continue; }
        if (a == "--window") { request.mode = UltraSnip::SnipMode::Window; shape = true; continue; }
        if (a == "--screen" || a == "--fullscreen") { request.mode = UltraSnip::SnipMode::FullScreen; shape = true; continue; }
        if (a == "--video") { request.kind = UltraSnip::SnipKind::Video; continue; }
        if (a == "--picture") { request.kind = UltraSnip::SnipKind::Picture; continue; }
        if (a == "--delay" && i + 1 < argc) {
            request.delaySeconds = std::atoi(argv[++i]);
            if (request.delaySeconds != 0 && request.delaySeconds != 3 && request.delaySeconds != 5 &&
                request.delaySeconds != 10) {
                std::cerr << "--delay takes 0, 3, 5 or 10\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        std::cerr << "Unknown option: " << a << "\n";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }
    request.startAtOnce = shape;

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraSnip::UltraSnipWindow> window;
    // Declared after the window, so it stops listening before the window goes.
    UltraCanvasSingleInstance instance("org.ultraos.ultrasnip", "UltraSnip");
    const UltraCanvasSingleInstance::Start start = instance.Claim(UltraSnip::RequestToJson(request));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) {
        std::cout << "UltraSnip is already running: handed the request to it.\n";
        return EXIT_SUCCESS;
    }
    try {
        if (!app.Initialize("UltraSnip")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraSnip.png"));
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraSnip");
        window = std::make_unique<UltraSnip::UltraSnipWindow>(app);
        if (!window->Create()) {
            std::cerr << "Failed to create the main window\n";
            return EXIT_FAILURE;
        }
        UltraSnip::UltraSnipWindow* shown = window.get();
        instance.Listen([shown, &app](const JSONValue& json) {
            const UltraSnip::SnipRequest later = UltraSnip::RequestFromJson(json);
            app.PostToUIThread([shown, later]() { shown->Present(later); });
        });
        if (request.startAtOnce) window->StartCapture(request);
        else window->Show();
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
