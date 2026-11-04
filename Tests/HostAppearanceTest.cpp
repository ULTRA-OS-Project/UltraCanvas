// Tests/HostAppearanceTest.cpp
// UltraCanvasHostAppearance.h without a display: the default (light, no
// accent, Ubuntu 12 / Ubuntu Mono 12), the conversions of the raw values the
// Windows backend fetches (AppsUseLightTheme, AccentColorMenu's 0xAABBGGRR,
// DWM's 0xAARRGGBB, TextScaleFactor), the colour-as-text helpers, the
// ULTRACANVAS_HOST_APPEARANCE override in every form it takes and the ones
// it must ignore, the report line, and equality.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasHostAppearance.h"

#include <cmath>
#include <cstdio>
#include <string>

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

    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
