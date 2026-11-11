// Tests/HostAppearanceTest.cpp
// UltraCanvasHostAppearance.h without a display: the default (light, no
// accent, Ubuntu 12 / Ubuntu Mono 12), the conversions of the raw values the
// Windows backend fetches (AppsUseLightTheme, AccentColorMenu's 0xAABBGGRR,
// DWM's 0xAARRGGBB, TextScaleFactor), the colour-as-text helpers, the
// ULTRACANVAS_HOST_APPEARANCE override in every form it takes and the ones
// it must ignore, the report line, and equality.
// Version: 1.2.0 - Appearance.conf: Parse, Serialize, ApplyTo over the host, the override still winning
// Version: 1.1.0 - the Linux readers: the XSETTINGS decoder in both byte orders, the
//                  theme-name, colour-scheme, portal, kdeglobals and GNOME accent rules,
//                  the composer's precedence per session
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#include "UltraCanvasHostAppearance.h"
#include "UltraCanvasAppearanceSettings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;
#define CHECK(cond, name) do { ++testCount; if (cond) std::printf("  PASS  %s\n", name); else { ++failCount; std::printf("  FAIL  %s (line %d)\n", name, __LINE__); } } while (0)
static bool Same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    std::printf("HostAppearanceTest\n\n");

    // ----- the default -----
    {
        const HostAppearance host = DefaultHostAppearance();
        CHECK(!host.dark && !host.highContrast && !host.reduceMotion, "the default is light, normal contrast, full motion");
        CHECK(!host.accent.has_value(), "the default has no accent");
        CHECK(host.uiFont.fontFamily == "Ubuntu" && Near(host.uiFont.fontSize, 12.0), "the default interface font is Ubuntu 12");
        CHECK(host.monoFont.fontFamily == "Ubuntu Mono" && Near(host.monoFont.fontSize, 12.0), "the default monospace font is Ubuntu Mono 12");
        CHECK(Near(host.textScale, 1.0) && host.source == "default", "the default scale is 1.0 from \"default\"");
        CHECK(host == DefaultHostAppearance(), "two defaults are equal");
    }

    // ----- colours as text -----
    {
        Color color;
        CHECK(ColorFromHex("#0078d4", color) && Same(color, Color(0x00, 0x78, 0xd4, 255)), "#rrggbb parses");
        CHECK(ColorFromHex("E95420", color) && Same(color, Color(0xe9, 0x54, 0x20, 255)), "rrggbb without the hash parses, upper case too");
        CHECK(ColorFromHex("#10203080", color) && Same(color, Color(0x10, 0x20, 0x30, 0x80)), "#rrggbbaa parses with its alpha");
        CHECK(ColorFromHex(" #abcdef ", color) && Same(color, Color(0xab, 0xcd, 0xef, 255)), "spaces around the colour are ignored");
        Color untouched(1, 2, 3, 4);
        CHECK(!ColorFromHex("#12345", untouched) && !ColorFromHex("#gg0000", untouched) && !ColorFromHex("", untouched)
              && Same(untouched, Color(1, 2, 3, 4)), "a short, a non-hex and an empty colour are refused and leave the output alone");
        CHECK(HexOfColor(Color(0x00, 0x78, 0xd4, 255)) == "#0078d4", "HexOfColor writes #rrggbb");
        CHECK(HexOfColor(Color(0x10, 0x20, 0x30, 0x80)) == "#10203080", "HexOfColor writes the alpha when it is not 255");
    }

    // ----- the Windows raw values -----
    {
        CHECK(HostAppearanceWindows::DarkFromAppsUseLightTheme(0) && !HostAppearanceWindows::DarkFromAppsUseLightTheme(1),
              "AppsUseLightTheme 0 is dark, 1 light");
        CHECK(Same(HostAppearanceWindows::AccentFromAbgr(0xFFD47800u), Color(0x00, 0x78, 0xd4, 255)),
              "AccentColorMenu 0xAABBGGRR gives the colour Settings shows");
        CHECK(Same(HostAppearanceWindows::AccentFromArgb(0xC40078D4u), Color(0x00, 0x78, 0xd4, 255)),
              "DwmGetColorizationColor 0xAARRGGBB gives the colour, opaque");
        CHECK(Near(HostAppearanceWindows::TextScaleFromPercent(100), 1.0) && Near(HostAppearanceWindows::TextScaleFromPercent(125), 1.25)
              && Near(HostAppearanceWindows::TextScaleFromPercent(225), 2.25), "TextScaleFactor 100, 125 and 225 are 1.0, 1.25 and 2.25");
        CHECK(Near(HostAppearanceWindows::TextScaleFromPercent(0), 1.0) && Near(HostAppearanceWindows::TextScaleFromPercent(50), 1.0)
              && Near(HostAppearanceWindows::TextScaleFromPercent(300), 1.0), "a TextScaleFactor outside 100-225 is 1.0");
    }

    // ----- the override -----
    {
        HostAppearance host = DefaultHostAppearance();
        CHECK(!ApplyHostAppearanceOverride(host, "") && host == DefaultHostAppearance(), "an empty override changes nothing");
        CHECK(!ApplyHostAppearanceOverride(host, "blue,size=14,=,,") && host == DefaultHostAppearance(),
              "parts it does not understand change nothing and leave the source alone");

        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "dark") && host.dark && host.source == "default+override", "\"dark\" sets dark and marks the source");
        CHECK(ApplyHostAppearanceOverride(host, "light") && !host.dark, "\"light\" sets light");
        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "look=dark") && host.dark, "\"look=dark\" sets dark");
        CHECK(ApplyHostAppearanceOverride(host, " Look = Light ") && !host.dark, "keys and values are trimmed and case-insensitive");

        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "contrast=high") && host.highContrast, "\"contrast=high\" sets high contrast");
        CHECK(ApplyHostAppearanceOverride(host, "contrast=normal") && !host.highContrast, "\"contrast=normal\" clears it");
        CHECK(ApplyHostAppearanceOverride(host, "high-contrast") && host.highContrast, "\"high-contrast\" sets it too");
        CHECK(!ApplyHostAppearanceOverride(host, "contrast=loud"), "an unknown contrast word is ignored");

        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "motion=reduce") && host.reduceMotion, "\"motion=reduce\" sets reduced motion");
        CHECK(ApplyHostAppearanceOverride(host, "motion=full") && !host.reduceMotion, "\"motion=full\" clears it");
        CHECK(ApplyHostAppearanceOverride(host, "reduce-motion") && host.reduceMotion, "\"reduce-motion\" sets it too");

        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "accent=#e95420") && host.accent && Same(*host.accent, Color(0xe9, 0x54, 0x20, 255)),
              "\"accent=#rrggbb\" sets the accent");
        CHECK(ApplyHostAppearanceOverride(host, "accent=none") && !host.accent, "\"accent=none\" removes it");
        CHECK(!ApplyHostAppearanceOverride(host, "accent=orange") && !host.accent, "an accent that is not a colour is ignored");

        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "font=Noto Sans 14") && host.uiFont.fontFamily == "Noto Sans" && Near(host.uiFont.fontSize, 14.0),
              "\"font=Noto Sans 14\" sets the interface font");
        CHECK(host.monoFont.fontFamily == "Ubuntu Mono", "the monospace font is untouched by \"font=\"");
        CHECK(ApplyHostAppearanceOverride(host, "mono=Fira Code Medium 13") && host.monoFont.fontFamily == "Fira Code" && Near(host.monoFont.fontSize, 13.0),
              "\"mono=Fira Code Medium 13\" sets the monospace font, the style word dropped");
        CHECK(!ApplyHostAppearanceOverride(host, "font=Noto Sans") && host.uiFont.fontFamily == "Noto Sans" && Near(host.uiFont.fontSize, 14.0),
              "a font without a size is ignored");

        host = DefaultHostAppearance();
        CHECK(ApplyHostAppearanceOverride(host, "scale=1.25") && Near(host.textScale, 1.25), "\"scale=1.25\" sets the text scale");
        CHECK(!ApplyHostAppearanceOverride(host, "scale=0.1") && Near(host.textScale, 1.25), "a scale under 0.5 is ignored");
        CHECK(!ApplyHostAppearanceOverride(host, "scale=9") && Near(host.textScale, 1.25), "a scale over 4 is ignored");
        CHECK(ApplyHostAppearanceOverride(host, "scale=1,5") && Near(host.textScale, 1.0), "a comma is a separator, not a decimal point: \"scale=1,5\" is scale 1 and a part \"5\" ignored");

        host = DefaultHostAppearance();
        host.source = "windows-registry";
        const bool all = ApplyHostAppearanceOverride(host, "dark,font=Noto Sans 14,mono=Fira Code 13,accent=#e95420,scale=1.25,contrast=high,motion=reduce");
        CHECK(all && host.dark && host.highContrast && host.reduceMotion && host.accent && host.uiFont.fontFamily == "Noto Sans"
              && host.monoFont.fontFamily == "Fira Code" && Near(host.textScale, 1.25) && host.source == "windows-registry+override",
              "every part at once, in one line, over a platform's answer");
        CHECK(ApplyHostAppearanceOverride(host, "light") && host.source == "windows-registry+override+override",
              "a second override marks the source again (one call per read in practice)");
    }

    // ----- the report -----
    {
        HostAppearance host = DefaultHostAppearance();
        CHECK(HostAppearanceReport(host) == "light, no accent, font Ubuntu 12, mono Ubuntu Mono 12, from default",
              "the default's report line");
        host.dark = true;
        host.highContrast = true;
        host.reduceMotion = true;
        host.accent = Color(0x00, 0x78, 0xd4, 255);
        host.uiFont.fontFamily = "Segoe UI";
        host.uiFont.fontSize = 9;
        host.monoFont.fontFamily = "Consolas";
        host.monoFont.fontSize = 9;
        host.textScale = 1.25;
        host.source = "windows-registry";
        CHECK(HostAppearanceReport(host) == "dark, high contrast, reduced motion, accent #0078d4, font Segoe UI 9, mono Consolas 9, scale 1.25, from windows-registry",
              "a full report line");
    }

    // ----- equality -----
    {
        HostAppearance a = DefaultHostAppearance(), b = DefaultHostAppearance();
        CHECK(a == b && !(a != b), "equal values compare equal");
        b.accent = Color(1, 2, 3, 255);
        CHECK(a != b, "an accent against none differs");
        a.accent = Color(1, 2, 3, 255);
        CHECK(a == b, "the same accent compares equal");
        b.uiFont.fontSize = 13;
        CHECK(a != b, "a font size differs");
        b = a;
        b.source = "override";
        CHECK(a != b, "the source is part of the value");
    }


    // ----- the Linux readers -----
    {
        using namespace HostAppearanceLinux;
        // An XSETTINGS property as a manager writes it: little-endian, serial
        // 7, three settings - Net/ThemeName "Adwaita-dark" (string),
        // Gtk/EnableAnimations 0 (integer), Net/Accent (colour) - names and
        // strings padded to four bytes.
        auto le16 = [](std::vector<uint8_t>& b, uint32_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); };
        auto le32 = [](std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); };
        auto name = [&](std::vector<uint8_t>& b, uint8_t type, const std::string& n) {
            b.push_back(type); b.push_back(0); le16(b, static_cast<uint32_t>(n.size()));
            for (char c : n) b.push_back(static_cast<uint8_t>(c));
            while (b.size() % 4) b.push_back(0);
            le32(b, 1);   // last-change serial
        };
        std::vector<uint8_t> buffer;
        buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(0);   // byte order LSB, pad
        le32(buffer, 7);   // serial
        le32(buffer, 3);   // count
        name(buffer, 1, "Net/ThemeName");
        const std::string theme = "Adwaita-dark";
        le32(buffer, static_cast<uint32_t>(theme.size()));
        for (char c : theme) buffer.push_back(static_cast<uint8_t>(c));
        while (buffer.size() % 4) buffer.push_back(0);
        name(buffer, 0, "Gtk/EnableAnimations");
        le32(buffer, 0);
        name(buffer, 2, "Net/Accent");
        le16(buffer, 0x3535); le16(buffer, 0x8484); le16(buffer, 0xe4e4); le16(buffer, 0xffff);

        XSettingsMap map;
        CHECK(DecodeXSettings(buffer.data(), buffer.size(), map) && map.size() == 3, "an XSETTINGS property decodes, three settings");
        CHECK(map["Net/ThemeName"].type == 1 && map["Net/ThemeName"].text == "Adwaita-dark", "the string setting");
        CHECK(map["Gtk/EnableAnimations"].type == 0 && map["Gtk/EnableAnimations"].integer == 0, "the integer setting");
        CHECK(map["Net/Accent"].type == 2 && Same(map["Net/Accent"].color, Color(0x35, 0x84, 0xe4, 255)), "the colour setting, 16-bit channels to 8");

        std::vector<uint8_t> big = buffer;   // the same, big-endian
        big[0] = 1;
        auto swap32 = [&](size_t at) { std::swap(big[at], big[at + 3]); std::swap(big[at + 1], big[at + 2]); };
        auto swap16 = [&](size_t at) { std::swap(big[at], big[at + 1]); };
        // serial, count, then per setting: name-len (16), last-change (32), value
        swap32(4); swap32(8);
        size_t at = 12;
        for (int i = 0; i < 3; ++i) {
            const uint8_t type = big[at];
            swap16(at + 2);
            const size_t nameLength = buffer[at + 2] | (buffer[at + 3] << 8);
            at += 4 + ((nameLength + 3) & ~size_t(3));
            swap32(at); at += 4;
            if (type == 1) { const size_t len = buffer[at]; swap32(at); at += 4 + ((len + 3) & ~size_t(3)); }
            else if (type == 0) { swap32(at); at += 4; }
            else { for (int k = 0; k < 4; ++k) swap16(at + 2 * k); at += 8; }
        }
        XSettingsMap bigMap;
        CHECK(DecodeXSettings(big.data(), big.size(), bigMap) && bigMap["Net/ThemeName"].text == "Adwaita-dark"
              && bigMap["Gtk/EnableAnimations"].integer == 0 && Same(bigMap["Net/Accent"].color, Color(0x35, 0x84, 0xe4, 255)),
              "the big-endian form decodes the same");
        XSettingsMap untouched;
        CHECK(!DecodeXSettings(buffer.data(), 20, untouched) && untouched.empty(), "a truncated property is refused");
        CHECK(!DecodeXSettings(nullptr, 0, untouched), "no property is refused");

        CHECK(DarkFromThemeName("Adwaita-dark") && DarkFromThemeName("Breeze Dark") && DarkFromThemeName("Arc-Darker")
              && !DarkFromThemeName("Adwaita") && !DarkFromThemeName("Yaru"), "a theme name says dark when it contains the word");
        CHECK(DarkFromColorScheme("prefer-dark") == true && DarkFromColorScheme("prefer-light") == false
              && !DarkFromColorScheme("default").has_value() && !DarkFromColorScheme("").has_value(), "the GSettings colour scheme");
        CHECK(DarkFromPortalColorScheme(1) == true && DarkFromPortalColorScheme(2) == false && !DarkFromPortalColorScheme(0).has_value()
              && !DarkFromPortalColorScheme(7).has_value(), "the portal's colour scheme");
        Color kde;
        CHECK(ParseKdeColor("61,174,233", kde) && Same(kde, Color(61, 174, 233, 255)), "a kdeglobals colour");
        CHECK(ParseKdeColor(" 35, 38, 41, 128 ", kde) && Same(kde, Color(35, 38, 41, 128)), "a kdeglobals colour with alpha and spaces");
        CHECK(!ParseKdeColor("61,174", kde) && !ParseKdeColor("61,174,300", kde) && !ParseKdeColor("#3daee9", kde) && !ParseKdeColor("", kde),
              "two parts, a channel over 255, a hex colour and nothing are refused");
        CHECK(IsDarkColor(Color(35, 38, 41)) && IsDarkColor(Color(0, 0, 0)) && !IsDarkColor(Color(239, 240, 241)) && !IsDarkColor(Color(255, 255, 255)),
              "Breeze Dark's window is dark, Breeze's is light");
        CHECK(AccentFromGnomeName("blue") && Same(*AccentFromGnomeName("blue"), Color(0x35, 0x84, 0xe4, 255))
              && AccentFromGnomeName(" Purple ") && !AccentFromGnomeName("mauve").has_value(), "GNOME's accent names");

        // ----- the composer -----
        Sources none;
        HostAppearance h = HostAppearanceFromSources(none);
        CHECK(!h.dark && !h.highContrast && !h.reduceMotion && !h.accent && h.source == "default", "no source at all: light, from default");

        Sources portal; portal.portalColorScheme = 1; portal.gsColorScheme = "prefer-light";
        CHECK(HostAppearanceFromSources(portal).dark && HostAppearanceFromSources(portal).source == "portal",
              "the portal answers first");
        portal.portalColorScheme = 0;
        CHECK(!HostAppearanceFromSources(portal).dark && HostAppearanceFromSources(portal).source == "gsettings",
              "a portal with no preference passes to GSettings");

        Sources gs; gs.gsColorScheme = "default"; gs.gsGtkTheme = "Yaru-dark";
        CHECK(HostAppearanceFromSources(gs).dark && HostAppearanceFromSources(gs).source == "gsettings",
              "GSettings' default scheme passes to its theme name");
        gs.gsColorScheme = "prefer-dark"; gs.gsGtkTheme = "Adwaita";
        CHECK(HostAppearanceFromSources(gs).dark, "GSettings' prefer-dark wins over a light theme name (GNOME 42+)");

        Sources xs; xs.xsettings = map;   // Adwaita-dark, animations off
        h = HostAppearanceFromSources(xs);
        CHECK(h.dark && h.source == "xsettings" && h.reduceMotion, "XSETTINGS: a dark theme name and animations off");
        xs.gsColorScheme = "prefer-light";
        CHECK(!HostAppearanceFromSources(xs).dark, "GSettings' scheme is asked before XSETTINGS' theme name");

        Sources ini; ini.gtkSettingsIni = "[Settings]\ngtk-application-prefer-dark-theme=1\ngtk-theme-name=Adwaita\n";
        CHECK(HostAppearanceFromSources(ini).dark && HostAppearanceFromSources(ini).source == "gtk-settings-ini",
              "settings.ini's prefer-dark switch");
        ini.gtkSettingsIni = "[Settings]\ngtk-theme-name=Arc-Dark\n";
        CHECK(HostAppearanceFromSources(ini).dark, "settings.ini's theme name");

        Sources kdeSrc;
        kdeSrc.kdeGlobals = "[General]\nColorScheme=BreezeDark\nAccentColor=61,174,233\n[Colors:Window]\nBackgroundNormal=35,38,41\n[KDE]\nAnimationDurationFactor=0\n";
        h = HostAppearanceFromSources(kdeSrc);
        CHECK(h.dark && h.source == "kdeglobals" && h.accent && Same(*h.accent, Color(61, 174, 233, 255)) && h.reduceMotion,
              "kdeglobals: a dark scheme, the accent, animations off");
        kdeSrc.kdeGlobals = "[General]\nColorScheme=Breeze\n[Colors:Window]\nBackgroundNormal=239,240,241\n";
        CHECK(!HostAppearanceFromSources(kdeSrc).dark && HostAppearanceFromSources(kdeSrc).source == "kdeglobals",
              "kdeglobals: a light window background");
        kdeSrc.kdeGlobals = "[General]\nColorScheme=Breeze\n[Colors:Window]\nBackgroundNormal=35,38,41\n";
        CHECK(HostAppearanceFromSources(kdeSrc).dark, "kdeglobals: a scheme not named dark but a dark window background");

        Sources order;
        order.kdeGlobals = "[General]\nColorScheme=BreezeDark\nAccentColor=61,174,233\n";
        order.gsColorScheme = "prefer-light"; order.gsAccentColor = "red";
        h = HostAppearanceFromSources(order);
        CHECK(!h.dark && h.source == "gsettings" && h.accent && Same(*h.accent, Color(0xe6, 0x2d, 0x42, 255)),
              "a GNOME session asks GSettings before kdeglobals, the accent too");
        order.kdeSession = true;
        h = HostAppearanceFromSources(order);
        CHECK(h.dark && h.source == "kdeglobals" && h.accent && Same(*h.accent, Color(61, 174, 233, 255)),
              "a KDE session asks kdeglobals first, the accent too");

        Sources a11y; a11y.gsHighContrast = true; a11y.gsEnableAnimations = false; a11y.gsTextScale = 1.25;
        h = HostAppearanceFromSources(a11y);
        CHECK(h.highContrast && h.reduceMotion && Near(h.textScale, 1.25), "GSettings' high contrast, animations off and text scale");
        Sources hcTheme; hcTheme.gsGtkTheme = "HighContrastInverse";
        h = HostAppearanceFromSources(hcTheme);
        CHECK(h.highContrast && h.dark == false, "a HighContrast theme name is high contrast");
        Sources scale; scale.gsTextScale = 9.0;
        CHECK(Near(HostAppearanceFromSources(scale).textScale, 1.0), "a text scale outside 0.5-4 is ignored");
        CHECK(HostAppearanceFromSources(kdeSrc).uiFont.fontFamily == "Ubuntu", "the composer leaves the fonts to the font detectors");
    }

    std::printf("Appearance.conf\n");
    {
        const AppearanceSettings none;
        CHECK(none.IsSystem() && none.look == "system" && none.fontSize == 0.0 && none.scale == 1.0, "the defaults say system everywhere");
        CHECK(AppearanceSettings::Parse("").IsSystem() && AppearanceSettings::Parse("nonsense\n") .IsSystem(), "an empty or unknown file is the defaults");
        const AppearanceSettings s = AppearanceSettings::Parse("look=Dark\r\naccent=#E95420\nfont=Noto Sans\nfontsize=10.5\nscale=1.25\ncontrast=high\nunknown=1\n");
        CHECK(s.look == "dark" && s.accent == "#e95420" && s.font == "Noto Sans" && Near(s.fontSize, 10.5) && Near(s.scale, 1.25) && s.contrast == "high",
              "every line read: the look lowered, the accent normalised, the size and scale as numbers");
        const AppearanceSettings bad = AppearanceSettings::Parse("look=blue\naccent=red\nfontsize=-3\nscale=9\ncontrast=loud\n");
        CHECK(bad.IsSystem(), "a value the file cannot mean keeps the default");
        CHECK(AppearanceSettings::Parse(s.Serialize()) == s, "Serialize and Parse round-trip");
        CHECK(none.Serialize() == "look=system\naccent=system\nfont=system\nfontsize=system\nscale=1\ncontrast=system\n", "the defaults serialise as the documented file");
        HostAppearance host = DefaultHostAppearance();
        CHECK(!none.ApplyTo(host) && host == DefaultHostAppearance(), "the defaults change nothing");
        CHECK(s.ApplyTo(host) && host.dark && host.accent && Same(*host.accent, Color(0xe9, 0x54, 0x20)) && host.uiFont.fontFamily == "Noto Sans"
              && Near(host.uiFont.fontSize, 10.5) && Near(host.textScale, 1.25) && host.highContrast && host.source == "default+settings",
              "the choices over the host: the look, the accent, the font, the size, the scale over the host's, high contrast, the source");
        HostAppearance scaled = DefaultHostAppearance();
        scaled.textScale = 1.5;
        AppearanceSettings onlyScale;
        onlyScale.scale = 2.0;
        onlyScale.ApplyTo(scaled);
        CHECK(Near(scaled.textScale, 3.0) && scaled.uiFont.fontFamily == "Ubuntu", "the scale multiplies the host's, the rest untouched");
        HostAppearance light = DefaultHostAppearance();
        light.dark = true;
        AppearanceSettings forceLight;
        forceLight.look = "light";
        forceLight.contrast = "normal";
        light.highContrast = true;
        forceLight.ApplyTo(light);
        CHECK(!light.dark && !light.highContrast, "light and normal contrast overrule a dark, high-contrast host");
        // the one run's override still wins over the file
        AppearanceSettings dark;
        dark.look = "dark";
        HostAppearance run = DefaultHostAppearance();
        dark.ApplyTo(run);
        ApplyHostAppearanceOverride(run, "light");
        CHECK(!run.dark && run.source == "default+settings+override", "ULTRACANVAS_HOST_APPEARANCE wins over Appearance.conf for the run");
    }

    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
