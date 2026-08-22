// Apps/UltraAuthenticator/main.cpp
// Entry point: unlock the vault, then show the account list.
//
// The unlock prompt is a modal gate at launch rather than an inline banner on
// the list. Nothing about the accounts — not even how many there are — is
// rendered before the password is accepted, which is the behaviour the threat
// model in Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md §3.1
// asks for and the one Aegis and andOTP use.
//
// There is no "skip" and no "remember me". A vault that can be opened without
// the password is a vault that an attacker with the file can open too, and a
// silent unprotected mode is exactly the hole UltraCrypt exists to close.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AccountStore.h"
#include "AuthenticatorWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasModalDialog.h"
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

using namespace UltraCanvas;
using namespace UltraCanvas::Authenticator;

namespace {

UltraCanvasApplication* g_app = nullptr;

#ifdef __linux__
void OnSignal(int sig) {
    if (g_app) g_app->RequestExit();
    std::exit(sig == SIGTERM ? EXIT_SUCCESS : EXIT_FAILURE);
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraAuthenticator — TOTP/HOTP codes for ULTRA OS.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --vault PATH   use this vault file\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

// Per-user data directory, following XDG on Linux and the platform's
// equivalent elsewhere. The vault holds second factors, so it belongs beside
// the user's other application data, not in the working directory.
std::string DefaultVaultPath() {
    namespace fs = std::filesystem;
    fs::path base;

    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = fs::path(xdg);
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = fs::path(home) / ".local" / "share";
    } else if (const char* appData = std::getenv("APPDATA"); appData && *appData) {
        base = fs::path(appData);
    } else {
        base = fs::current_path();
    }

    fs::path dir = base / "UltraAuthenticator";
    std::error_code ec;
    fs::create_directories(dir, ec);   // best effort; open reports real errors
    return (dir / "accounts.vault").string();
}

// Bridges a dialog's std::string into a secure buffer and wipes the copy we
// own. The framework's own copy inside the text field is outside our reach —
// a genuine limitation, and the reason a future UltraCanvasTextInput that can
// hand over a secure buffer directly would be worth having.
UltraCryptSecureBuffer AdoptPassword(const std::string& typed) {
    std::string copy = typed;
    return UltraCryptSecureBuffer::AdoptString(copy);
}

// Opens (or creates) the vault, then hands off to the window. Kept as a
// continuation because the password dialog is asynchronous.
void OpenVaultThen(UltraCanvasApplication& app, AccountStore& store,
                   const std::string& vaultPath,
                   std::shared_ptr<AuthenticatorWindow>& windowOut) {
    const bool exists = AccountStore::Exists(vaultPath);

    const std::string prompt =
        exists ? "Enter your master password to unlock your accounts."
               : "Choose a master password.\n\nIt protects every account "
                 "in this app and cannot be recovered — if you forget it, "
                 "the accounts are gone.";

    UltraCanvasDialogManager::ShowInputDialog(
        prompt,
        exists ? "Unlock UltraAuthenticator" : "Set up UltraAuthenticator",
        "", InputType::Password,
        [&app, &store, vaultPath, exists, &windowOut](
            DialogResult result, const std::string& typed) {
            if (result != DialogResult::OK) {
                app.RequestExit();
                return;
            }
            if (typed.empty()) {
                UltraCanvasDialogManager::ShowError(
                    "A master password is required.", "UltraAuthenticator");
                app.RequestExit();
                return;
            }

            UltraCryptSecureBuffer password = AdoptPassword(typed);
            StoreResult opened = exists ? store.Open(vaultPath, password)
                                        : store.Create(vaultPath, password);
            if (!opened) {
                // A wrong password and a modified vault deliberately produce
                // the same message; telling them apart would say which of the
                // two an attacker achieved.
                UltraCanvasDialogManager::ShowError(
                    opened.message, "Could not open the vault");
                app.RequestExit();
                return;
            }

            windowOut = std::make_shared<AuthenticatorWindow>(app, store);
            if (!windowOut->Create()) {
                std::cerr << "Failed to create the main window\n";
                app.RequestExit();
                return;
            }
            windowOut->Show();
        },
        nullptr);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string vaultPath;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")    { PrintUsage(argv[0]); return 0; }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraAuthenticator 0.1.0\n";
            return 0;
        }
        if (a == "--vault" && i + 1 < argc) { vaultPath = argv[++i]; continue; }
    }
    if (vaultPath.empty()) vaultPath = DefaultVaultPath();

    // Fail before showing any UI if there is no crypto backend. Without one
    // there is no safe way to hold a seed, and an authenticator that starts
    // anyway would be inviting the user to trust it.
    if (!UltraCrypt_IsAvailable()) {
        std::cerr << "UltraAuthenticator requires a crypto backend "
                     "(built without libsodium).\n";
        return EXIT_FAILURE;
    }

#ifdef __linux__
    if (!XInitThreads()) {
        std::cerr << "warning: XInitThreads failed\n";
    }
    std::signal(SIGINT,  OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    g_app = &app;

    AccountStore store;
    std::shared_ptr<AuthenticatorWindow> window;

    try {
        if (!app.Initialize("UltraAuthenticator")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }

        // Use the framework's own dialogs: the native ones cannot be given a
        // password field on every platform.
        UltraCanvasDialogManager::SetUseNativeDialogs(false);

        OpenVaultThen(app, store, vaultPath, window);

        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        store.Close();
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        store.Close();
        return EXIT_FAILURE;
    }

    store.Close();
    g_app = nullptr;
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
