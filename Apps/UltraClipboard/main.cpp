// Apps/UltraClipboard/main.cpp
// Entry point: the clipboard history's main window. UltraDesktop's quick
// panel starts it with --search TEXT (the panel's search, carried over) or
// --edit ID (an entry's edit dialog at once). When UltraClipboard is already
// running, the request goes to it and this start quits
// (UltraCanvasSingleInstance.h).
//
// --background starts it with no window (UltraDesktop's "start with the
// desktop"): it records and keeps the history open, and the next start shows
// the window at once. Closing the window then hides it; Quit in the More
// menu ends the program. A --background start while one runs changes nothing.
// Version: 0.5.0 - UltraAI reads the words in a picture when one of its vision providers
//                  reads text; the OCR plugin otherwise
// Version: 0.4.0 - text recognition: the OCR plugin reads the words in a picture
//                  for Extract text (UltraCanvasFileLoader::ExtractText)
// Version: 0.3.0 - --background
// Version: 0.2.0 - the framework's single-instance helper: two starts at the
//                  same moment no longer both stay, and a copy that does not
//                  answer is reported instead of joined by a second one
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraClipboardWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#ifdef ULTRACANVAS_OCR_SUPPORT
#include "UltraCanvasOCRPlugin.h"   // Extract text reads pictures with it
#endif
#ifdef ULTRACLIPBOARD_HAS_ULTRAAI
#include "UltraAITextRecognition.h"   // ... or with an UltraAI vision provider, when one reads text
#endif

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
              << "  --background   start without a window, ready for the next start\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string search;
    int64_t editId = 0;
    bool background = false;
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
        if (a == "--background") {
            background = true;
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
    // Declared after the window, so it stops listening before the window goes.
    // The request travels as {search, editId}, on the bus topic
    // org.ultraos.ultraclipboard.show.
    UltraCanvasSingleInstance instance("org.ultraos.ultraclipboard", "UltraClipboard");
    JSONValue request = JSONValue::MakeObject();
    if (!search.empty()) request.Set("search", search);
    if (editId != 0) request.Set("editId", editId);
    if (background) request.Set("background", true);
    const UltraCanvasSingleInstance::Start start = instance.Claim(request);
    if (start == UltraCanvasSingleInstance::Start::HandedOff) {
        std::cout << (background ? "UltraClipboard is already running.\n"
                                 : "UltraClipboard is already running: brought it forward.\n");
        return EXIT_SUCCESS;
    }
    try {
        if (!app.Initialize("UltraClipboard")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraClipboard.png"));
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraClipboard");
#ifdef ULTRACANVAS_OCR_SUPPORT
        RegisterOCRTextRecognition();
#endif
#ifdef ULTRACLIPBOARD_HAS_ULTRAAI
        UltraAI::RegisterUltraAITextRecognition();   // in front of the OCR plugin, which stays the fallback
#endif
        window = std::make_unique<UltraClipboard::UltraClipboardWindow>(app);
        if (!window->Create(search, editId)) {
            std::cerr << "Failed to create the main window\n";
            return EXIT_FAILURE;
        }
        if (background) window->SetKeepRunning(true);
        else window->Show();
        UltraClipboard::UltraClipboardWindow* shown = window.get();
        instance.Listen([shown](const JSONValue& request) {
            // Another background start (the desktop starting again): it
            // is running, which is all that one wanted.
            if (request.Get("background").GetBoolean()) return;
            shown->Present(request.Get("search").GetString(), request.Get("editId").GetInteger());
        });
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
