// Apps/UltraMail/main.cpp
// UltraMail application entry point. Creates the UltraCanvas application, opens
// the local store under the user data directory, shows the main window (Toolbox
// + account info-tile bar) and runs the main loop.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraMailApp.h"
#include "ui/UltraMailAlerts.h"

#include "UltraCanvasApplication.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// Per-platform user data directory for UltraMail.
std::string UserDataDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::string(xdg) + "/UltraMail";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.local/share/UltraMail";
    return "./UltraMail";
}

} // namespace

int main() {
    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraMail")) {
        // There is no UI to alert with yet — this is the one failure that has
        // to go to the console.
        std::fprintf(stderr,
                     "UltraMail: the UltraCanvas application could not be "
                     "initialised (no display?).\n");
        return EXIT_FAILURE;
    }

    UltraMail::UltraMailApp mail;
    std::string storeError;
    if (!mail.Initialize(UserDataDir(), &storeError)) {
        // The UltraCanvas application is up, so alert properly instead of
        // exiting to a blank screen. Alerts are non-blocking and need the event
        // loop to draw, so run it and leave when the alert is dismissed.
        UltraCanvas::AlertOptions opts;
        opts.severity = UltraCanvas::AlertSeverity::Error;
        opts.title    = "UltraMail";
        opts.message  = "UltraMail could not open its mailbox database, so it "
                        "cannot start.";
        opts.details  = (storeError.empty()
                            ? std::string("The data folder could not be opened.")
                            : storeError)
                        + "\n\nData folder: " + UserDataDir();
        opts.onResult = [&app](UltraCanvas::DialogResult) { app.Exit(); };
        UltraCanvas::UltraCanvasAlert::Show(opts);
        app.Run();
        return EXIT_FAILURE;
    }

    auto window = mail.CreateMainWindow();
    window->Show();

    app.Run();
    return EXIT_SUCCESS;
}
