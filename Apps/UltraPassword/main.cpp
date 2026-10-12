// Apps/UltraPassword/main.cpp
// Entry point: find the vault file, then hand over to the main window, which
// shows the start page — "Create password vault" on first run, the master
// password field once a vault exists. One UltraPassword per user: a second
// start brings the running one forward and quits (UltraCanvasSingleInstance.h).
//
// There is no "skip" and no "remember me": a vault that opens without the
// password opens for anyone holding the file.
//
// Version: 0.2.0 - one UltraPassword per user: a second start brings the
//                  first one's window forward instead of opening another copy
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/PasswordApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraCrypt/UltraCryptCore.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef ULTRAPASSWORD_VERSION
#error "ULTRAPASSWORD_VERSION must come from Docs/UltraPassword/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraPassword - the password vault for ULTRA OS.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --vault PATH   use this vault file\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

// Per-user data directory: $XDG_DATA_HOME, else %APPDATA% on Windows, else
// ~/.local/share - the same places UltraAuthenticator keeps its vault.
std::string DefaultVaultPath() {
    namespace fs = std::filesystem;
    fs::path base;
    // UTF-8 (GetEnvUtf8), as PathFromUtf8 expects: on Windows a narrow
    // getenv answers in the ANSI code page and would miss a profile folder
    // named outside it.
    if (const std::string xdg = GetEnvUtf8("XDG_DATA_HOME"); !xdg.empty()) {
        base = PathFromUtf8(xdg);
    } else if (const std::string appData = GetEnvUtf8("APPDATA"); !appData.empty()) {
        base = PathFromUtf8(appData);
    } else if (const std::string home = GetEnvUtf8("HOME"); !home.empty()) {
        base = PathFromUtf8(home) / ".local" / "share";
    } else {
        base = fs::current_path();
    }
    fs::path dir = base / "UltraPassword";
    std::error_code ec;
    fs::create_directories(dir, ec);   // best effort; Create reports real errors
    return PathToUtf8(dir / "vault.upwvault");
}

} // namespace

int main(int argc, char* argv[]) {
    std::string vaultPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help")    { PrintUsage(argv[0]); return 0; }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraPassword " << ULTRAPASSWORD_VERSION << "\n";
            return 0;
        }
        if (a == "--vault" && i + 1 < argc) { vaultPath = argv[++i]; continue; }
    }
    if (vaultPath.empty()) vaultPath = DefaultVaultPath();

    // Without a crypto backend there is no safe way to hold a password; a
    // vault app that started anyway would be inviting misplaced trust.
    if (!UltraCrypt_IsAvailable()) {
        std::cerr << "UltraPassword requires a crypto backend (built without libsodium).\n";
        return EXIT_FAILURE;
    }

    // One copy per user whatever the vault: a second start, with or without
    // --vault, only brings the running one forward - its window, on the vault
    // it already has open. Outlives app.Run(), so it sits outside the try.
    UltraCanvasSingleInstance instance("org.ultraos.ultrapassword", "UltraPassword");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy came forward

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT,  OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraPassword::PasswordApp> window;
    try {
        if (!app.Initialize("UltraPassword")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraPassword");
        app.SetDefaultWindowIcon(
            NormalizePath(GetResourcesDir() + "media/appicon/UltraPassword.png"));
        // The framework's own dialogs: native ones cannot hold a password
        // field on every platform.
        UltraCanvasDialogManager::SetUseNativeDialogs(false);

        window = std::make_unique<UltraPassword::PasswordApp>(app, vaultPath);
        if (!window->Create()) {
            std::cerr << "Failed to create the main window\n";
            return EXIT_FAILURE;
        }
        window->Show();
        instance.ListenRaise();   // a later start brings this window forward
        app.Run();
        instance.Close();         // no request reaches the window once it starts to go
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
    window.reset();   // wipes the vault and the key
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
