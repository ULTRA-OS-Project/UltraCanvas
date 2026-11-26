// include/UltraCanvasLaunchAtLogin.h
// Starting a program when the user signs in to the computer - what a settings
// page offers as "Start: manually / when the computer starts":
//
//   LaunchAtLoginEntry entry;
//   entry.id = "org.ultraos.ChatCube";
//   entry.displayName = "ChatCube";
//   entry.arguments = {"--minimized"};
//   std::string error;
//   if (!SetLaunchAtLogin(entry, true, error)) ShowError(error);
//   ...
//   bool on = IsLaunchAtLoginEnabled(entry.id);
//
// The computer's own startup list is the record, not a setting kept by the
// program: a program asks IsLaunchAtLoginEnabled each time it shows the
// choice, so one switched off in the desktop's own startup settings (or the
// Windows Task Manager's Startup tab) reads as off here too.
//
// Backends:
//   Linux, BSD  an XDG autostart entry, $XDG_CONFIG_HOME/autostart/<id>.desktop
//               (~/.config/autostart), which GNOME, KDE, Xfce, LXQt, Cinnamon,
//               MATE and UltraDesktop's session all start. An entry of the
//               same name the system installs (/etc/xdg/autostart) is turned
//               off by a Hidden=true one in the user's folder. Inside an
//               AppImage the entry starts the AppImage ($APPIMAGE). Not
//               available in a Flatpak or a Snap, whose config folder is
//               their own (the Background portal is what those would need).
//   Windows     a value named <id> under
//               HKCU\Software\Microsoft\Windows\CurrentVersion\Run. One the
//               Task Manager's Startup tab turned off
//               (Explorer\StartupApproved\Run) reads as off; turning it on
//               here clears that. Not available in an MSIX package, whose
//               HKCU writes stay inside the package (a StartupTask in the
//               manifest is what that would need).
//   macOS       a LaunchAgent, ~/Library/LaunchAgents/<id>.plist, with
//               RunAtLoad; a program inside an .app bundle is started
//               through `open -a <bundle>`, so it starts as an application.
// Android, iOS and WebAssembly have none: LaunchAtLoginAvailable() is false
// there and SetLaunchAtLogin fails, so a caller leaves the choice out.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // What is started when the user signs in.
    struct LaunchAtLoginEntry {
        // The entry's name in the startup list: the .desktop file's name, the
        // registry value's, the LaunchAgent's label. Reverse-DNS by
        // convention ("org.ultraos.ChatCube"); letters, digits, '.', '-' and
        // '_' only.
        std::string id;
        // The program's name, where the desktop shows the list ("ChatCube").
        std::string displayName;
        // One line on what it is, for the Linux entry's Comment (optional).
        std::string comment;
        // The icon name the Linux entry carries (optional, "ChatCube").
        std::string iconName;
        // The program to start (UTF-8). Empty: this program (inside an
        // AppImage the AppImage, inside a macOS .app bundle the bundle).
        std::string executable;
        // Its arguments ("--minimized").
        std::vector<std::string> arguments;
    };

    // Whether this platform - and this kind of install - can start a program
    // at sign-in. A particular entry can still fail (a folder that cannot be
    // written); SetLaunchAtLogin answers for that one.
    bool LaunchAtLoginAvailable();

    // Whether an entry named `id` is in the startup list and switched on.
    bool IsLaunchAtLoginEnabled(const std::string& id);

    // Adds the entry to the startup list (`enabled`), replacing one of the
    // same id, or takes it out. False with a short, user-readable reason in
    // `error` when that could not be done; the list is then as it was.
    bool SetLaunchAtLogin(const LaunchAtLoginEntry& entry, bool enabled, std::string& error);

    // The command line an entry starts, as the platform will run it - for a
    // settings page to show ("/opt/ChatCube/ChatCube --minimized"). Empty
    // where LaunchAtLoginAvailable() is false.
    std::string LaunchAtLoginCommand(const LaunchAtLoginEntry& entry);

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use the functions above, which
    // check the id first. Platforms without a backend get the fallback in
    // core/UltraCanvasLaunchAtLogin.cpp; CMake sets
    // ULTRACANVAS_HAS_NATIVE_LAUNCH_AT_LOGIN where one exists.
    namespace LaunchAtLoginBackend {
        bool Available();
        bool IsEnabled(const std::string& id);
        bool Set(const LaunchAtLoginEntry& entry, bool enabled, std::string& error);
        std::string Command(const LaunchAtLoginEntry& entry);
    }

} // namespace UltraCanvas
