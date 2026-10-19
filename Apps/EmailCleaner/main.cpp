// Apps/EmailCleaner/main.cpp
// EmailCleaner entry point. Opens the analysis database under the user data
// directory, picks up the accounts and cached mail UltraMail keeps, shows the
// main window (sender map, timetable, messages) and runs the main loop. One
// EmailCleaner per user: a second start brings the running one forward and
// quits (UltraCanvasSingleInstance.h).
// Version: 0.2.1 - the data folders come from the framework's rule
//                  (UltraCanvasDataFolder.h): Application Support on macOS,
//                  %APPDATA% on Windows, read as UTF-8; an analysis folder an
//                  earlier version left elsewhere is moved into place once
// Version: 0.2.0 - one EmailCleaner per user: a second start brings the first
//                  one's window forward instead of opening another copy
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/EmailCleanerApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"       // GetResourcesDir
#include "UltraCanvasDataFolder.h"   // UltraCanvasDataFolder / AdoptLegacyDataFolder
#include "UltraCanvasPathUtf8.h"     // PathToUtf8 / GetEnvUtf8
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"        // NormalizePath

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Per-platform user data directory for an app: the framework's rule
// (UltraCanvasDataFolder.h), the same one UltraMail resolves its own folder
// with - so the mail is found where UltraMail keeps it on every platform. Up
// to 0.4.6 this read XDG_DATA_HOME and HOME only, through the narrow getenv:
// macOS got ~/.local/share and Windows, where HOME is normally unset, the
// working directory.
std::string UserDataDir(const std::string& appName) {
    const std::filesystem::path dir = UltraCanvas::UltraCanvasDataFolder(appName);
    return dir.empty() ? "./" + appName : UltraCanvas::PathToUtf8(dir);
}

// EmailCleaner's own folder, with the analysis database and the rules an
// earlier version left in ~/.local/share (macOS, or Windows with HOME) or in
// the working directory (Windows) moved into place once.
std::string OwnDataDir() {
    const std::string dir = UserDataDir("EmailCleaner");
    const auto platform = UltraCanvas::CurrentDataFolderPlatform();
    std::vector<std::string> legacy =
        UltraCanvas::LegacyUnixDataFolders(platform, UltraCanvas::GetEnvUtf8("HOME"), "EmailCleaner");
    if (platform == UltraCanvas::DataFolderPlatform::Windows) {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) legacy.push_back(UltraCanvas::PathToUtf8(cwd / "EmailCleaner"));
    }
    std::string error;
    const std::string movedFrom = UltraCanvas::AdoptLegacyDataFolder(dir, legacy, {"analysis.db"}, error);
    if (!movedFrom.empty())
        std::fprintf(stderr, "EmailCleaner: moved the analysis database and the rules from %s to %s\n",
                     movedFrom.c_str(), dir.c_str());
    else if (!error.empty())
        std::fprintf(stderr, "EmailCleaner: %s\n", error.c_str());
    return dir;
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
    if (const std::string fromEnv = UltraCanvas::GetEnvUtf8("EMAILCLEANER_MAIL_DIR"); !fromEnv.empty())
        mailDataDir = fromEnv;

    EmailCleaner::EmailCleanerApp cleaner;
    if (!cleaner.Initialize(OwnDataDir(), mailDataDir))
        return EXIT_FAILURE;

    auto window = cleaner.CreateMainWindow();
    window->Show();
    // A later start asks this window to come forward.
    instance.ListenRaise();

    app.Run();
    instance.Close();   // no request reaches the window once it starts to go
    return EXIT_SUCCESS;
}
