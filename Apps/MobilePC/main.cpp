// Apps/MobilePC/main.cpp
// MobilePC: UltraDesktop on a TV or monitor, run by an Android phone.
//
// Connected to an external display (a USB-C cable or dock, an OEM wireless
// display) the desktop - UltraDesktop, and the applications its Apps window
// starts - is drawn there by the framework's desktop mode, its windows kept
// by the embedded window manager; the phone shows the control pad. With no
// display the desktop is on the phone's own screen (a tablet with a keyboard
// is a small computer that way).
//
// A shared library, libMobilePC.so, which UltraCanvasActivity loads; this is
// its ultracanvas_app_main(), called by the framework's android_main(). Its
// arguments come from the launching intent's ultracanvas.args extra:
//   --launch <id>   start that application once the desktop is up (an id of
//                   MobilePCHostedApps: ultraviewer, ultrapaint, artcreator)
//   --debug         the framework's diagnostics, to logcat
//
// Version: 0.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework / ULTRA OS

#include "MobilePCControlPad.h"
#include "MobilePCHostedApps.h"

#include "UltraCanvasAndroidDesktop.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasHostedApps.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"
#include "UltraDesktopSettings.h"
#include "UltraDesktopWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using namespace UltraCanvas;

extern "C" int ultracanvas_app_main(int argc, char** argv) {
    std::vector<std::string> launch;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) SetDebugOutputEnabled(true);
        else if (std::strcmp(argv[i], "--launch") == 0 && i + 1 < argc) launch.push_back(argv[++i]);
    }

    // The texts of the desktop and of the applications it carries are in
    // their own catalogues: looked up after MobilePC's, as UOS-Settings takes
    // UltraDesktop's (UltraViewer has none of its own).
    for (const char* set : {"UltraDesktop", "UltraPaint", "ArtCreator"}) UltraLocalisation::AddSet(set);

    UltraCanvasApplication app;
    if (!app.Initialize("MobilePC")) {
        std::fprintf(stderr, "MobilePC: the UltraCanvas application could not be initialised\n");
        return EXIT_FAILURE;
    }
    app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/MobilePC.png"));
    // The framework's own dialogs: Android's open on the phone, these on the
    // TV with the window that asks.
    UltraCanvasDialogManager::SetUseNativeDialogs(false);

    MobilePC::RegisterHostedApps();

    // From here on every window is one of the desktop's - except the
    // control pad, which the phone shows while the desktop is on the TV.
    UltraCanvasAndroidDesktop& desktop = UltraCanvasAndroidDesktop::Get();
    desktop.Enable();

    int exitCode = EXIT_SUCCESS;
    try {
        UltraDesktop::UltraDesktopWindow desktopWindow;
        MobilePC::MobilePCControlPad pad;
        pad.onApps = [&desktopWindow]() { desktopWindow.OpenAppStarter(); };
        if (!pad.Create()) {
            debugOutput << "MobilePC: the control pad could not be made" << std::endl;
        }
        desktop.onOutputChanged = [&pad](UltraCanvasAndroidDesktop::Output output) {
            pad.ShowOutput(output == UltraCanvasAndroidDesktop::Output::External);
        };

        if (!desktopWindow.Initialize(UltraDesktop::DesktopSettings::DefaultPath(), nullptr)) {
            debugOutput << "MobilePC: the desktop window could not be made" << std::endl;
            exitCode = EXIT_FAILURE;
        } else {
            desktopWindow.Show();
            // Applications asked for on the command line, once the loop runs.
            // Each start is logged straight to logcat: the emulator test
            // (scripts/android-mobilepc-smoke.sh) waits for it.
            if (!launch.empty()) {
                app.PostToUIThread([launch]() {
                    for (const std::string& id : launch) {
                        std::string error;
                        const bool ok = UltraCanvasHostedApps::Launch(id, {}, &error);
                        const std::string line = ok ? "MobilePC: started " + id
                                                    : "MobilePC: " + id + " did not start: " + error;
                        __android_log_write(ok ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "UltraCanvas", line.c_str());
                    }
                });
            }
            app.Run();
        }
        desktop.onOutputChanged = nullptr;
    } catch (const std::exception& e) {
        debugOutput << "MobilePC: unhandled exception: " << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }
    return exitCode;
}
