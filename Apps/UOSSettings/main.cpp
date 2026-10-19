// Apps/UOSSettings/main.cpp
// UOS-Settings, the ULTRA OS settings application: settings that belong to
// the system rather than to one application - starting with where the
// file dialogs of all applications open. One UOS-Settings per user: a second
// start brings the running one forward and quits (UltraCanvasSingleInstance.h).
// Version: 0.2.0 - one UOS-Settings per user: a second start brings the first
//                  one's window forward instead of opening another copy
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UOSSettingsWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef UOSSETTINGS_VERSION
#define UOSSETTINGS_VERSION "0.0"
#endif

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("UOS-Settings %s\n", UOSSETTINGS_VERSION);
            return EXIT_SUCCESS;
        }
    }

    // --version above runs alongside an open window; only the window claims.
    using UltraCanvas::UltraCanvasSingleInstance;
    UltraCanvasSingleInstance instance("org.ultraos.uossettings", "UOS-Settings");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy came forward

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UOS-Settings")) {
        std::fprintf(stderr, "UOS-Settings: the UltraCanvas application could not "
                             "be initialised (no display?).\n");
        return EXIT_FAILURE;
    }
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UOS-Settings");
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(
            UltraCanvas::GetResourcesDir() + "media/icons/settings.svg"));

    UOSSettings::UOSSettingsWindow window(UOSSETTINGS_VERSION);
    if (!window.Create()) {
        std::fprintf(stderr, "UOS-Settings: the window could not be created.\n");
        return EXIT_FAILURE;
    }
    window.onClosed = [&app]() { app.Exit(); };
    window.Show();
    instance.ListenRaise();   // a later start brings this window forward
    app.Run();
    instance.Close();         // no request reaches the window once it starts to go
    return EXIT_SUCCESS;
}
