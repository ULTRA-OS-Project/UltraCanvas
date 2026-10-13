// Apps/UltraMail/main.cpp
// UltraMail application entry point. Creates the UltraCanvas application, opens
// the local store under the user data directory, shows the main window (start
// page, or the account bar + mail view once an account exists) and runs the
// main loop. One UltraMail per user: a second start brings the running one
// forward and quits (UltraCanvasSingleInstance.h).
// Version: 0.7.1 - the data folder is Application Support on macOS (UltraMailPaths.h),
//                  and a folder left in ~/.local/share there is moved into place once
// Version: 0.7.0 - one UltraMail per user: a second start brings the first
//                  one's window forward instead of opening another copy
// Version: 0.6.2 - WinMain: Windows Release builds have no console window
// Version: 0.6.1 - says whether the system fonts were ready at the start
// Version: 0.6.0 - the timing trace: each step of the start, with its time, on
//                  the console and in trace.log (UltraMailTrace.h)
// Version: 0.5.2 - the data folder's location is read as UTF-8
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraMailApp.h"
#include "ui/UltraMailSettingsDialog.h"
#include "ui/UltraMailAlerts.h"

#include "UltraMailTrace.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8
#include "UltraCanvasSingleInstance.h"
#include "UltraMailPaths.h"        // DefaultDataDir / AdoptLegacyDataDir

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace {

// The per-user data folder (UltraMailPaths.h): %APPDATA%\UltraMail on
// Windows (where HOME is normally unset, so without this the mailbox database
// and the credential vault landed in whatever folder the app was started
// from), ~/Library/Application Support/UltraMail on macOS, $XDG_DATA_HOME or
// ~/.local/share elsewhere - and $XDG_DATA_HOME wins everywhere. Read as
// UTF-8 (GetEnvUtf8), which the databases, the vault and the preferences file
// all take: on Windows a narrow getenv answers in the ANSI code page and
// misses a profile folder named outside it.
//
// Worked out once: the first call also moves a folder an earlier version left
// in ~/.local/share on macOS into place, whole, so the accounts and the vault
// open as before.
const std::string& UserDataDir() {
    static const std::string dir = [] {
        std::string resolved = UltraMail::DefaultDataDir();
        if (resolved.empty()) return std::string("./UltraMail");
        std::string error;
        const std::string movedFrom = UltraMail::AdoptLegacyDataDir(
            resolved,
            UltraMail::LegacyDataDirs(UltraMail::CurrentDataDirPlatform(),
                                      UltraCanvas::GetEnvUtf8("HOME")),
            error);
        if (!movedFrom.empty())
            UltraMail::Trace::Line("Moved the mailbox, the settings and the credential vault from " +
                                   movedFrom + " to " + resolved);
        else if (!error.empty())
            UltraMail::Trace::Line("Data folder: " + error);
        return resolved;
    }();
    return dir;
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

    // One UltraMail per user, decided before anything else is opened: a
    // second copy would sync, store and send the same accounts twice.
    // The lock sits in the data folder: one UltraMail per mailbox.
    using UltraCanvas::UltraCanvasSingleInstance;
    UltraCanvasSingleInstance instance("org.ultraos.ultramail", "UltraMail");
    const UltraCanvasSingleInstance::Start start =
        instance.Claim(UltraCanvas::JSONValue::MakeObject(), UserDataDir());
    if (start == UltraCanvasSingleInstance::Start::HandedOff) {
        Trace::Line("UltraMail is already running: brought its window forward; this start ends");
        return EXIT_SUCCESS;
    }

    std::optional<Trace::Stage> step;
    step.emplace("Startup: framework application object");
    UltraCanvas::UltraCanvasApplication app;
    step.emplace("Startup: framework initialisation (fonts, images, windowing)");
    const bool initialised = app.Initialize("UltraMail");
    step.reset();
    for (const auto& timing : app.GetStartupTimings())
        Trace::Line("  framework step " + timing.stage + ": " + Trace::FormatMs(timing.ms));
    // Windows scans the system fonts on a thread: in use from the start when
    // the font cache was warm, else the window starts with the bundled fonts
    // and switches when the scan ends (UltraCanvas::AdoptSystemFontsWithin).
    if (const UltraCanvas::SystemFontScanStatus fonts = UltraCanvas::GetSystemFontScanStatus(); fonts.started) {
        if (fonts.adoptedAtStart)
            Trace::Line("  system fonts: scanned in the background in " + Trace::FormatMs(fonts.ms) +
                        ", in use from the start");
        else
            Trace::Line("  system fonts: still being scanned (a cold font cache) - the window "
                        "starts with the bundled fonts and switches when they are ready");
        UltraCanvas::SetSystemFontsSwitchedHandler([](const UltraCanvas::SystemFontScanStatus& done) {
            Trace::Line("System fonts ready after " + Trace::FormatMs(done.ms) +
                        " of scanning: every window now uses them");
        });
    }
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

    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning) {
        // The other copy holds the lock but did not answer (busy, or a build
        // without the message bus). Say so rather than vanish without a word.
        Trace::Line("UltraMail is already running but did not answer; saying so");
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraMail");
    }

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
    // A later start asks this window to come forward.
    instance.Listen([](const UltraCanvas::JSONValue&) {
        Trace::Line("Single instance: another start asked for the window; bringing it forward");
        UltraCanvasSingleInstance::BringToFront();
    });

    // From here the event loop runs: the first frame, the first mail check
    // and every account switch are reported by the app as they happen, and
    // the watchdog says when the window stops answering.
    Trace::StartUiWatchdog([&app](std::function<void()> task) { app.PostToUIThread(std::move(task)); });
    Trace::Line("Startup: event loop starting");
    app.Run();
    Trace::StopUiWatchdog();
    instance.Close();   // no request reaches the window once it starts to go
    Trace::Line("UltraMail closing");
    // The settings window's widgets go while the application is still alive,
    // not at static destruction after main() returns.
    UltraMail::SettingsDialog::Shutdown();
    return EXIT_SUCCESS;
}

// ===== WINDOWS ENTRY POINT =====
// Release builds are GUI-subsystem programs (no console window; see
// CMakeLists.txt), which Windows enters through WinMain. <windows.h> is
// included only here, below everything else, so none of its macros
// (CreateWindow, SendMessage ...) renames a name above.
#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main();
}
#endif
