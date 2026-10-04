// Apps/UOSSettings/main.cpp
// UOS-Settings, the ULTRA OS settings application: settings that belong to
// the system rather than to one application - starting with where the
// file dialogs of all applications open.
// Version: 0.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UOSSettingsWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
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

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UOS-Settings")) {
        std::fprintf(stderr, "UOS-Settings: the UltraCanvas application could not "
                             "be initialised (no display?).\n");
        return EXIT_FAILURE;
    }
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(
            UltraCanvas::GetResourcesDir() + "media/icons/settings.svg"));

    UOSSettings::UOSSettingsWindow window(UOSSETTINGS_VERSION);
    if (!window.Create()) {
        std::fprintf(stderr, "UOS-Settings: the window could not be created.\n");
        return EXIT_FAILURE;
    }
    window.onClosed = [&app]() { app.Exit(); };
    window.Show();
    app.Run();
    return EXIT_SUCCESS;
}
