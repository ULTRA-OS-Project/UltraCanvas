// OS/Linux/UltraCanvasLinuxHostAppearance.cpp
// The desktop's look on Linux and the BSDs: LinuxHostAppearance::Detect,
// which the X11 and the Wayland backends answer their
// DetectHostAppearanceNative with (T2 of the host theme proposal). What is
// read, and decoded by HostAppearanceLinux in UltraCanvasHostAppearance.h:
//   the portal   org.freedesktop.portal.Settings Read("org.freedesktop.appearance",
//                "color-scheme") over the session bus - the one answer every
//                modern desktop gives, GNOME, KDE and a compositor with darkman
//                alike - asked only inside a desktop session, with a 1 s limit
//   GSettings    org.gnome.desktop.interface color-scheme, gtk-theme,
//                accent-color, text-scaling-factor, enable-animations and
//                org.gnome.desktop.a11y.interface high-contrast, inside a
//                desktop session (UltraCanvasLinuxDesktop.h says why)
//   XSETTINGS    the X11 backend's fetch of the manager's property
//   kdeglobals   ~/.config/kdeglobals, always
//   settings.ini ~/.config/gtk-4.0/settings.ini, else gtk-3.0/settings.ini, always
// The fonts come from the font detector, so they follow the same sources
// in the order it already keeps. The change events are the backends':
// XSETTINGS' PropertyNotify (X11) and the config folders' watch
// (UltraCanvasLinuxHostAppearanceWatch.h), which covers dconf's file for
// GSettings and the portal on GNOME, kdeglobals on KDE and settings.ini.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#include "UltraCanvasLinuxHostAppearance.h"
#include "UltraCanvasLinuxDesktop.h"
#include "UltraCanvasLinuxFonts.h"

#include <gio/gio.h>

#include <string>

namespace UltraCanvas {

namespace {

constexpr const char* kInterfaceSchema = "org.gnome.desktop.interface";
constexpr const char* kA11ySchema = "org.gnome.desktop.a11y.interface";
constexpr int kPortalTimeoutMs = 1000;

std::optional<std::string> GSettingsOptionalString(const char* schema, const char* key) {
    const std::string value = LinuxDesktop::GSettingsString(schema, key);
    if (value.empty()) return std::nullopt;
    return value;
}

}  // namespace

std::optional<uint32_t> LinuxHostAppearance::ReadPortalColorScheme() {
    if (!LinuxDesktop::IsDesktopSession()) return std::nullopt;
    GError* gerror = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (!bus) {
        if (gerror) g_error_free(gerror);
        return std::nullopt;
    }
    std::optional<uint32_t> result;
    GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.Settings", "Read",
                                                  g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
                                                  G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, kPortalTimeoutMs,
                                                  nullptr, &gerror);
    if (reply) {
        // Read answers a variant holding a variant: (v) -> v -> u.
        GVariant* outer = nullptr;
        g_variant_get(reply, "(v)", &outer);
        if (outer) {
            GVariant* inner = outer;
            if (g_variant_is_of_type(outer, G_VARIANT_TYPE_VARIANT)) inner = g_variant_get_variant(outer);
            if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT32)) result = g_variant_get_uint32(inner);
            if (inner != outer && inner) g_variant_unref(inner);
            g_variant_unref(outer);
        }
        g_variant_unref(reply);
    } else if (gerror) {
        g_error_free(gerror);
    }
    g_object_unref(bus);
    return result;
}

HostAppearanceLinux::Sources LinuxHostAppearance::ReadSources(const XSettingsFetcher& fetchXSettings) {
    HostAppearanceLinux::Sources sources;
    sources.kdeSession = LinuxDesktop::IsKdeSession();

    if (fetchXSettings) {
        std::vector<uint8_t> bytes;
        HostAppearanceLinux::XSettingsMap decoded;
        if (fetchXSettings(bytes) && HostAppearanceLinux::DecodeXSettings(bytes.data(), bytes.size(), decoded)) {
            sources.xsettings = std::move(decoded);
        }
    }

    if (LinuxDesktop::IsDesktopSession()) {
        sources.portalColorScheme = ReadPortalColorScheme();
        sources.gsColorScheme = GSettingsOptionalString(kInterfaceSchema, "color-scheme");
        sources.gsGtkTheme = GSettingsOptionalString(kInterfaceSchema, "gtk-theme");
        sources.gsAccentColor = GSettingsOptionalString(kInterfaceSchema, "accent-color");
        sources.gsTextScale = LinuxDesktop::GSettingsDouble(kInterfaceSchema, "text-scaling-factor");
        sources.gsEnableAnimations = LinuxDesktop::GSettingsBoolean(kInterfaceSchema, "enable-animations");
        sources.gsHighContrast = LinuxDesktop::GSettingsBoolean(kA11ySchema, "high-contrast");
    }

    const std::filesystem::path home = LinuxDesktop::ConfigHome();
    if (!home.empty()) {
        sources.kdeGlobals = LinuxDesktop::ReadWholeFile(home / "kdeglobals");
        sources.gtkSettingsIni = LinuxDesktop::ReadWholeFile(home / "gtk-4.0" / "settings.ini");
        if (sources.gtkSettingsIni.empty()) sources.gtkSettingsIni = LinuxDesktop::ReadWholeFile(home / "gtk-3.0" / "settings.ini");
    }
    return sources;
}

HostAppearance LinuxHostAppearance::Detect(const XSettingsFetcher& fetchXSettings) {
    HostAppearance host = HostAppearanceLinux::HostAppearanceFromSources(ReadSources(fetchXSettings));
    const FontStyle ui = LinuxFonts::DetectSystemFontStyle();
    const FontStyle mono = LinuxFonts::DetectMonospacedFontStyle();
    if (!ui.fontFamily.empty() && ui.fontSize > 0) host.uiFont = ui;
    if (!mono.fontFamily.empty() && mono.fontSize > 0) host.monoFont = mono;
    host.source = "linux:" + host.source;
    return host;
}

}  // namespace UltraCanvas
