// Apps/ChatCube/main.cpp
// ChatCube - the ULTRA OS native messenger. Chat directly between two
// computers (end-to-end encrypted, peer to peer when both are online), in
// groups through a relay server, without an account or a password: the
// identity is a key made on this computer. Built on UltraIM, whose
// provider modules carry the networks - ChatCube Direct first; Matrix,
// Telegram and others follow as further providers.
//
//   ChatCube                     open the window
//   ChatCube --version / --help
//
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS

#include "ui/ChatCubeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetPlugins.h"
#include "UltraRTC/UltraRTCCore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

// CHATCUBE_VERSION comes from the build alone: CMake reads the first line of
// Docs/ChatCube/CHANGELOG.md (cmake/UltraCanvasVersion.cmake). No fallback.
#ifndef CHATCUBE_VERSION
#error "CHATCUBE_VERSION is not defined: build through CMake, which reads it from Docs/ChatCube/CHANGELOG.md"
#endif

namespace {

namespace fs = std::filesystem;

bool HasModule(const fs::path& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string ext = UltraCanvas::PathToUtf8(it->path().extension());
        if (ext == ".so" || ext == ".dll" || ext == ".dylib") return true;
    }
    return false;
}

// Where the provider modules are: $CHATCUBE_PLUGIN_DIR, else Plugins/UltraIM
// next to the program or up to two folders above it (the build tree keeps
// executables and plug-ins side by side under build/).
std::string ResolvePluginDirectory() {
    const std::string fromEnv = UltraCanvas::GetEnvUtf8("CHATCUBE_PLUGIN_DIR");
    if (!fromEnv.empty()) return fromEnv;
    fs::path base = UltraCanvas::PathFromUtf8(UltraCanvas::GetExecutableDir());
    for (int up = 0; up < 3; ++up) {
        const fs::path candidate = base / "Plugins" / "UltraIM";
        if (HasModule(candidate)) return UltraCanvas::PathToUtf8(candidate);
        if (!base.has_parent_path() || base.parent_path() == base) break;
        base = base.parent_path();
    }
    return "Plugins/UltraIM";
}

void PrintUsage(const char* program) {
    std::printf("ChatCube %s - the ULTRA OS messenger\n\n"
                "Usage: %s [--version | --help]\n\n"
                "Environment:\n"
                "  CHATCUBE_PLUGIN_DIR   where the UltraIM provider modules are\n",
                CHATCUBE_VERSION, program);
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("ChatCube %s\n", CHATCUBE_VERSION);
            return EXIT_SUCCESS;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        std::fprintf(stderr, "ChatCube: unknown option %s\n\n", argv[i]);
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    // One ChatCube per user: two would share, and fight over, one identity.
    using UltraCanvas::UltraCanvasSingleInstance;
    UltraCanvasSingleInstance instance("org.ultraos.chatcube", "ChatCube");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("ChatCube")) {
        std::fprintf(stderr, "ChatCube: the UltraCanvas application could not be initialised (no display?)\n");
        return EXIT_FAILURE;
    }
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "ChatCube");
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/appicon/ChatCube.png"));

    UltraNet_Initialize();
    UltraRtc_Initialize();

    UltraIMConfig config;
    config.appId = "org.ultraos.chatcube";
    config.pluginDirectory = ResolvePluginDirectory();
    // UltraNet's plug-ins (mDNS for the local network) sit next to UltraIM's.
    const fs::path netPlugins = UltraCanvas::PathFromUtf8(config.pluginDirectory).parent_path() / "UltraNet";
    std::error_code netError;
    if (fs::is_directory(netPlugins, netError)) UltraNet_SetPluginDirectory(UltraCanvas::PathToUtf8(netPlugins));
    const fs::path data = UltraCanvas::UltraCanvasDataFolder("ChatCube");
    config.dataDirectory = UltraCanvas::PathToUtf8(data);
    config.cacheDirectory = UltraCanvas::PathToUtf8(data / "cache");
    UltraIM_Initialize(config);
    // Every UltraIM event and callback reaches the window on the UI thread.
    UltraIM_SetEventDispatcher([&app](std::function<void()> task) { app.PostToUIThread(std::move(task)); });
    UltraIM_RefreshProviders();

    int exitCode = EXIT_SUCCESS;
    {
        ChatCube::ChatCubeWindow window(CHATCUBE_VERSION, "direct");
        if (!window.Create()) {
            std::fprintf(stderr, "ChatCube: the window could not be created.\n");
            exitCode = EXIT_FAILURE;
        } else {
            window.onClosed = [&app]() { app.Exit(); };
            window.Show();
            instance.ListenRaise();
            app.Run();
            instance.Close();
        }
        // Providers stop (and their callbacks with them) before the window goes.
        UltraIM_Shutdown();
    }
    UltraRtc_Shutdown();
    UltraNet_Shutdown();
    return exitCode;
}
