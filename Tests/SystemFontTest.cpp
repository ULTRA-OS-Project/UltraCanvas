// Tests/SystemFontTest.cpp
// The desktop-font parsers without a desktop: the GTK / Pango font name,
// the kdeglobals entry, and the .ini reader the Linux backend reads
// settings.ini and kdeglobals with.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemFont.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;

static void Check(bool ok, const char* what) {
    ++testCount;
    if (!ok) { ++failCount; std::printf("FAIL: %s\n", what); }
}

static bool Gtk(const char* text, const char* family, double points) {
    std::string f = "unchanged"; double p = -1;
    const bool ok = ParseGtkFontName(text, f, p);
    return ok && f == family && std::fabs(p - points) < 1e-9;
}

static bool Kde(const char* text, const char* family, double points) {
    std::string f = "unchanged"; double p = -1;
    const bool ok = ParseKdeFontEntry(text, f, p);
    return ok && f == family && std::fabs(p - points) < 1e-9;
}

int main() {
    // GTK / Pango: family, style words, size
    Check(Gtk("Cantarell 11", "Cantarell", 11), "GNOME default");
    Check(Gtk("Noto Sans 10", "Noto Sans", 10), "two-word family");
    Check(Gtk("Noto Sans Bold 10.5", "Noto Sans", 10.5), "style word and a fraction");
    Check(Gtk("DejaVu Sans Mono Book 9", "DejaVu Sans Mono", 9), "Book is a style");
    Check(Gtk("Ubuntu Medium Italic 11", "Ubuntu", 11), "two style words");
    Check(Gtk("Sans 12px", "Sans", 9), "pixels become points");
    Check(Gtk("  \"Cantarell 11\"  ", "Cantarell", 11), "quotes and spaces");
    Check(Gtk("Source Code Pro 10", "Source Code Pro", 10), "three-word family");
    {
        std::string f = "unchanged"; double p = -1;
        Check(!ParseGtkFontName("Cantarell", f, p) && f == "unchanged" && p == -1, "no size: unchanged");
        Check(!ParseGtkFontName("11", f, p), "size alone");
        Check(!ParseGtkFontName("", f, p), "empty");
        Check(!ParseGtkFontName("Cantarell 0", f, p), "zero size");
        Check(!ParseGtkFontName("Cantarell 1,5", f, p), "a comma is not a decimal point");
    }

    // kdeglobals
    Check(Kde("Noto Sans,10,-1,5,50,0,0,0,0,0", "Noto Sans", 10), "KDE font entry");
    Check(Kde("Hack,9,-1,5,50,0,0,0,0,0,Regular", "Hack", 9), "KDE entry with the style name");
    Check(Kde("Noto Sans, 10.5, -1", "Noto Sans", 10.5), "spaces around the fields");
    {
        std::string f = "unchanged"; double p = -1;
        Check(!ParseKdeFontEntry("Noto Sans", f, p) && f == "unchanged", "no size");
        Check(!ParseKdeFontEntry(",10", f, p), "no family");
        Check(!ParseKdeFontEntry("Noto Sans,ten", f, p), "size not a number");
    }

    // The .ini reader
    const std::string ini =
        "# a comment\n"
        "[Settings]\n"
        "gtk-theme-name=Adwaita\n"
        "gtk-font-name = Cantarell 11\n"
        "\n"
        "[General]\n"
        "font=Noto Sans,10,-1,5,50,0,0,0,0,0\n"
        "fixed=\"Hack,9,-1,5,50,0,0,0,0,0\"\n"
        "; another comment\n";
    Check(ReadIniValue(ini, "Settings", "gtk-font-name") == "Cantarell 11", "key in its section, spaces trimmed");
    Check(ReadIniValue(ini, "General", "font") == "Noto Sans,10,-1,5,50,0,0,0,0,0", "KDE key");
    Check(ReadIniValue(ini, "General", "fixed") == "Hack,9,-1,5,50,0,0,0,0,0", "quotes removed");
    Check(ReadIniValue(ini, "Settings", "font").empty(), "a key of another section");
    Check(ReadIniValue(ini, "", "font") == "Noto Sans,10,-1,5,50,0,0,0,0,0", "any section");
    Check(ReadIniValue(ini, "General", "missing").empty(), "missing key");
    Check(ReadIniValue("gtk-font-name=Sans 10", "", "gtk-font-name") == "Sans 10", "no section at all");
    Check(ReadIniValue("[Settings]\r\ngtk-font-name=Sans 10\r\n", "Settings", "gtk-font-name") == "Sans 10", "CRLF");

    std::printf("SystemFontTest: %d checks, %d failed\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}
