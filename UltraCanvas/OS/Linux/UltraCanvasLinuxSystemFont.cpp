// OS/Linux/UltraCanvasLinuxSystemFont.cpp
// The desktop's interface and monospace fonts on Linux and the BSDs:
// LinuxFonts::DetectSystemFontStyle and DetectMonospacedFontStyle, which the
// X11 and the Wayland backends answer their DetectSystemFontStyleNative /
// DetectMonospacedFontStyleNative with. Until 0.9.362 both returned the literal
// Ubuntu 12, so "the system font" was never the system's. Sources, first
// answer wins, the parsers in UltraCanvasSystemFont.h:
//   - a KDE session (XDG_CURRENT_DESKTOP names KDE): ~/.config/kdeglobals
//     [General] font= and fixed=, then GSettings
//   - otherwise GSettings org.gnome.desktop.interface font-name and
//     monospace-font-name (read through GIO, which the core links with
//     GTK 3; a machine without the schema answers nothing), then kdeglobals
//   - ~/.config/gtk-4.0/settings.ini and gtk-3.0/settings.ini gtk-font-name
//     (no monospace key there)
//   - the bundled Ubuntu / Ubuntu Mono at 12 pt
// GSettings is asked only inside a desktop session (XDG_CURRENT_DESKTOP or
// DESKTOP_SESSION set): with no desktop running - a bare window manager, an
// SSH session, a CI runner under Xvfb - the schema answers its compiled
// default, Cantarell 11, which nobody chose; GTK itself reads GSettings
// only through the desktop's settings daemon and falls back to its files.
// Version: 1.3.0 - the session helpers and the GSettings readers are LinuxDesktop's
//                  (UltraCanvasLinuxDesktop.h), shared with the host appearance detector
// Version: 1.2.0 - GSettings only inside a desktop session; the layout audit under Xvfb got Cantarell 11
// Version: 1.1.0 - the detectors are LinuxFonts::DetectSystemFontStyle / DetectMonospacedFontStyle, shared with the Wayland backend
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#include "UltraCanvasLinuxFonts.h"
#include "UltraCanvasLinuxDesktop.h"
#include "UltraCanvasSystemFont.h"
#include "UltraCanvasPathUtf8.h"

#include <gio/gio.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace UltraCanvas {

// ===== THE SESSION (UltraCanvasLinuxDesktop.h) =====

std::string LinuxDesktop::ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::filesystem::path LinuxDesktop::ConfigHome() {
    const std::string xdg = GetEnvUtf8("XDG_CONFIG_HOME");
    if (!xdg.empty()) return PathFromUtf8(xdg);
    const std::string home = GetEnvUtf8("HOME");
    if (!home.empty()) return PathFromUtf8(home) / ".config";
    return {};
}

bool LinuxDesktop::IsKdeSession() {
    const std::string desktop = SystemFontDetail::Lower(GetEnvUtf8("XDG_CURRENT_DESKTOP"));
    return desktop.find("kde") != std::string::npos;
}

bool LinuxDesktop::IsDesktopSession() {
    return !GetEnvUtf8("XDG_CURRENT_DESKTOP").empty() || !GetEnvUtf8("DESKTOP_SESSION").empty();
}

namespace {

// The schema's settings object when the schema and the key are installed;
// the caller unrefs it.
GSettings* OpenSchemaKey(const char* schema, const char* key) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (!source) return nullptr;
    GSettingsSchema* found = g_settings_schema_source_lookup(source, schema, TRUE);
    if (!found) return nullptr;
    GSettings* settings = nullptr;
    if (g_settings_schema_has_key(found, key)) settings = g_settings_new_full(found, nullptr, nullptr);
    g_settings_schema_unref(found);
    return settings;
}

}  // namespace

std::string LinuxDesktop::GSettingsString(const char* schema, const char* key) {
    std::string result;
    if (GSettings* settings = OpenSchemaKey(schema, key)) {
        if (gchar* value = g_settings_get_string(settings, key)) {
            result = value;
            g_free(value);
        }
        g_object_unref(settings);
    }
    return result;
}

std::optional<bool> LinuxDesktop::GSettingsBoolean(const char* schema, const char* key) {
    std::optional<bool> result;
    if (GSettings* settings = OpenSchemaKey(schema, key)) {
        result = g_settings_get_boolean(settings, key) != FALSE;
        g_object_unref(settings);
    }
    return result;
}

std::optional<double> LinuxDesktop::GSettingsDouble(const char* schema, const char* key) {
    std::optional<double> result;
    if (GSettings* settings = OpenSchemaKey(schema, key)) {
        result = g_settings_get_double(settings, key);
        g_object_unref(settings);
    }
    return result;
}

namespace {

using LinuxDesktop::ConfigHome;
using LinuxDesktop::GSettingsString;
using LinuxDesktop::IsDesktopSession;
using LinuxDesktop::IsKdeSession;
using LinuxDesktop::ReadWholeFile;

bool FromGSettings(bool monospace, FontStyle& out) {
    if (!IsDesktopSession()) return false;
    const std::string value = GSettingsString("org.gnome.desktop.interface",
                                              monospace ? "monospace-font-name" : "font-name");
    return !value.empty() && ParseGtkFontName(value, out.fontFamily, out.fontSize);
}

bool FromKdeGlobals(bool monospace, FontStyle& out) {
    const std::filesystem::path home = ConfigHome();
    if (home.empty()) return false;
    const std::string text = ReadWholeFile(home / "kdeglobals");
    if (text.empty()) return false;
    const std::string value = ReadIniValue(text, "General", monospace ? "fixed" : "font");
    return !value.empty() && ParseKdeFontEntry(value, out.fontFamily, out.fontSize);
}

bool FromGtkSettingsIni(FontStyle& out) {
    const std::filesystem::path home = ConfigHome();
    if (home.empty()) return false;
    for (const char* file : {"gtk-4.0/settings.ini", "gtk-3.0/settings.ini"}) {
        const std::string text = ReadWholeFile(home / file);
        if (text.empty()) continue;
        const std::string value = ReadIniValue(text, "Settings", "gtk-font-name");
        if (!value.empty() && ParseGtkFontName(value, out.fontFamily, out.fontSize)) return true;
    }
    return false;
}

FontStyle DetectDesktopFont(bool monospace) {
    FontStyle result;
    result.fontFamily = monospace ? "Ubuntu Mono" : "Ubuntu";
    result.fontSize = 12.0;
    if (IsKdeSession()) {
        if (FromKdeGlobals(monospace, result)) return result;
        if (FromGSettings(monospace, result)) return result;
    } else {
        if (FromGSettings(monospace, result)) return result;
        if (FromKdeGlobals(monospace, result)) return result;
    }
    if (!monospace && FromGtkSettingsIni(result)) return result;
    return result;
}

}  // namespace

FontStyle LinuxFonts::DetectSystemFontStyle() {
    return DetectDesktopFont(false);
}

FontStyle LinuxFonts::DetectMonospacedFontStyle() {
    return DetectDesktopFont(true);
}

}  // namespace UltraCanvas
