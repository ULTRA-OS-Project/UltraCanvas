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
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSystemFont.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

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
