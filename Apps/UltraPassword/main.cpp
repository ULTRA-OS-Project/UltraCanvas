// Apps/UltraPassword/main.cpp
// Entry point: find the vault file, then hand over to the main window, which
// shows the start page — "Create password vault" on first run, the master
// password field once a vault exists.
//
// There is no "skip" and no "remember me": a vault that opens without the
// password opens for anyone holding the file.
//
// Version: 0.1.1 - the default vault folder comes from core/VaultLocation.h
//                  (Application Support on macOS, moved there once)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "core/VaultLocation.h"   // DefaultVaultPath
#include "ui/PasswordApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
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
    if (vaultPath.empty()) {
        // The platform's application data folder (VaultLocation.h); a vault an
        // earlier version kept in ~/.local/share on macOS is moved there once.
        std::string movedFrom, moveError;
        vaultPath = UltraPassword::DefaultVaultPath(&movedFrom, &moveError);
        if (!movedFrom.empty())
            std::cerr << "UltraPassword: moved the vault from " << movedFrom << " to "
                      << UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(vaultPath).parent_path()) << "\n";
        else if (!moveError.empty())
            std::cerr << "UltraPassword: " << moveError << "\n";
    }

    // Without a crypto backend there is no safe way to hold a password; a
    // vault app that started anyway would be inviting misplaced trust.
    if (!UltraCrypt_IsAvailable()) {
        std::cerr << "UltraPassword requires a crypto backend (built without libsodium).\n";
        return EXIT_FAILURE;
    }

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
        app.Run();
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
