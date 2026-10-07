// Apps/UltraMail/main.cpp
// UltraMail application entry point. Creates the UltraCanvas application, opens
// the local store under the user data directory, shows the main window (start
// page, or the account bar + mail view once an account exists) and runs the
// main loop.
// Version: 0.6.0 - the timing trace: each step of the start, with its time, on
//                  the console and in trace.log (UltraMailTrace.h)
// Version: 0.5.2 - the data folder's location is read as UTF-8
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraMailApp.h"
#include "ui/UltraMailSettingsDialog.h"
#include "ui/UltraMailAlerts.h"

#include "UltraMailTrace.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace {

// Per-platform user data directory for UltraMail: XDG on Linux, %APPDATA% on
// Windows (where HOME is normally unset, so without this the mailbox database
// and the credential vault landed in whatever folder the app was started from).
// Read as UTF-8 (GetEnvUtf8), which the databases, the vault and the
// preferences file all take: on Windows a narrow getenv answers in the ANSI
// code page and misses a profile folder named outside it.
std::string UserDataDir() {
    using UltraCanvas::GetEnvUtf8;
    if (const std::string xdg = GetEnvUtf8("XDG_DATA_HOME"); !xdg.empty())
        return xdg + "/UltraMail";
#if defined(_WIN32) || defined(_WIN64)
    if (const std::string appData = GetEnvUtf8("APPDATA"); !appData.empty())
        return appData + "/UltraMail";
#endif
    if (const std::string home = GetEnvUtf8("HOME"); !home.empty())
        return home + "/.local/share/UltraMail";
    return "./UltraMail";
}

// The timing trace (UltraMailTrace.h) is on unless ULTRAMAIL_TRACE says
// otherwise ("0", "off", "no", "false").
bool TraceWanted() {
    const std::string value = UltraCanvas::GetEnvUtf8("ULTRAMAIL_TRACE");
    return !(value == "0" || value == "off" || value == "no" || value == "false");
}

} // namespace

int main() {
    namespace Trace = UltraMail::Trace;
    Trace::Enable(TraceWanted());
    Trace::MarkUiThread();
    {
        const double beforeMain = Trace::ProcessStartToTraceMs();
        Trace::Line("UltraMail " ULTRAMAIL_VERSION " starting" +
                    (beforeMain >= 0 ? "; the process started " + Trace::FormatMs(beforeMain) +
                                       " before main() (loading the program and its libraries)"
                                     : std::string()));
        Trace::Line("Timing trace on: every line has the time of day, the seconds since "
                    "the process started and the thread; ULTRAMAIL_TRACE=0 turns it off");
    }

    std::optional<Trace::Stage> step;
    step.emplace("Startup: framework application object");
    UltraCanvas::UltraCanvasApplication app;
    step.emplace("Startup: framework initialisation (fonts, images, windowing)");
    const bool initialised = app.Initialize("UltraMail");
    step.reset();
    for (const auto& timing : app.GetStartupTimings())
        Trace::Line("  framework step " + timing.stage + ": " + Trace::FormatMs(timing.ms));
    if (!initialised) {
        // There is no UI to alert with yet — this is the one failure that has
        // to go to the console.
        std::fprintf(stderr,
                     "UltraMail: the UltraCanvas application could not be "
                     "initialised (no display?).\n");
        return EXIT_FAILURE;
    }
    // The application's own mark, and the one image UltraMail is recognized by:
    // the window icon, the taskbar/dock entry the window manager takes from it,
    // and the start-page logo below all read this same file. Keep them together
    // — an app wearing two icons is an app the user has to learn twice.
    app.SetDefaultWindowIcon(
        UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/appicon/UltraMail.png"));

    UltraMail::UltraMailApp mail;
    std::string storeError;
    step.emplace("Startup: open the mailbox, settings and vault (UltraMailApp::Initialize)");
    const bool opened = mail.Initialize(UserDataDir(), &storeError);
    step.reset();
    // Everything said so far goes into the file too.
    if (Trace::Enabled()) Trace::SetLogFile(UserDataDir() + "/trace.log");
    if (!opened) {
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

    step.emplace("Startup: build the main window (UltraMailApp::CreateMainWindow)");
    auto window = mail.CreateMainWindow();
    step.emplace("Startup: show the main window");
    window->Show();
    step.reset();

    // From here the event loop runs: the first frame, the first mail check
    // and every account switch are reported by the app as they happen, and
    // the watchdog says when the window stops answering.
    Trace::StartUiWatchdog([&app](std::function<void()> task) { app.PostToUIThread(std::move(task)); });
    Trace::Line("Startup: event loop starting");
    app.Run();
    Trace::StopUiWatchdog();
    Trace::Line("UltraMail closing");
    // The settings window's widgets go while the application is still alive,
    // not at static destruction after main() returns.
    UltraMail::SettingsDialog::Shutdown();
    return EXIT_SUCCESS;
}
