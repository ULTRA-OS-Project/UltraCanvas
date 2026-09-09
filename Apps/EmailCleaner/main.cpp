// Apps/EmailCleaner/main.cpp
// EmailCleaner entry point. Opens the analysis database under the user data
// directory, picks up the accounts and cached mail UltraMail keeps, shows the
// main window (sender map, timetable, messages) and runs the main loop.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/EmailCleanerApp.h"

#include "UltraCanvasApplication.h"

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
    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("EmailCleaner"))
        return EXIT_FAILURE;

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

    app.Run();
    return EXIT_SUCCESS;
}
