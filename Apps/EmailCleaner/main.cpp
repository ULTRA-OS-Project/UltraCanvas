// Apps/EmailCleaner/main.cpp
// EmailCleaner entry point. Opens the analysis database under the user data
// directory, picks up the accounts and cached mail UltraMail keeps, shows the
// main window (sender map, timetable, messages) and runs the main loop. One
// EmailCleaner per user: a second start brings the running one forward and
// quits (UltraCanvasSingleInstance.h).
// Version: 0.2.0 - one EmailCleaner per user: a second start brings the first
//                  one's window forward instead of opening another copy
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/EmailCleanerApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"    // NormalizePath

#include <cstdlib>
#include <string>

namespace {

// Per-platform user data directory for an app.
std::string UserDataDir(const std::string& appName) {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::string(xdg) + "/" + appName;
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.local/share/" + appName;
    return "./" + appName;
}

} // namespace

int main() {
    // One EmailCleaner per user, decided before anything else is opened: two
    // copies would analyse, and delete from, the same mailboxes at once.
    using UltraCanvas::UltraCanvasSingleInstance;
    UltraCanvasSingleInstance instance("org.ultraos.emailcleaner", "EmailCleaner");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff)
        return EXIT_SUCCESS;

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("EmailCleaner"))
        return EXIT_FAILURE;
    // The other copy holds the lock but did not answer: say so rather than
    // vanish without a word.
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "EmailCleaner");
    // One icon, everywhere the app is drawn: the window and the taskbar entry
    // that follows it read this file; the .ico embedded in the Windows binary
    // and the desktop entry's theme icon are rendered from the same
    // media/appicon/EmailCleaner.svg (see CMakeLists.txt).
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(
        UltraCanvas::GetResourcesDir() + "media/appicon/EmailCleaner.png"));

    // The mail itself lives in UltraMail's data directory; EMAILCLEANER_MAIL_DIR
    // points elsewhere when the two apps are not sharing a home (a second
    // profile, or a mailbox copied over for analysis).
    std::string mailDataDir = UserDataDir("UltraMail");
    if (const char* fromEnv = std::getenv("EMAILCLEANER_MAIL_DIR"); fromEnv && *fromEnv)
        mailDataDir = fromEnv;

    EmailCleaner::EmailCleanerApp cleaner;
    if (!cleaner.Initialize(UserDataDir("EmailCleaner"), mailDataDir))
        return EXIT_FAILURE;

    auto window = cleaner.CreateMainWindow();
    window->Show();
    // A later start asks this window to come forward.
    instance.ListenRaise();

    app.Run();
    instance.Close();   // no request reaches the window once it starts to go
    return EXIT_SUCCESS;
}
