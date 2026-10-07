// Apps/UltraSocial/main.cpp
// UltraSocial application entry point. Creates the UltraCanvas application,
// opens the store and the credential vault in the per-user application data
// folder (UltraSocialPaths.h), shows the main window and runs the main loop.
// Version: 0.2.0 - the app icon on the window and the taskbar; the data in
//                  the platform's application data folder (%APPDATA% on
//                  Windows, Application Support on macOS), moved there once
//                  from where 0.1.x left it
// Version: 0.1.0 (Phase 1)
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraSocialApp.h"

#include "UltraSocialPaths.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8 / PathToUtf8
#include "UltraCanvasUtils.h"

#include <UltraNet/UltraNetCore.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// The per-user application data folder (UltraSocialPaths.h), with whatever
// an earlier version left elsewhere moved into it on the first start.
std::string UserDataDir() {
    using namespace UltraSocial;
    std::string dataDir = DefaultDataDir();
    if (dataDir.empty()) {
        // No home of any kind in the environment: beside the executable,
        // which at least does not move with the working directory. Not
        // "UltraSocial", which outside Windows is the executable itself.
        dataDir = UltraCanvas::GetExecutableDir() + "/UltraSocialData";
    }

    std::error_code ec;
    const std::string currentDir = UltraCanvas::PathToUtf8(std::filesystem::current_path(ec));
    const auto legacy = LegacyDataDirs(CurrentDataDirPlatform(),
                                       UltraCanvas::GetEnvUtf8("HOME"), currentDir,
                                       UltraCanvas::GetExecutableDir());
    std::string error;
    const std::string movedFrom = AdoptLegacyDataDir(dataDir, legacy, error);
    if (!movedFrom.empty())
        std::fprintf(stderr, "UltraSocial: moved the accounts and the credential vault "
                             "from %s to %s\n", movedFrom.c_str(), dataDir.c_str());
    else if (!error.empty())
        std::fprintf(stderr, "UltraSocial: %s\n", error.c_str());
    return dataDir;
}

} // namespace

int main() {
    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraSocial"))
        return EXIT_FAILURE;
    // The application's own mark: the window icon, the taskbar/dock entry the
    // window manager takes from it, and the start page and toolbar logos all
    // read this same file.
    app.SetDefaultWindowIcon(
        UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/appicon/UltraSocial.png"));

    UltraNet_Initialize();   // connectors speak HTTPS

    UltraSocial::UltraSocialApp social;
    if (!social.Initialize(UserDataDir()))
        return EXIT_FAILURE;

    auto window = social.CreateMainWindow();
    window->Show();

    app.Run();
    UltraNet_Shutdown();
    return EXIT_SUCCESS;
}
