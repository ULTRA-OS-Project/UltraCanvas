// include/UltraCanvasHostAppearance.h
// What the desktop looks like, read from the host: its light or dark
// look, high contrast, reduced motion, its accent colour, its interface
// and monospace fonts and its text scale - UltraCanvasHostAppearance,
// phase T1 of Docs/UltraCanvas/UltraCanvasHostThemeProposal.md. The value
// is UltraCanvasApplication::GetHostAppearance(), filled by the platform's
// DetectHostAppearanceNative and read again on the host's change event
// (AddHostAppearanceHandler hears it); GetSystemFontStyle() and
// GetDefaultMonospacedFontStyle() are its fonts.
//
// This header is the pure half: the value type, the conversions of the raw
// values a platform fetches (a registry DWORD, a colour word) and the
// ULTRACANVAS_HOST_APPEARANCE override, so Tests/HostAppearanceTest.cpp
// holds every rule without a desktop and the platform code under
// OS/<Platform>/ only fetches. Header-only: nothing here needs the core.
//
// The override, for a dark/light/14 pt run of an application and for CI
// screenshots - comma-separated, every part optional, over what the host
// said:
//   ULTRACANVAS_HOST_APPEARANCE="dark,font=Noto Sans 14,mono=Fira Code 13,accent=#e95420,scale=1.25,contrast=high,motion=reduce"
//
// doc-check: example
//   UltraCanvas::HostAppearance host = UltraCanvas::DefaultHostAppearance();
//   UltraCanvas::ApplyHostAppearanceOverride(host, "dark,font=Noto Sans 14");   // host.dark, host.uiFont Noto Sans 14
//   const std::string line = UltraCanvas::HostAppearanceReport(host);           // for a start-up report
//
// Version: 1.1.0 - the Linux readers (HostAppearanceLinux): the XSETTINGS decoder, the
//                  theme-name, colour-scheme, kdeglobals and GNOME accent rules, and the
//                  composer with the sources' precedence; T2 of the proposal
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSystemFont.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== THE VALUE =====

    struct HostAppearance {
        bool dark = false;              // the desktop prefers a dark look
        bool highContrast = false;      // Windows High Contrast, GNOME high-contrast, macOS Increase Contrast
        bool reduceMotion = false;      // the animation preference, for the elements that animate
        std::optional<Color> accent;    // the desktop's accent, when it has one
        FontStyle uiFont;               // family and size in points: Segoe UI 9, SF 13, Cantarell 11, Noto Sans 10
        FontStyle monoFont;             // Consolas, SF Mono / Menlo, the GNOME monospace font
        double textScale = 1.0;         // Windows "Text size", GNOME text-scaling-factor, Android fontScale
        // Where it came from, for a start-up report: "windows-registry",
        // "macos-appkit", "default" (the fonts only, from the platform's font
        // detectors), with "+override" when ULTRACANVAS_HOST_APPEARANCE
        // changed something.
        std::string source = "default";

        bool operator==(const HostAppearance& o) const {
            auto sameColor = [](const std::optional<Color>& a, const std::optional<Color>& b) {
                if (a.has_value() != b.has_value()) return false;
                return !a.has_value() || (a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a);
            };
            return dark == o.dark && highContrast == o.highContrast && reduceMotion == o.reduceMotion
                && sameColor(accent, o.accent) && uiFont == o.uiFont && monoFont == o.monoFont
                && textScale == o.textScale && source == o.source;
        }
        bool operator!=(const HostAppearance& o) const { return !(*this == o); }
    };

    // Light, no accent, the bundled Ubuntu 12 and Ubuntu Mono 12 - what a
    // platform without a backend answers, and what every platform answered
    // before there was one.
    inline HostAppearance DefaultHostAppearance() {
        HostAppearance host;
        host.uiFont.fontFamily = "Ubuntu";
        host.uiFont.fontSize = 12.0;
        host.monoFont.fontFamily = "Ubuntu Mono";
        host.monoFont.fontSize = 12.0;
        return host;
    }

    // ===== COLOURS AS TEXT =====

    // "#rrggbb" or "rrggbb", "#rrggbbaa"; false, `out` unchanged, on
    // anything else. Pure.
    inline bool ColorFromHex(const std::string& text, Color& out) {
        std::string hex = SystemFontDetail::Trim(text);
        if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
        if (hex.size() != 6 && hex.size() != 8) return false;
        uint32_t value = 0;
        for (char c : hex) {
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return false;
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        if (hex.size() == 6) value = (value << 8) | 0xFF;
        out = Color(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
                    static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
        return true;
    }

    // "#rrggbb" (the alpha left out when it is 255, "#rrggbbaa" otherwise). Pure.
    inline std::string HexOfColor(const Color& color) {
        static const char* const kDigits = "0123456789abcdef";
        std::string text = "#";
        auto put = [&](uint8_t v) { text += kDigits[v >> 4]; text += kDigits[v & 0xF]; };
        put(color.r); put(color.g); put(color.b);
        if (color.a != 255) put(color.a);
        return text;
    }

    // ===== THE RAW VALUES OF THE PLATFORMS =====

    namespace HostAppearanceWindows {
        // HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
        // AppsUseLightTheme: 0 is dark, 1 (or the value missing) light.
        inline bool DarkFromAppsUseLightTheme(uint32_t value) { return value == 0; }

        // HKCU\...\Explorer\Accent AccentColorMenu, the colour Settings
        // shows, stored as 0xAABBGGRR.
        inline Color AccentFromAbgr(uint32_t abgr) {
            return Color(static_cast<uint8_t>(abgr & 0xFF), static_cast<uint8_t>((abgr >> 8) & 0xFF),
                         static_cast<uint8_t>((abgr >> 16) & 0xFF), 255);
        }

        // DwmGetColorizationColor's colour, 0xAARRGGBB.
        inline Color AccentFromArgb(uint32_t argb) {
            return Color(static_cast<uint8_t>((argb >> 16) & 0xFF), static_cast<uint8_t>((argb >> 8) & 0xFF),
                         static_cast<uint8_t>(argb & 0xFF), 255);
        }

        // HKCU\Software\Microsoft\Accessibility TextScaleFactor, 100 to 225
        // (the Settings slider); 1.0 when the value is missing or outside.
        inline double TextScaleFromPercent(uint32_t percent) {
            if (percent < 100 || percent > 225) return 1.0;
            return static_cast<double>(percent) / 100.0;
        }
    }

    // ===== THE RAW VALUES OF LINUX =====
    //
    // What a Linux desktop says and where - decoded here, fetched by
    // OS/Linux/UltraCanvasLinuxHostAppearance.cpp:
    //   XSETTINGS   the _XSETTINGS_S<n> manager's property (GNOME, XFCE, MATE,
    //               Cinnamon, xsettingsd): Net/ThemeName, Gtk/FontName,
    //               Gtk/EnableAnimations
    //   GSettings   org.gnome.desktop.interface color-scheme, gtk-theme,
    //               accent-color, text-scaling-factor, enable-animations;
    //               org.gnome.desktop.a11y.interface high-contrast
    //   kdeglobals  [General] ColorScheme, AccentColor; [Colors:Window]
    //               BackgroundNormal; [KDE] AnimationDurationFactor
    //   settings.ini  gtk-application-prefer-dark-theme, gtk-theme-name
    //   the portal  org.freedesktop.appearance color-scheme (0 none, 1 dark, 2 light)

    namespace HostAppearanceLinux {

        // One XSETTINGS entry: an integer, a string or a colour.
        struct XSettingsValue {
            int type = 0;              // 0 integer, 1 string, 2 colour
            int32_t integer = 0;
            std::string text;
            Color color;
        };
        using XSettingsMap = std::map<std::string, XSettingsValue>;

        // The _XSETTINGS_SETTINGS property, as the XSETTINGS specification
        // lays it out: a byte-order byte, the serial, the count, then each
        // setting as type, name, last-change serial and value, names and
        // strings padded to four bytes. False, `out` untouched, on a buffer
        // that does not parse. Pure.
        inline bool DecodeXSettings(const uint8_t* data, size_t size, XSettingsMap& out) {
            if (!data || size < 12) return false;
            const bool bigEndian = data[0] == 1;
            auto u16 = [&](size_t at) -> uint32_t {
                return bigEndian ? (static_cast<uint32_t>(data[at]) << 8) | data[at + 1]
                                 : (static_cast<uint32_t>(data[at + 1]) << 8) | data[at];
            };
            auto u32 = [&](size_t at) -> uint32_t {
                return bigEndian ? (static_cast<uint32_t>(data[at]) << 24) | (static_cast<uint32_t>(data[at + 1]) << 16)
                                       | (static_cast<uint32_t>(data[at + 2]) << 8) | data[at + 3]
                                 : (static_cast<uint32_t>(data[at + 3]) << 24) | (static_cast<uint32_t>(data[at + 2]) << 16)
                                       | (static_cast<uint32_t>(data[at + 1]) << 8) | data[at];
            };
            auto pad4 = [](size_t n) { return (n + 3) & ~static_cast<size_t>(3); };
            XSettingsMap parsed;
            const uint32_t count = u32(8);
            size_t at = 12;
            for (uint32_t i = 0; i < count; ++i) {
                if (at + 4 > size) return false;
                XSettingsValue value;
                value.type = data[at];
                const size_t nameLength = u16(at + 2);
                at += 4;
                if (at + pad4(nameLength) + 4 > size) return false;
                const std::string name(reinterpret_cast<const char*>(data + at), nameLength);
                at += pad4(nameLength) + 4;   // the name, then the last-change serial
                if (value.type == 0) {
                    if (at + 4 > size) return false;
                    value.integer = static_cast<int32_t>(u32(at));
                    at += 4;
                } else if (value.type == 1) {
                    if (at + 4 > size) return false;
                    const size_t length = u32(at);
                    at += 4;
                    if (at + pad4(length) > size) return false;
                    value.text.assign(reinterpret_cast<const char*>(data + at), length);
                    at += pad4(length);
                } else if (value.type == 2) {
                    if (at + 8 > size) return false;
                    value.color = Color(static_cast<uint8_t>(u16(at) >> 8), static_cast<uint8_t>(u16(at + 2) >> 8),
                                        static_cast<uint8_t>(u16(at + 4) >> 8), static_cast<uint8_t>(u16(at + 6) >> 8));
                    at += 8;
                } else {
                    return false;
                }
                parsed[name] = value;
            }
            out = std::move(parsed);
            return true;
        }

        // "Adwaita-dark", "Yaru-dark", "Breeze Dark", "Arc-Darker": a theme
        // whose name says dark. Pure.
        inline bool DarkFromThemeName(const std::string& name) {
            return SystemFontDetail::Lower(name).find("dark") != std::string::npos;
        }

        // GSettings color-scheme: "prefer-dark" is dark, "prefer-light" light,
        // "default" (or anything else) no preference. Pure.
        inline std::optional<bool> DarkFromColorScheme(const std::string& scheme) {
            const std::string value = SystemFontDetail::Lower(SystemFontDetail::Trim(scheme));
            if (value == "prefer-dark") return true;
            if (value == "prefer-light") return false;
            return std::nullopt;
        }

        // The portal's org.freedesktop.appearance color-scheme: 1 dark, 2
        // light, 0 (or anything else) no preference. Pure.
        inline std::optional<bool> DarkFromPortalColorScheme(uint32_t scheme) {
            if (scheme == 1) return true;
            if (scheme == 2) return false;
            return std::nullopt;
        }

        // "61,174,233" or "61,174,233,255": the kdeglobals colour form. Pure.
        inline bool ParseKdeColor(const std::string& text, Color& out) {
            std::vector<int> parts;
            std::string current;
            for (char c : SystemFontDetail::Trim(text) + ",") {
                if (c == ',') {
                    const std::string digits = SystemFontDetail::Trim(current);
                    current.clear();
                    if (digits.empty() || digits.size() > 3) return false;
                    for (char d : digits) if (d < '0' || d > '9') return false;
                    const int v = std::atoi(digits.c_str());
                    if (v > 255) return false;
                    parts.push_back(v);
                } else {
                    current += c;
                }
            }
            if (parts.size() != 3 && parts.size() != 4) return false;
            out = Color(static_cast<uint8_t>(parts[0]), static_cast<uint8_t>(parts[1]), static_cast<uint8_t>(parts[2]),
                        static_cast<uint8_t>(parts.size() == 4 ? parts[3] : 255));
            return true;
        }

        // Relative luminance under 0.5: a window background that is dark. Pure.
        inline bool IsDarkColor(const Color& color) {
            auto channel = [](uint8_t v) {
                const double c = v / 255.0;
                return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b) < 0.5;
        }

        // GNOME 47's accent-color names and the colours Settings shows for
        // them; nothing for a name it does not know. Pure.
        inline std::optional<Color> AccentFromGnomeName(const std::string& name) {
            struct Entry { const char* name; Color color; };
            static const Entry kNames[] = {
                {"blue", Color(0x35, 0x84, 0xe4)}, {"teal", Color(0x21, 0x90, 0xa4)}, {"green", Color(0x3a, 0x94, 0x4a)},
                {"yellow", Color(0xc8, 0x88, 0x00)}, {"orange", Color(0xed, 0x5b, 0x00)}, {"red", Color(0xe6, 0x2d, 0x42)},
                {"pink", Color(0xd5, 0x61, 0x99)}, {"purple", Color(0x91, 0x41, 0xac)}, {"slate", Color(0x6f, 0x83, 0x96)},
            };
            const std::string lower = SystemFontDetail::Lower(SystemFontDetail::Trim(name));
            for (const Entry& e : kNames) if (lower == e.name) return e.color;
            return std::nullopt;
        }

        // What the platform fetched, each part absent when its source is not
        // there; HostAppearanceFromSources composes them.
        struct Sources {
            bool kdeSession = false;                     // XDG_CURRENT_DESKTOP names KDE
            std::optional<XSettingsMap> xsettings;       // the manager's property, decoded
            std::optional<uint32_t> portalColorScheme;   // the portal's answer
            std::optional<std::string> gsColorScheme;    // org.gnome.desktop.interface color-scheme
            std::optional<std::string> gsGtkTheme;       // gtk-theme
            std::optional<std::string> gsAccentColor;    // accent-color (GNOME 47)
            std::optional<double> gsTextScale;           // text-scaling-factor
            std::optional<bool> gsEnableAnimations;      // enable-animations
            std::optional<bool> gsHighContrast;          // org.gnome.desktop.a11y.interface high-contrast
            std::string kdeGlobals;                      // ~/.config/kdeglobals, empty when absent
            std::string gtkSettingsIni;                  // ~/.config/gtk-3.0/settings.ini (gtk-4.0 first), empty when absent
        };

        // The look from the sources, first answer wins, the KDE files first in
        // a KDE session: dark from the portal, GSettings' colour scheme, the
        // theme names of XSETTINGS, GSettings and settings.ini, settings.ini's
        // prefer-dark switch, kdeglobals' scheme name and window background;
        // high contrast from GSettings or a theme named HighContrast;
        // reduced motion from GSettings, XSETTINGS or kdeglobals' animation
        // factor; the accent from GSettings' name or kdeglobals; the text
        // scale from GSettings. The fonts are left to the font detectors;
        // `source` names what answered the look. Pure.
        inline HostAppearance HostAppearanceFromSources(const Sources& in) {
            HostAppearance host = DefaultHostAppearance();
            std::optional<bool> dark;
            std::string source;
            auto answer = [&](std::optional<bool> value, const char* from) {
                if (dark.has_value() || !value.has_value()) return;
                dark = value;
                source = from;
            };
            auto fromKde = [&]() {
                if (in.kdeGlobals.empty()) return;
                const std::string scheme = ReadIniValue(in.kdeGlobals, "General", "ColorScheme");
                if (!scheme.empty()) {
                    if (DarkFromThemeName(scheme)) { answer(true, "kdeglobals"); return; }
                }
                Color background;
                if (ParseKdeColor(ReadIniValue(in.kdeGlobals, "Colors:Window", "BackgroundNormal"), background)) {
                    answer(IsDarkColor(background), "kdeglobals");
                } else if (!scheme.empty()) {
                    answer(false, "kdeglobals");
                }
            };
            auto fromIni = [&]() {
                if (in.gtkSettingsIni.empty()) return;
                const std::string prefer = ReadIniValue(in.gtkSettingsIni, "Settings", "gtk-application-prefer-dark-theme");
                if (prefer == "1" || SystemFontDetail::Lower(prefer) == "true") { answer(true, "gtk-settings-ini"); return; }
                const std::string theme = ReadIniValue(in.gtkSettingsIni, "Settings", "gtk-theme-name");
                if (!theme.empty()) answer(DarkFromThemeName(theme), "gtk-settings-ini");
            };
            auto fromXSettings = [&]() {
                if (!in.xsettings) return;
                auto it = in.xsettings->find("Net/ThemeName");
                if (it != in.xsettings->end() && it->second.type == 1 && !it->second.text.empty()) {
                    answer(DarkFromThemeName(it->second.text), "xsettings");
                }
            };
            auto fromGSettings = [&]() {
                if (in.gsColorScheme) answer(DarkFromColorScheme(*in.gsColorScheme), "gsettings");
                if (in.gsGtkTheme && !in.gsGtkTheme->empty()) answer(DarkFromThemeName(*in.gsGtkTheme), "gsettings");
            };

            if (in.portalColorScheme) answer(DarkFromPortalColorScheme(*in.portalColorScheme), "portal");
            if (in.kdeSession) {
                fromKde(); fromGSettings(); fromXSettings(); fromIni();
            } else {
                fromGSettings(); fromXSettings(); fromIni(); fromKde();
            }
            host.dark = dark.value_or(false);
            host.source = source.empty() ? "default" : source;

            // high contrast
            if (in.gsHighContrast && *in.gsHighContrast) host.highContrast = true;
            if (in.gsGtkTheme && SystemFontDetail::Lower(*in.gsGtkTheme).find("highcontrast") != std::string::npos) host.highContrast = true;
            if (!in.kdeGlobals.empty()) {
                const std::string scheme = SystemFontDetail::Lower(ReadIniValue(in.kdeGlobals, "General", "ColorScheme"));
                if (scheme.find("highcontrast") != std::string::npos || scheme.find("high contrast") != std::string::npos) host.highContrast = true;
            }

            // reduced motion
            if (in.gsEnableAnimations && !*in.gsEnableAnimations) host.reduceMotion = true;
            if (in.xsettings) {
                auto it = in.xsettings->find("Gtk/EnableAnimations");
                if (it != in.xsettings->end() && it->second.type == 0 && it->second.integer == 0) host.reduceMotion = true;
            }
            if (!in.kdeGlobals.empty()) {
                const std::string factor = ReadIniValue(in.kdeGlobals, "KDE", "AnimationDurationFactor");
                double value = 0;
                if (!factor.empty() && (factor == "0" || (SystemFontDetail::ParseSize(factor, value) && value <= 0))) host.reduceMotion = true;
                if (factor == "0" || factor == "0.0") host.reduceMotion = true;
            }

            // the accent
            auto kdeAccent = [&]() {
                Color color;
                if (!in.kdeGlobals.empty() && ParseKdeColor(ReadIniValue(in.kdeGlobals, "General", "AccentColor"), color)) host.accent = color;
            };
            auto gnomeAccent = [&]() {
                if (in.gsAccentColor) {
                    if (std::optional<Color> color = AccentFromGnomeName(*in.gsAccentColor)) host.accent = color;
                }
            };
            if (in.kdeSession) { kdeAccent(); if (!host.accent) gnomeAccent(); }
            else { gnomeAccent(); if (!host.accent) kdeAccent(); }

            // the text scale
            if (in.gsTextScale && *in.gsTextScale >= 0.5 && *in.gsTextScale <= 4.0) host.textScale = *in.gsTextScale;
            return host;
        }
    }

    // ===== THE OVERRIDE =====

    // ULTRACANVAS_HOST_APPEARANCE over `host`: "dark" / "light",
    // "contrast=high" / "contrast=normal" (or "high-contrast"),
    // "motion=reduce" / "motion=full" (or "reduce-motion"), "accent=#rrggbb"
    // / "accent=none", "font=<family> <points>" and "mono=<family> <points>"
    // in the GTK form, "scale=<factor>"; comma-separated, in any order, a
    // part it does not understand ignored. True when anything changed; the
    // source then carries "+override". Pure.
    inline bool ApplyHostAppearanceOverride(HostAppearance& host, const std::string& spec) {
        bool applied = false;
        std::string part;
        auto take = [&](const std::string& raw) {
            const std::string token = SystemFontDetail::Trim(raw);
            if (token.empty()) return;
            const size_t eq = token.find('=');
            const std::string key = SystemFontDetail::Lower(SystemFontDetail::Trim(eq == std::string::npos ? token : token.substr(0, eq)));
            const std::string value = eq == std::string::npos ? std::string() : SystemFontDetail::Trim(token.substr(eq + 1));
            const std::string lower = SystemFontDetail::Lower(value);
            if (key == "dark") { host.dark = true; applied = true; }
            else if (key == "light") { host.dark = false; applied = true; }
            else if (key == "high-contrast") { host.highContrast = true; applied = true; }
            else if (key == "reduce-motion") { host.reduceMotion = true; applied = true; }
            else if (key == "look" && (lower == "dark" || lower == "light")) { host.dark = lower == "dark"; applied = true; }
            else if (key == "contrast" && (lower == "high" || lower == "normal")) { host.highContrast = lower == "high"; applied = true; }
            else if (key == "motion" && (lower == "reduce" || lower == "full")) { host.reduceMotion = lower == "reduce"; applied = true; }
            else if (key == "accent") {
                Color color;
                if (lower == "none") { host.accent.reset(); applied = true; }
                else if (ColorFromHex(value, color)) { host.accent = color; applied = true; }
            }
            else if (key == "font" || key == "mono") {
                std::string family;
                double points = 0;
                if (ParseGtkFontName(value, family, points)) {
                    FontStyle& font = key == "font" ? host.uiFont : host.monoFont;
                    font.fontFamily = family;
                    font.fontSize = points;
                    applied = true;
                }
            }
            else if (key == "scale") {
                double factor = 0;
                if (SystemFontDetail::ParseSize(value, factor) && factor >= 0.5 && factor <= 4.0) {
                    host.textScale = factor;
                    applied = true;
                }
            }
        };
        for (char c : spec) {
            if (c == ',') { take(part); part.clear(); }
            else part += c;
        }
        take(part);
        if (applied) host.source += "+override";
        return applied;
    }

    // One line for a start-up report: "dark, high contrast, accent #0078d4,
    // font Segoe UI 9, mono Consolas 9, scale 1.25, from windows-registry".
    inline std::string HostAppearanceReport(const HostAppearance& host) {
        auto number = [](double v) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", v);
            return std::string(buffer);
        };
        std::string line = host.dark ? "dark" : "light";
        if (host.highContrast) line += ", high contrast";
        if (host.reduceMotion) line += ", reduced motion";
        line += host.accent ? ", accent " + HexOfColor(*host.accent) : std::string(", no accent");
        line += ", font " + host.uiFont.fontFamily + " " + number(host.uiFont.fontSize);
        line += ", mono " + host.monoFont.fontFamily + " " + number(host.monoFont.fontSize);
        if (host.textScale != 1.0) line += ", scale " + number(host.textScale);
        line += ", from " + host.source;
        return line;
    }

}  // namespace UltraCanvas
