// Apps/UltraWeb/main.cpp
// UltraWeb - the ULTRA OS browser. This first version runs WebAssembly apps
// that build their UI out of UltraCanvas elements through the element ABI
// (UltraWeb/guest/ultraweb.h); HTML pages and JavaScript follow the phase
// plan in Docs/UltraWeb/UltraWebProposal.md.
//
//   UltraWeb [address]          the window, opening address (default about:demo)
//   UltraWeb --check <address>  load and start an app without a window, print
//                               what it built or why it failed, exit 0 / 1
//   UltraWeb --version | --help
//
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "host/UltraWebGuest.h"
#include "host/UltraWebLoader.h"
#include "ui/UltraWebWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasUtils.h"
#include "UltraNet/UltraNetCore.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

// ULTRAWEB_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraWeb/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as
// a compile definition. No fallback, so a build that lost it fails instead of
// reporting a wrong number.
#ifndef ULTRAWEB_VERSION
#error "ULTRAWEB_VERSION is not defined: build through CMake, which reads it from Docs/UltraWeb/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace {

void SignalHandler(int) { UltraCanvasApplicationBase::RequestExitFromSignal(); }

void PrintUsage() {
    std::printf("UltraWeb %s - runs WebAssembly apps built on UltraCanvas elements\n\n"
                "  UltraWeb [address]          open the window at address (default about:demo)\n"
                "  UltraWeb --check <address>  start an app without a window and report\n"
                "  UltraWeb --version\n\n"
                "An address is about:demo, a .wasm (or .wat) file, a file:// URL or an https:// URL.\n"
                "Engine: %s\n",
                ULTRAWEB_VERSION, UltraCanvasWasm_EngineDescription().c_str());
}

// --check: the same loader and guest as the window, with a root container
// that is never shown and deferred work run immediately.
int RunCheck(const std::string& address) {
    UltraWeb::LoadedApp app = UltraWeb::UltraWebLoader::LoadOffline(address);
    if (!app.ok) {
        std::fprintf(stderr, "%s: %s\n", app.address.c_str(), app.error.c_str());
        return EXIT_FAILURE;
    }
    auto root = CreateContainer("uwCheckRoot", 0, 0, 800, 600);
    UltraWeb::GuestOptions options;
    options.onLog = [](const std::string& line) { std::printf("  log: %s\n", line.c_str()); };
    std::string failure;
    options.onFailure = [&failure](const std::string& message) { failure = message; };
    options.defer = [](std::function<void()> task) { task(); };
    std::string error;
    auto guest = UltraWeb::UltraWebGuest::Start(app.module, root, std::move(options), error);
    if (!guest) {
        std::fprintf(stderr, "%s did not start: %s\n", app.address.c_str(), error.c_str());
        return EXIT_FAILURE;
    }
    std::printf("%s: started, %zu elements (%s)\n", app.address.c_str(), guest->ElementCount(),
                UltraCanvasWasm_EngineDescription().c_str());
    return failure.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string address = "about:demo";
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--version") == 0) {
            std::printf("UltraWeb %s (%s)\n", ULTRAWEB_VERSION, UltraCanvasWasm_EngineDescription().c_str());
            return EXIT_SUCCESS;
        }
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            PrintUsage();
            return EXIT_SUCCESS;
        }
        if (std::strcmp(arg, "--check") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--check needs an address\n");
                return EXIT_FAILURE;
            }
            return RunCheck(argv[i + 1]);
        }
        if (arg[0] == '-') {
            std::fprintf(stderr, "Unknown option %s\n\n", arg);
            PrintUsage();
            return EXIT_FAILURE;
        }
        address = arg;
    }

    UltraCanvasApplication app;
#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif
    try {
        if (!app.Initialize("UltraWeb")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        // One icon, everywhere the app is drawn: the window and the taskbar
        // read this file; the .ico in the Windows binary and the desktop
        // entry's theme icon come from the same media/appicon/UltraWeb.svg.
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraWeb.png"));
        // Apps fetched from https:// come through UltraNet.
        const UltraNetResult net = UltraNet_Initialize();
        if (!net.success) debugOutput << "UltraNet: " << net.message << " - only local apps will load" << std::endl;

        UltraWeb::UltraWebWindow window;
        if (!window.Initialize()) {
            debugOutput << "Failed to create the UltraWeb window" << std::endl;
            return EXIT_FAILURE;
        }
        window.Show();
        window.Navigate(address);
        app.Run();
        UltraNet_Shutdown();
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
