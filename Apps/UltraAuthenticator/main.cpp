// Apps/UltraAuthenticator/main.cpp
// Entry point: unlock the vault, then show the account list.
//
// The unlock prompt is a modal gate at launch rather than an inline banner on
// the list. Nothing about the accounts — not even how many there are — is
// rendered before the password is accepted, which is the behaviour the threat
// model in Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md §3.1
// asks for and the one Aegis and andOTP use.
//
// For an existing vault that gate is the same LockScreenDialog the app uses
// after an idle or minimise lock: the store is attached to the file without
// opening it, the window comes up locked, and the first unlock goes through
// AccountStore::Unlock with its back-off. The earlier launch prompt quit the
// app on a wrong password, which punished a typo with a restart and a guesser
// with nothing more. A *new* vault goes through NewVaultDialog instead: not an
// unlock, so no back-off, but the password is typed twice, because a typo in
// the one password that cannot be recovered would otherwise become it.
//
// There is no "skip" and no "remember me". A vault that can be opened without
// the password is a vault that an attacker with the file can open too, and a
// silent unprotected mode is exactly the hole UltraCrypt exists to close.
//
// Once open, the vault can lock itself again without the app quitting — after
// a period without input, or on minimise — and the window then shows a lock
// screen (AuthenticatorWindow.h). Both timings live in a settings file beside
// the vault (Preferences.h); nothing about them is secret.
//
// One UltraAuthenticator per user: a second start brings the running one
// forward and quits (UltraCanvasSingleInstance.h).
//
// Version: 0.3.1 - the default vault folder comes from VaultLocation.h: Application
//                  Support on macOS, %APPDATA% ahead of HOME on Windows, moved there once
// Version: 0.3.0 - one UltraAuthenticator per user: a second start brings the
//                  first one's window forward instead of opening another copy
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AccountStore.h"
#include "AuthenticatorWindow.h"
#include "NewVaultDialog.h"
#include "Preferences.h"
#include "VaultLocation.h"   // DefaultVaultPath

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasModalDialog.h"
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

using namespace UltraCanvas;
using namespace UltraCanvas::Authenticator;

// Set from the first line of Docs/UltraAuthenticator/CHANGELOG.md by the build
// (cmake/UltraCanvasVersion.cmake), so --version cannot drift from the release.
#ifndef ULTRAAUTHENTICATOR_VERSION
#define ULTRAAUTHENTICATOR_VERSION "0.0.0-dev"
#endif

namespace {

// Kept alive for as long as it is on screen; there is no window to own it yet.
std::shared_ptr<NewVaultDialog> g_newVaultDialog;

#ifdef __linux__
// The one call a signal handler may make: it sets a flag the main loop
// turns into RequestExit, so main returns and the destructors run in order.
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* prog) {
    std::cout << "UltraAuthenticator — TOTP/HOTP codes for ULTRA OS.\n"
              << "Usage: " << prog << " [options]\n"
              << "  --vault PATH   use this vault file\n"
              << "  -h, --help     show this message\n"
              << "  -v, --version  show version\n";
}

// Bridges a dialog's std::string into a secure buffer and wipes the copy we
// own. The framework's own copy inside the text field is outside our reach —
// a genuine limitation, and the reason a future UltraCanvasTextInput that can
// hand over a secure buffer directly would be worth having.
UltraCryptSecureBuffer AdoptPassword(const std::string& typed) {
    std::string copy = typed;
    return UltraCryptSecureBuffer::AdoptString(copy);
}

// Settings live beside the vault, so a custom --vault path carries its own.
std::string PreferencesPathFor(const std::string& vaultPath) {
    namespace fs = std::filesystem;
    return PathToUtf8(PathFromUtf8(vaultPath).parent_path() / "settings.ini");
}

bool CreateMainWindow(UltraCanvasApplication& app, AccountStore& store,
                      const std::string& vaultPath,
                      std::shared_ptr<AuthenticatorWindow>& windowOut) {
    const std::string prefsPath = PreferencesPathFor(vaultPath);
    windowOut = std::make_shared<AuthenticatorWindow>(
        app, store, Preferences::Load(prefsPath), prefsPath);
    if (!windowOut->Create()) {
        std::cerr << "Failed to create the main window\n";
        app.RequestExit();
        return false;
    }
    windowOut->Show();
    return true;
}

// Opens (or creates) the vault, then hands off to the window.
void OpenVaultThen(UltraCanvasApplication& app, AccountStore& store,
                   const std::string& vaultPath,
                   std::shared_ptr<AuthenticatorWindow>& windowOut) {
    if (AccountStore::Exists(vaultPath)) {
        // Existing vault: attach without opening, and let the window's lock
        // screen do the unlock — throttled, retryable, and the same dialog
        // the user meets after every later lock.
        StoreResult attached = store.Attach(vaultPath);
        if (!attached) {
            UltraCanvasDialogManager::ShowError(
                attached.message, "Could not open the vault");
            app.RequestExit();
            return;
        }
        CreateMainWindow(app, store, vaultPath, windowOut);
        return;
    }

    // New vault: choose a password, twice. The dialog is asynchronous, so the
    // rest is a continuation.
    g_newVaultDialog = std::make_shared<NewVaultDialog>();
    g_newVaultDialog->onAccept = [&store, vaultPath](const std::string& typed) -> std::string {
        UltraCryptSecureBuffer password = AdoptPassword(typed);
        StoreResult created = store.Create(vaultPath, password);
        return created ? std::string() : created.message;
    };
    g_newVaultDialog->onQuit = [&app]() { app.RequestExit(); };
    g_newVaultDialog->onResult = [&app, &store, vaultPath, &windowOut](DialogResult result) {
        g_newVaultDialog.reset();
        if (result != DialogResult::OK) return;   // quit; the app is exiting
        CreateMainWindow(app, store, vaultPath, windowOut);
    };
    g_newVaultDialog->CreateNewVaultDialog();
    g_newVaultDialog->ShowModal(nullptr);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string vaultPath;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")    { PrintUsage(argv[0]); return 0; }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraAuthenticator " << ULTRAAUTHENTICATOR_VERSION << "\n";
            return 0;
        }
        if (a == "--vault" && i + 1 < argc) { vaultPath = argv[++i]; continue; }
    }
    if (vaultPath.empty()) {
        // The platform's application data folder (VaultLocation.h); a vault
        // 0.2.6 kept in ~/.local/share on macOS, or on Windows with HOME set,
        // is moved there once, settings included.
        std::string movedFrom, moveError;
        vaultPath = DefaultVaultPath(&movedFrom, &moveError);
        if (!movedFrom.empty())
            std::cerr << "UltraAuthenticator: moved the vault and its settings from " << movedFrom
                      << " to " << PathToUtf8(PathFromUtf8(vaultPath).parent_path()) << "\n";
        else if (!moveError.empty())
            std::cerr << "UltraAuthenticator: " << moveError << "\n";
    }

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

    // One UltraAuthenticator per user, decided before any window opens: two
    // copies would hold the same vault open and could each write it.
    UltraCanvasSingleInstance instance("org.ultraos.ultraauthenticator", "UltraAuthenticator");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

    UltraCanvasApplication app;

    AccountStore store;
    std::shared_ptr<AuthenticatorWindow> window;

    try {
        if (!app.Initialize("UltraAuthenticator")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }

        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning) {
            // The other copy holds the lock but did not answer: say so
            // rather than vanish without a word.
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraAuthenticator");
        }

        // One icon, everywhere the app is drawn: the window and the taskbar
        // entry that follows it read this file; the .ico embedded in the
        // Windows binary and the desktop entry's theme icon are rendered from
        // the same media/appicon/UltraAuthenticator.svg (see CMakeLists.txt).
        app.SetDefaultWindowIcon(
            NormalizePath(GetResourcesDir() + "media/appicon/UltraAuthenticator.png"));

        // Use the framework's own dialogs: the native ones cannot be given a
        // password field on every platform.
        UltraCanvasDialogManager::SetUseNativeDialogs(false);

        OpenVaultThen(app, store, vaultPath, window);

        // A later start asks this copy to come forward: the account list, or
        // the unlock / new-vault dialog in front of it.
        instance.ListenRaise();

        app.Run();
        instance.Close();   // no request reaches the window once it starts to go
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        instance.Close();
        store.Close();
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        instance.Close();
        store.Close();
        return EXIT_FAILURE;
    }

    store.Close();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
