// include/UltraCanvasHostedApps.h
// Applications that run inside another program rather than as programs of
// their own: MobilePC on Android carries UltraDesktop and the applications
// it starts in one process, so a desktop's Apps window lists them from here
// and starting one calls its entry point instead of running a program.
//
//   UltraCanvasHostedApps::Register({"texter", "Texter", "A text editor",
//                                    iconPath, {"Office", "TextEditor"},
//                                    [](const std::vector<std::string>& files) {
//                                        return TexterLaunch(files);
//                                    }});
//
// UltraCanvasDesktopShell::ListApplications lists every registered one where
// the platform has no applications of its own to list (Android), each as an
// entry whose shortcutPath is "hosted:<id>"; LaunchApplication with that
// entry calls Launch. An application may also stand in for programs by
// name (`programs`: "UltraFiler"): UltraCanvasDesktopShell::LaunchProgram
// starts it, with the program's arguments, instead of looking for an
// executable - so a desktop button that starts "UltraFiler" starts the hosted
// one.
// Version: 1.1.0 - programs: the program names an application stands in for
//                  (IdForProgram, which UltraCanvasDesktopShell::LaunchProgram asks)
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_HOSTED_APPS_H
#define ULTRACANVAS_HOSTED_APPS_H

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

    struct HostedAppInfo {
        std::string id;                       // stable, lower case: "texter"
        std::string name;                     // what a launcher shows
        std::string comment;                  // its tooltip
        std::string iconFile;                 // an image file
        std::vector<std::string> categories;  // freedesktop categories
        // Opens the application's window(s), with files to open if any - or,
        // started by program name, that program's command-line arguments.
        // False when it could not start; it must not run an event loop of
        // its own - the host's runs every application.
        std::function<bool(const std::vector<std::string>& files)> launch;
        // The programs it stands in for, by executable name ("UltraFiler"):
        // LaunchProgram of any of them starts this application instead.
        std::vector<std::string> programs = {};
    };

    namespace UltraCanvasHostedApps {
        // Adds or replaces (same id) an application.
        void Register(const HostedAppInfo& app);
        std::vector<HostedAppInfo> List();
        bool Has(const std::string& id);
        // Starts it; false with the reason when it is unknown or failed.
        bool Launch(const std::string& id, const std::vector<std::string>& files = {},
                    std::string* error = nullptr);
        // The application whose Launch is running right now ("" outside
        // one): the windows it makes belong to it.
        std::string Launching();

        // "hosted:<id>", the shortcutPath its desktop entry carries.
        std::string PathFor(const std::string& id);
        // The id in such a path, "" for any other path.
        std::string IdFromPath(const std::string& path);
        // The application standing in for `program` - a name or a path, whose
        // file name counts, ".exe" aside, in any letter case - or "".
        std::string IdForProgram(const std::string& program);
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_HOSTED_APPS_H
