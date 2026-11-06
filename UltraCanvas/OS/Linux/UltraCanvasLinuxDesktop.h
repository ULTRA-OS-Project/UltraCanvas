// OS/Linux/UltraCanvasLinuxDesktop.h
// INTERNAL: what the Linux backends know about the session they run in -
// the config home, which desktop runs the session, whether one runs at all -
// and the GSettings readers, shared by the font detector
// (UltraCanvasLinuxSystemFont.cpp) and the host appearance detector
// (UltraCanvasLinuxHostAppearance.cpp). Not a public header.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace UltraCanvas {
namespace LinuxDesktop {

    // $XDG_CONFIG_HOME, else ~/.config; empty when neither is known.
    std::filesystem::path ConfigHome();
    // The whole file as text; empty when it is not there.
    std::string ReadWholeFile(const std::filesystem::path& path);
    // XDG_CURRENT_DESKTOP names KDE.
    bool IsKdeSession();
    // A desktop environment is running this session (XDG_CURRENT_DESKTOP or
    // DESKTOP_SESSION set). Without one, GSettings answers only its compiled
    // defaults, which nobody chose, so it is not asked.
    bool IsDesktopSession();

    // A key of a GSettings schema through GIO, looked up rather than opened
    // blind (g_settings_new aborts on a missing schema): nothing when the
    // schema or the key is not installed.
    std::string GSettingsString(const char* schema, const char* key);
    std::optional<bool> GSettingsBoolean(const char* schema, const char* key);
    std::optional<double> GSettingsDouble(const char* schema, const char* key);

} // namespace LinuxDesktop
} // namespace UltraCanvas
